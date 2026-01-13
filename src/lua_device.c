#include "lua_device.h"
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/timerfd.h>

#include <linux/input-event-codes.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include "poll_group.h"

#define REG_FD_MAP "fd_map"
#define REG_NAME_TIMER "timer"
#define REG_NAME_EVDEV "evdev"
#define REG_NAME_UINPUT "uinput"

static int lua_device_load(struct lua_State *ls, int narg);

static int l_new_object(struct lua_State *ls)
{
	void *data = lua_touserdata(ls, 1);
	const char *reg_name = lua_tostring(ls, 2);
	*(void **)lua_newuserdata(ls, sizeof(void *)) = data;
	luaL_setmetatable(ls, reg_name);
	return 1;
}

#define L_NEW_OBJECT(data, reg_name, ...)		\
do {							\
	int rc;						\
	lua_pushcfunction(ls, l_new_object);		\
	lua_pushlightuserdata(ls, (void *)(data));	\
	lua_pushliteral(ls, reg_name);			\
	rc = lua_pcall(ls, 2, 1, 0);			\
	if (rc != LUA_OK) {				\
		__VA_ARGS__;				\
		return lua_error(ls);			\
	}						\
} while (0)

static int l_sys_meminfo(struct lua_State *ls)
{
	struct lua_device_info_t *info = *(struct lua_device_info_t **)lua_getextraspace(ls);
	lua_pushinteger(ls, info->mem_usage);
	if (info->mem_limit)
		lua_pushinteger(ls, info->mem_limit);
	else
		lua_pushnil(ls);
	return 2;
}

static int l_sys_exit(struct lua_State *ls)
{
	int sig = SIGINT;
	if (lua_toboolean(ls, 1))
		sig = SIGTERM;
	kill(0, sig);
	return 0;
}

static int l_sys_gettime(struct lua_State *ls)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	lua_pushinteger(ls, ts.tv_sec);
	lua_pushinteger(ls, ts.tv_nsec);
	return 2;
}

static int l_sys_timer(struct lua_State *ls)
{
	int fd;
	struct lua_device_info_t *info = *(struct lua_device_info_t **)lua_getextraspace(ls);
	luaL_checktype(ls, 1, LUA_TFUNCTION);
	fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if (fd < 0)
		return luaL_error(ls, "cannot create timer: %s", strerror(errno));

	luaL_getsubtable(ls, LUA_REGISTRYINDEX, REG_FD_MAP);
	L_NEW_OBJECT((uintptr_t)fd, REG_NAME_TIMER, close(fd));

	// user value
	lua_pushvalue(ls, 1);
	lua_setuservalue(ls, -2);

	// put in FD_MAP
	lua_pushvalue(ls, -1);
	lua_seti(ls, -3, fd);

	poll_group_add(info->poll_group, fd);

	return 1;
}

static int l_timer_close(struct lua_State *ls)
{
	int fd;
	struct lua_device_info_t *info = *(struct lua_device_info_t **)lua_getextraspace(ls);
	fd = (int)*(uintptr_t *)luaL_checkudata(ls, 1, REG_NAME_TIMER);

	poll_group_del(info->poll_group, fd);
	close(fd);
	lua_pushnil(ls);
	lua_setmetatable(ls, -2);

	luaL_getsubtable(ls, LUA_REGISTRYINDEX, REG_FD_MAP);
	lua_pushnil(ls);
	lua_seti(ls, -2, fd);

	return 0;
}

static int l_timer_handler(struct  lua_State *ls)
{
	int nargs;
	struct libevdev *dev;
	dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_TIMER);
	(void) dev;
	nargs = lua_gettop(ls);

	lua_getuservalue(ls, 1);

	if (nargs > 1) {
		luaL_checktype(ls, 2, LUA_TFUNCTION);
		lua_pushvalue(ls, 2);
		lua_setuservalue(ls, 1);
	}
	return 1;
}

static int l_timer_set(struct lua_State *ls)
{
	struct itimerspec ts = { 0 };
	int arg_base = 2;
	int flags = 0;
	int fd = *(int *)luaL_checkudata(ls, 1, REG_NAME_TIMER);
	if (lua_isboolean(ls, arg_base))
		flags = lua_toboolean(ls, arg_base++) ? TFD_TIMER_ABSTIME : 0;

	if (lua_isinteger(ls, arg_base)) {
		ts.it_value.tv_sec = luaL_checkinteger(ls, arg_base);
		ts.it_value.tv_nsec = luaL_optinteger(ls, arg_base + 1, 0);
	} else {
		double time = luaL_checknumber(ls, arg_base);
		ts.it_value.tv_sec = (time_t)time;
		ts.it_value.tv_nsec = (time_t)((time - (time_t)time) * 1000 * 1000 * 1000);
	}
	if (timerfd_settime(fd, flags, &ts, NULL) < 0)
		return luaL_error(ls, "cannot set timer: %s", strerror(errno));
	return 0;
}

