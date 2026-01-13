#pragma once

#include <stddef.h>
#include <time.h>

struct lua_State;
struct poll_group_t;
struct input_event;

struct lua_device_info_t {
	size_t mem_usage;
	size_t mem_limit;
	struct poll_group_t *poll_group;
	int dev_dir_fd;
	timer_t timer_id;
	unsigned time_limit;
	unsigned timer_ref;
};

struct lua_State *lua_device_create(struct lua_device_info_t *info);
void lua_device_destroy(struct lua_State *ls);
int lua_device_start(struct lua_State *ls, const char *main_name, char **args);
int lua_device_event(struct lua_State *ls, int op, const char *dev_name);
int lua_device_handle_fd(struct lua_State *ls, int fd);