static int l_timer_get(struct lua_State *ls)
{
	struct itimerspec ts;
	int fd = *(int *)luaL_checkudata(ls, 1, REG_NAME_TIMER);
	if (timerfd_gettime(fd, &ts) < 0)
		return luaL_error(ls, "cannot get timer: %s", strerror(errno));
	lua_pushinteger(ls, ts.it_value.tv_sec);
	lua_pushinteger(ls, ts.it_value.tv_nsec);
	return 2;
}

static int l_timer_cancel(struct lua_State *ls)
{
	struct itimerspec ts = { 0 };
	int fd = *(int *)luaL_checkudata(ls, 1, REG_NAME_TIMER);
	if (timerfd_settime(fd, 0, &ts, NULL) < 0)
		return luaL_error(ls, "cannot cancel timer: %s", strerror(errno));
	return 0;
}

static inline int check_devname(const char *path)
{
	if (*path == '.')
		return EINVAL;
	while (1) {
		switch (*path++) {
		case '/':
			return EINVAL;
		case 0:
			return 0;
		}
	}
}

static int l_evdev_open(struct lua_State *ls)
{
	int rc;
	int fd;
	int nargs;
	struct stat statbuf = { 0 };
	struct libevdev *dev = NULL;
	const char *devname;
	struct lua_device_info_t *info = *(struct lua_device_info_t **)lua_getextraspace(ls);


	nargs = lua_gettop(ls);
	devname = luaL_checkstring(ls, 1);
	if (nargs > 1)
		luaL_checktype(ls, 2, LUA_TFUNCTION);
	rc = check_devname(devname);
	if (rc != 0) {
		return luaL_error(ls, "invalid device path %s", devname);
	}

	// fd = openat2(info->dev_dir_fd, devname, O_RDONLY | O_NONBLOCK, 0, RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV);
	fd = openat(info->dev_dir_fd, devname, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
	if (fd < 0) {
		return luaL_error(ls, "cannot open device %s: %s", devname, strerror(errno));
	}
	rc = fstat(fd, &statbuf);
	if (rc < 0 || (statbuf.st_mode & S_IFCHR) == 0) {
		close(fd);
		return luaL_error(ls, "cannot open device %s: %x", devname, statbuf.st_mode);
	}
	rc = libevdev_new_from_fd(fd, &dev);
	if (rc < 0) {
		close(fd);
		return luaL_error(ls, "cannot create device: %d", rc);
	}

	luaL_getsubtable(ls, LUA_REGISTRYINDEX, REG_FD_MAP);
	L_NEW_OBJECT(dev, REG_NAME_EVDEV, libevdev_free(dev), close(fd));

	if (nargs > 1) {
		// user value
		lua_pushvalue(ls, 2);
		lua_setuservalue(ls, -2);
	}

	// put in FD_MAP
	lua_pushvalue(ls, -1);
	lua_seti(ls, -3, fd);

	return 1;
}

static int l_evdev_close(struct lua_State *ls)
{
	int fd;
	struct libevdev *dev;
	dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_EVDEV);
	fd = libevdev_get_fd(dev);
	libevdev_free(dev);
	if (fd >= 0)
		close(fd);

	lua_pushnil(ls);
	lua_setmetatable(ls, -2);

	luaL_getsubtable(ls, LUA_REGISTRYINDEX, REG_FD_MAP);
	lua_pushnil(ls);
	lua_seti(ls, -2, fd);

	return 0;
}

static int l_evdev_info(struct lua_State *ls)
{
	struct libevdev *dev;
	dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_EVDEV);
	lua_newtable(ls);

	lua_pushstring(ls, libevdev_get_name(dev));
	lua_setfield(ls, -2, "name");

	lua_pushstring(ls, libevdev_get_phys(dev));
	lua_setfield(ls, -2, "phys");

	lua_pushstring(ls, libevdev_get_uniq(dev));
	lua_setfield(ls, -2, "uniq");

	lua_pushinteger(ls, libevdev_get_id_product(dev));
	lua_setfield(ls, -2, "product");

	lua_pushinteger(ls, libevdev_get_id_vendor(dev));
	lua_setfield(ls, -2, "vendor");

	lua_pushinteger(ls, libevdev_get_id_bustype(dev));
	lua_setfield(ls, -2, "bustype");

	lua_pushinteger(ls, libevdev_get_id_version(dev));
	lua_setfield(ls, -2, "version");

	lua_pushinteger(ls, libevdev_get_driver_version(dev));
	lua_setfield(ls, -2, "driver_version");

	// properties
	lua_newtable(ls);
	for (int i = 0; i < INPUT_PROP_MAX; i++) {
#if 0
		static const char *prop_name[INPUT_PROP_MAX] = {
			"POINTER", "DIRECT", "BUTTONPAD", "SEMI_MT", "TOPBUTTONPAD", "POINTING_STICK", "ACCELEROMETER"
		};
#endif
		if (libevdev_has_property(dev, i)) {
			const char *name = libevdev_property_get_name(i);
			if (name) {
				lua_pushboolean(ls, 1);
				lua_setfield(ls, -2, name);
			}
		}
	}
	lua_setfield(ls, -2, "properties");

	// events
	lua_newtable(ls);
	for (int i = 0; i < EV_MAX; i++) {
#if 0
		static const char *event_name[EV_MAX] = {
			"SVN", "KEY", "REL", "ABS", "MSC", "SW", "LED", "SND", "REP", "FF", "PWR", "FF_STATUS"
		};
#endif
		if (libevdev_has_event_type(dev, i)) {
			const char *name = libevdev_event_type_get_name(i);
			if (name) {
				lua_pushboolean(ls, 1);
				lua_setfield(ls, -2, name);
			}
		}
	}
	lua_setfield(ls, -2, "events");

	// abs info
	if (libevdev_has_event_type(dev, EV_ABS)) {
		int abs_max = libevdev_event_type_get_max(EV_ABS);
		lua_newtable(ls);
		for (int i = 0; i < abs_max; i++) {
#if 0
			static const char *abs_name[ABS_MAX] = {
				/*0x00*/"X", "Y", "Z", "RX", "RY", "RZ", "THROTTLE", "RUDDER",
					"WHEEL", "GAS", "BRAKE", NULL, NULL, NULL, NULL, NULL,
				/*0x10*/"HAT0X", "HAT0Y", "HAT1X", "HAT1Y", "HAT2X", "HAT2Y",
					"HAT3X", "HAT3Y", "PRESSURE", "DISTANCE", "TILT_X",
					"TILT_Y", "TOOL_WIDTH", NULL, NULL, NULL,
				/*0x20*/"VOLUME", "PROFILE", NULL, NULL, NULL, NULL, NULL, NULL,
					"MISC", NULL, NULL, NULL, NULL, NULL, "RESERVED", "MT_SLOT",
				/*0x30*/"MT_TOUCH_MAJOR", "MT_TOUCH_MINOR", "MT_WIDTH_MAJOR",
					"MT_WIDTH_MINOR", "MT_ORIENTATION", "MT_POSITION_X",
					"MT_POSITION_Y", "MT_TOOL_TYPE", "MT_BLOB_ID", "MT_TRACKING_ID",
					"MT_PRESSURE", "MT_DISTANCE", "MT_TOOL_X", "MT_TOOL_Y"
			};
#endif
			const struct input_absinfo *abs_info;
			const char *name;
			abs_info = libevdev_get_abs_info(dev, i);
			if (!abs_info)
				continue;
			name = libevdev_event_code_get_name(EV_ABS, i);
			if (!name)
				continue;

			lua_newtable(ls);

			lua_pushinteger(ls, abs_info->value);
			lua_setfield(ls, -2, "value");

			lua_pushinteger(ls, abs_info->minimum);
			lua_setfield(ls, -2, "minimum");

			lua_pushinteger(ls, abs_info->maximum);
			lua_setfield(ls, -2, "maximum");

			lua_pushinteger(ls, abs_info->fuzz);
			lua_setfield(ls, -2, "fuzz");

			lua_pushinteger(ls, abs_info->flat);
			lua_setfield(ls, -2, "flat");

			lua_pushinteger(ls, abs_info->resolution);
			lua_setfield(ls, -2, "resolution");

			lua_setfield(ls, -2, name);
		}
		lua_setfield(ls, -2, "abs_info");
	}

	return 1;
}

static int l_evdev_handler(struct lua_State *ls)
{
	int nargs;
	struct libevdev *dev;
	dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_EVDEV);
	(void) dev;
	nargs = lua_gettop(ls);

	lua_getuservalue(ls, 1);

	if (nargs > 1) {
		luaL_checktype(ls, 2, LUA_TFUNCTION);
		lua_pushvalue(ls, 2);
		lua_setuservalue(ls, 1);
	}
	return 1;
}

static int l_evdev_monitor(struct lua_State *ls)
{
	int rc;
	int fd;
	int monitor;
	struct libevdev *dev;
	struct lua_device_info_t *info = *(struct lua_device_info_t **)lua_getextraspace(ls);
	dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_EVDEV);
	fd = libevdev_get_fd(dev);
	if (fd < 0)
		return luaL_error(ls, "cannot monitor device: %d", fd);

	luaL_checktype(ls, 2, LUA_TBOOLEAN);
	monitor = lua_toboolean(ls, 2);

	if (monitor)
		rc = poll_group_add(info->poll_group, fd);
	else
		rc = poll_group_del(info->poll_group, fd);
	if (rc != 0)
		return luaL_error(ls, "cannot monitor device: %d", rc);

	return 0;
}

static int l_evdev_grab(struct lua_State *ls)
{
	int rc;
	int grab;
	struct libevdev *dev;
	dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_EVDEV);
	luaL_checktype(ls, 2, LUA_TBOOLEAN);
	grab = lua_toboolean(ls, 2);
	rc = libevdev_grab(dev, grab ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB);
	if (rc != 0)
		return luaL_error(ls, "cannot grab device: %d", rc);
	return 0;
}

static int l_evdev_read(struct lua_State *ls)
{
	int rc;
	int count = 0;
	int flag = LIBEVDEV_READ_FLAG_NORMAL;
	struct libevdev *dev;
	dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_EVDEV);
	lua_newtable(ls);
	do {
		struct input_event ev;
		rc = libevdev_next_event(dev, flag, &ev);
		if (rc < 0) {
			if (rc == -EAGAIN && flag == LIBEVDEV_READ_STATUS_SYNC) {
				flag = LIBEVDEV_READ_FLAG_NORMAL;
				continue;
			}
			if (rc == -EINTR)
				continue;
			break;
		}
		if (rc == LIBEVDEV_READ_STATUS_SYNC && rc != flag) {
			flag = LIBEVDEV_READ_FLAG_SYNC;
			continue;
		}

		lua_createtable(ls, 0, 3);
		lua_pushinteger(ls, ev.type);
		lua_setfield(ls, -2, "type");
		lua_pushinteger(ls, ev.code);
		lua_setfield(ls, -2, "code");
		lua_pushinteger(ls, ev.value);
		lua_setfield(ls, -2, "value");
		lua_seti(ls, -2, ++count);
	} while (1);
	if (rc != -EAGAIN && count == 0)
		return luaL_error(ls, "cannot read device: %d", rc);
	return 1;
}

#define EVDEV_SET_STR(ls, dev, key)		\
do { 						\
	const char *str;			\
	lua_getfield((ls), -1, #key);		\
	str = lua_tostring((ls), -1);		\
	if (str)				\
		libevdev_set_##key((dev), str);	\
	lua_pop((ls), 1);			\
} while (0)

#define EVDEV_SET_INT(ls, dev, key) 			\
do { 							\
	int val, rc;					\
	lua_getfield((ls), -1, #key);			\
	val = lua_tointegerx((ls), -1, &rc);		\
	if (rc)						\
		libevdev_set_id_##key((dev), val);	\
	lua_pop((ls), 1);				\
} while (0)

#define ABS_INFO_GET(ls, abs_info, key)		\
do {						\
	int val, rc;				\
	lua_getfield((ls), -1, #key);		\
	val = lua_tointegerx((ls), -1, &rc);	\
	if (rc)					\
		(abs_info)->key = val;	\
	lua_pop((ls), 1);			\
} while (0)


static int get_abs_info_from_table(struct lua_State *ls, int table_index, int code, struct input_absinfo *abs_info)
{
	int rc = 0;
	lua_getfield(ls, table_index, "abs_info");
	if (lua_type(ls, -1) == LUA_TTABLE) {
		const char *key = libevdev_event_code_get_name(EV_ABS, code);
		if (key) {
			lua_getfield(ls, -1, key);
			if (lua_type(ls, -1) == LUA_TTABLE) {
				ABS_INFO_GET(ls, abs_info, value);
				ABS_INFO_GET(ls, abs_info, minimum);
				ABS_INFO_GET(ls, abs_info, maximum);
				ABS_INFO_GET(ls, abs_info, fuzz);
				ABS_INFO_GET(ls, abs_info, flat);
				ABS_INFO_GET(ls, abs_info, resolution);
				rc = 1;
			}
			lua_pop(ls, 1);
		}
	}
	lua_pop(ls, 1);
	return rc;
}

static void build_evdev_from_table(struct lua_State *ls, struct libevdev *dev)
{
	int table_index = lua_gettop(ls);
	EVDEV_SET_STR(ls, dev, name);
	EVDEV_SET_STR(ls, dev, phys);
	EVDEV_SET_STR(ls, dev, uniq);
	EVDEV_SET_INT(ls, dev, product);
	EVDEV_SET_INT(ls, dev, vendor);
	EVDEV_SET_INT(ls, dev, bustype);
	EVDEV_SET_INT(ls, dev, version);

	lua_getfield(ls, -1, "properties");
	if (lua_istable(ls, -1)) {
		lua_pushnil(ls);
		while (lua_next(ls, -2)) {
			if (lua_type(ls, -2) == LUA_TSTRING && lua_toboolean(ls, -1)) {
				int prop;
				const char *key = lua_tostring(ls, -2);
				prop = libevdev_property_from_name(key);
				if (prop >= 0)
					libevdev_enable_property(dev, prop);
			}
			lua_pop(ls, 1);
		}
	}
	lua_pop(ls, 1);


	lua_getfield(ls, -1, "events");
	if (lua_istable(ls, -1)) {
		lua_pushnil(ls);
		while (lua_next(ls, -2)) {
			if (lua_type(ls, -2) == LUA_TSTRING && lua_toboolean(ls, -1)) {
				int type;
				const char *key = lua_tostring(ls, -2);
				type = libevdev_event_type_from_name(key);
				if (type >= 0) {
					int max = libevdev_event_type_get_max(type);
					// libevdev_enable_event_type(dev, type);
					for (int i = 0; i < max; i++) {
						struct input_absinfo abs_info = { 0 };
						if (type == EV_ABS && get_abs_info_from_table(ls, table_index, i, &abs_info))
							libevdev_enable_event_code(dev, type, i, &abs_info);
						else
							libevdev_enable_event_code(dev, type, i, NULL);
					}
				}
			}
			lua_pop(ls, 1);
		}
	}
	lua_pop(ls, 1);
}

static int l_uinput_create(struct lua_State *ls)
{
	int rc, needs_free_dev;
	struct libevdev *dev;
 	struct libevdev_uinput *uinput_dev;

	if (lua_type(ls, 1) == LUA_TUSERDATA) {
		dev = *(struct libevdev **)luaL_checkudata(ls, 1, REG_NAME_EVDEV);
		needs_free_dev = 0;
	} else {
		luaL_checktype(ls, 1, LUA_TTABLE);
		dev = libevdev_new();
		needs_free_dev = 1;

		// load from table
		build_evdev_from_table(ls, dev);
	}

	rc = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput_dev);
	if (needs_free_dev)
		libevdev_free(dev);

	if (rc < 0)
		return luaL_error(ls, "cannot create device: %d", rc);

	L_NEW_OBJECT(uinput_dev, REG_NAME_UINPUT, libevdev_uinput_destroy(uinput_dev));
	// lua_pushlightuserdata(ls, uinput_dev);
	// luaL_setmetatable(ls, REG_NAME_UINPUT);

	return 1;
}

static int l_uinput_close(struct lua_State *ls)
{
	struct libevdev_uinput *dev;
	dev = *(struct libevdev_uinput **)luaL_checkudata(ls, 1, REG_NAME_UINPUT);
	libevdev_uinput_destroy(dev);

	lua_pushnil(ls);
	lua_setmetatable(ls, -2);
	return 0;
}

static int l_uinput_name(struct lua_State *ls)
{
	struct libevdev_uinput *dev;
	const char *node_name;
	const char *ptr = NULL;
	dev = *(struct libevdev_uinput **)luaL_checkudata(ls, 1, REG_NAME_UINPUT);
	node_name = libevdev_uinput_get_devnode(dev);
	if (node_name)
		ptr = strrchr(node_name, '/');
	lua_pushstring(ls, ptr ? ptr + 1 : node_name);
	return 1;
}

static int l_uinput_write(struct lua_State *ls)
{
	int rc;
	int len;
	struct libevdev_uinput *dev;

	dev = *(struct libevdev_uinput **)luaL_checkudata(ls, 1, REG_NAME_UINPUT);
	luaL_checktype(ls, 2, LUA_TTABLE);
	len = luaL_len(ls, 2);
	for (int i = 1; i <= len; ++i) {
		struct input_event ev;
		int elem;
		rc = lua_geti(ls, 2, i);
		elem = lua_gettop(ls);
		luaL_checktype(ls, elem, LUA_TTABLE);
		lua_getfield(ls, elem, "type");
		ev.type = luaL_checkinteger(ls, -1);
		lua_getfield(ls, elem, "code");
		ev.code = luaL_checkinteger(ls, -1);
		lua_getfield(ls, elem, "value");
		ev.value = luaL_checkinteger(ls, -1);
		lua_pop(ls, 4);

		rc = libevdev_uinput_write_event(dev, ev.type, ev.code, ev.value);
		if (rc != 0)
			return luaL_error(ls, "cannot write device: %d", rc);
	}
	return 0;
}

static int l_event_type_name(struct lua_State *ls)
{
	int type;
	type = luaL_checkinteger(ls, 1);
	lua_pushstring(ls, libevdev_event_type_get_name(type));
	return 1;
}

static int l_event_code_name(struct lua_State *ls)
{
	int type, code;
	type = luaL_checkinteger(ls, 1);
	code = luaL_checkinteger(ls, 2);
	lua_pushstring(ls, libevdev_event_code_get_name(type, code));
	return 1;
}

static int l_event_value_name(struct lua_State *ls)
{
	int type, code, value;
	type = luaL_checkinteger(ls, 1);
	code = luaL_checkinteger(ls, 2);
	value = luaL_checkinteger(ls, 3);
	lua_pushstring(ls, libevdev_event_value_get_name(type, code, value));
	return 1;
}

static int l_event_type_num(struct lua_State *ls)
{
	int type;
	const char *name = luaL_checkstring(ls, 1);
	type = libevdev_event_type_from_name(name);
	if (type < 0)
		return 0;
	lua_pushinteger(ls, type);
	return 1;
}

static int l_event_code_num(struct lua_State *ls)
{
	int type, code;
	const char *name = luaL_checkstring(ls, 1);
	type = libevdev_event_type_from_code_name(name);
	code = libevdev_event_code_from_code_name(name);

	if (code < 0)
		return 0;
	lua_pushinteger(ls, code);
	if (type < 0)
		return 1;
	lua_pushinteger(ls, type);
	return 2;
}

static int l_event_value_num(struct lua_State *ls)
{
	int type, code, value, arg_base;
	const char *name;

	if (lua_type(ls, 1) == LUA_TSTRING) {
		const char *code_name = luaL_checkstring(ls, 1);
		type = libevdev_event_type_from_code_name(code_name);
		code = libevdev_event_code_from_code_name(code_name);
		arg_base = 2;
	} else {
		type = luaL_checkinteger(ls, 1);
		code = luaL_checkinteger(ls, 2);
		arg_base = 3;
	}

	name = luaL_checkstring(ls, arg_base);

	if (type < 0 || code < 0)
		return 0;

	value = libevdev_event_value_from_name(type, code, name);
	if (value < 0)
		return 0;
	lua_pushinteger(ls, value);
	return 1;
}

static const struct luaL_Reg sys_table[] = {
	{"meminfo", l_sys_meminfo},
	{"exit", l_sys_exit},
	{"gettime", l_sys_gettime},
	{"timer", l_sys_timer},
	{NULL, NULL}
};

static const struct luaL_Reg timer_methods[] = {
	{"close", l_timer_close},
	{"handler", l_timer_handler},
	{"set", l_timer_set},
	{"get", l_timer_get},
	{"cancel", l_timer_cancel},
	{NULL, NULL}
};

static const struct luaL_Reg device_table[] = {
	{"open", l_evdev_open},
	{"create", l_uinput_create},
	{"type_name", l_event_type_name},
	{"code_name", l_event_code_name},
	{"value_name", l_event_value_name},
	{"type_num", l_event_type_num},
	{"code_num", l_event_code_num},
	{"value_num", l_event_value_num},
	{NULL, NULL}
};

static const struct luaL_Reg evdev_methods[] = {
	{"close", l_evdev_close},
	{"info", l_evdev_info},
	{"handler", l_evdev_handler},
	{"monitor", l_evdev_monitor},
	{"grab", l_evdev_grab},
	{"read", l_evdev_read},
	{NULL, NULL}
};

static const struct luaL_Reg uinput_methods[] = {
	{"close", l_uinput_close},
	{"name", l_uinput_name},
	{"write", l_uinput_write},
	{NULL, NULL}
};

static int l_require(struct lua_State *ls)
{
	int rc;
	int narg;

 	narg = lua_gettop(ls);
	luaL_checkstring(ls, 1);

	rc = lua_device_load(ls, narg);
	if (rc != LUA_OK) {
		// err msg on stack
		return lua_error(ls);
	}
	return lua_gettop(ls);
}

static inline void load_std_libraries(struct lua_State *ls)
{
	luaopen_base(ls);
	// remove 'unsafe' methods
	lua_pushnil(ls);
	lua_setfield(ls, -2, "dofile");
	lua_pushnil(ls);
	lua_setfield(ls, -2, "loadfile");
	lua_pushnil(ls);
	lua_setfield(ls, -2, "load");
	lua_pushnil(ls);
	lua_setfield(ls, -2, "loadstring");
	lua_pop(ls, 1);

	luaopen_coroutine(ls);
	lua_setglobal(ls, "coroutine");
	luaopen_table(ls);
	lua_setglobal(ls, "table");
	luaopen_string(ls);
	lua_setglobal(ls, "string");
	luaopen_utf8(ls);
	lua_setglobal(ls, "utf8");
	luaopen_bit32(ls);
	lua_setglobal(ls, "bit32");
	luaopen_math(ls);
	lua_setglobal(ls, "math");

	// custom 'require' method
	lua_pushcfunction(ls, l_require);
	lua_setglobal(ls, "require");
}

static inline void load_device_libraries(struct lua_State *ls)
{
	// set sys table
	luaL_newlib(ls, sys_table);
	lua_pushvalue(ls, -1);
	lua_setglobal(ls, "sys");
	lua_pop(ls, 1);

	// set timer methods
	luaL_newmetatable(ls, REG_NAME_TIMER);
	luaL_newlib(ls, timer_methods);
	lua_setfield(ls, -2, "__index");
	lua_pushcfunction(ls, l_timer_close);
	lua_setfield(ls, -2, "__gc");

	lua_pushboolean(ls, 1);
	lua_setfield(ls, -2, "__metatable");
	lua_pop(ls, 1);

	// set device table
	luaL_newlib(ls, device_table);
	lua_setglobal(ls, "device");

	// set evdev methods
	luaL_newmetatable(ls, REG_NAME_EVDEV);
	luaL_newlib(ls, evdev_methods);
	lua_setfield(ls, -2, "__index");
	lua_pushcfunction(ls, l_evdev_close);
	lua_setfield(ls, -2, "__gc");

	lua_pushboolean(ls, 1);
	lua_setfield(ls, -2, "__metatable");
	lua_pop(ls, 1);

	// set uinput methods
	luaL_newmetatable(ls, REG_NAME_UINPUT);
	luaL_newlib(ls, uinput_methods);
	lua_setfield(ls, -2, "__index");
	lua_pushcfunction(ls, l_uinput_close);
	lua_setfield(ls, -2, "__gc");

	lua_pushboolean(ls, 1);
	lua_setfield(ls, -2, "__metatable");
	lua_pop(ls, 1);
}

static int l_load_libraries(lua_State *ls) {
	load_std_libraries(ls);
	load_device_libraries(ls);
	return 0;
}

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	struct lua_device_info_t *info = (struct lua_device_info_t *)ud;

	if (nsize == 0) {
		info->mem_usage -= osize;
		free(ptr);
		return NULL;
	}
	if (ptr && osize >= nsize) {
		// shrink, assume realloc never fails
		info->mem_usage -= (osize - nsize);
		return realloc(ptr, nsize);
	}
	if (ptr == NULL) {
		// osize encodes object type, which we do not need
		// discard and set to 0, to ease calculation below
		osize = 0;
	}

	if (info->mem_limit && info->mem_usage + (nsize - osize) > info->mem_limit) {
		// used memory exceeds limit, return NOMEM
		return NULL;
	} else {
		void *block = realloc(ptr, nsize);
		if (block)
			info->mem_usage += (nsize - osize);
		return block;
	}
}

struct lua_State *lua_device_create(struct lua_device_info_t *info)
{
	int rc;
	struct lua_State *ls = lua_newstate(l_alloc, info);
	if (ls == NULL)
		return NULL;
	// save poll_group pointer
	*(struct lua_device_info_t **)lua_getextraspace(ls) = info;

	lua_pushcfunction(ls, l_load_libraries);
	rc = lua_pcall(ls, 0, 0, 0);
	if (rc != LUA_OK) {
		fprintf(stderr, "failed to create lua_State %d\n", rc);
		lua_close(ls);
		return NULL;
	}

	return ls;
}
void lua_device_destroy(struct lua_State *ls)
{
	lua_close(ls);
}

int lua_device_start(struct lua_State *ls, const char *main_name, char **args)
{
	int rc;
	int i = 0;

	lua_pushstring(ls, main_name);
	lua_pushglobaltable(ls);
	luaL_getsubtable(ls, -1, "sys");
	lua_pushvalue(ls, -3);
	lua_setfield(ls, -2, "main");
	lua_pop(ls, 2);

	if (args) {
		for (i = 0; args[i]; i++) {
			luaL_checkstack(ls, 1, NULL);
			lua_pushstring(ls, args[i]);
		}
	}

	rc = lua_device_load(ls, i + 1);
	if (rc != LUA_OK) {
		fprintf(stderr, "%s\n", lua_tostring(ls, -1));
		lua_pop(ls, 1);
	}
	return rc;
}

static inline void alarm_op(struct lua_State *ls, int startstop)
{
	struct lua_device_info_t *info = *(struct lua_device_info_t **)lua_getextraspace(ls);
	if (info->time_limit > 0) {
		struct itimerspec ts = { 0 };
		int rc;

		if (startstop) {
			if (info->timer_ref++)
				return;
			ts.it_value.tv_sec = info->time_limit / 1000;
			ts.it_value.tv_nsec = (info->time_limit % 1000) * 1000 * 1000;
		} else {
			if (info->timer_ref && --info->timer_ref)
				return;
		}
		rc = timer_settime(info->timer_id, 0, &ts, NULL);
		if (rc != 0)
			fprintf(stderr, "cannot set alarm %d\n", errno);
	}
}
#define alarm_start(ls) alarm_op(ls, 1)
#define alarm_stop(ls) alarm_op(ls, 0)

static int lua_err_handler(struct lua_State *ls)
{
	const char *msg = lua_tostring(ls, 1);
	luaL_traceback(ls, ls, msg, 1);
	return 1;
}

static int lua_do_call(struct lua_State *ls, int narg, int nres)
{
	int rc;
	int base = lua_gettop(ls) - narg;
	lua_pushcfunction(ls, lua_err_handler);
	lua_insert(ls, base);
	alarm_start(ls);
	rc = lua_pcall(ls, narg, nres, base);
	alarm_stop(ls);
	if (rc != LUA_OK) {
		const char *msg = lua_tostring(ls, -1);
		fprintf(stderr, "%s\n", msg);
		// keeps err msg on stack
	}
	lua_remove(ls, base);
	return rc;
}

static int lua_device_load(struct lua_State *ls, int narg)
{
	int rc;
	int base;
	const char *filename;

	base = lua_gettop(ls) - narg + 1;
	filename = lua_tostring(ls, base);

	if (NULL != strstr(filename, "..")) {
		lua_pushfstring(ls, "invalid path %s", filename);
		return LUA_ERRRUN;
	}

	if (filename[0] == '/') {
		rc = luaL_loadfilex(ls, filename, "t");
	} else {
		luaL_gsub(ls, filename, ".", "/");
		lua_pushliteral(ls, ".lua");
		lua_concat(ls, 2);
		rc = luaL_loadfilex(ls, lua_tostring(ls, -1), "t");
		lua_replace(ls, -2);
	}
	if (rc != LUA_OK) {
		// err msg already on stack
		return rc;
	}
	// move chunk before args
	lua_insert(ls, base);

	// fprintf(stderr, "loading %s\n", modname);
	rc = lua_do_call(ls, narg, LUA_MULTRET);

	if (rc != LUA_OK) {
		// err msg already on stack
		return rc;
	}

	return rc;
}

int lua_device_event(struct lua_State *ls, int op, const char *dev_name)
{
	int rc;
	int top = lua_gettop(ls);
	luaL_checkstack(ls, 4, NULL);

	lua_pushvalue(ls, -1);
	if (op)
		lua_pushliteral(ls, "add");
	else
		lua_pushliteral(ls, "del");
	lua_pushstring(ls, dev_name);
	rc = lua_do_call(ls, 2, 0);
	lua_settop(ls, top);
	return rc;
}

int lua_device_handle_fd(struct lua_State *ls, int fd)
{
	int rc = 0;
	int top = lua_gettop(ls);
	luaL_checkstack(ls, 4, NULL);

	luaL_getsubtable(ls, LUA_REGISTRYINDEX, REG_FD_MAP);
	lua_geti(ls, -1, fd);
	if (!lua_isuserdata(ls, -1)) {
		lua_settop(ls, top);
		return LUA_OK;
	}
	if (LUA_TFUNCTION == lua_getuservalue(ls, -1)) {
		lua_pushvalue(ls, -2);
		rc = lua_do_call(ls, 1, 0);
	}
	lua_settop(ls, top);
	return rc;
}
