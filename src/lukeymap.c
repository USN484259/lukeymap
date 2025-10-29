#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>
#include <argp.h>

#include "poll_group.h"
#include "monitor.h"
#include "lua_device.h"

#define DEV_INPUT_PATH "/dev/input/"

static const char * const doc = "Lua scripted key remapping program";
static const char * const args_doc = "FILE [ param ... ]";

struct argp_info_t {
	char *dir;
	char *file;
	size_t memory;
	int nice;
	int mlock;
	unsigned time;
	char **parameters;
};

static const struct argp_option options[] = {
	{"dir", 'd', "DIR", 0, "set scripts dir"},
	{"nice", 'n', "NICE", 0, "set nice value"},
	{"memory", 'm', "MEMORY", 0, "pre-allocate memory for Lua runtime"},
	{"lock", 'l', 0, 0, "lock memory using mlockall(2)"},
	{"time", 't', "TIME", 0, "time limit in ms for each script call"},
	{ 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct argp_info_t *info = state->input;
	switch (key) {
	case 'd':
		info->dir = arg;
		break;
	case 'n':
	{
		char *endptr;
		long value = strtol(arg, &endptr, 0);
		if (*endptr != 0)
			argp_error(state, "invalid nice value %s", arg);
		info->nice = (int)value;
		break;
	}
	case 'm':
	{
		char *endptr;
		unsigned long long value = strtoull(arg, &endptr, 0);
		if (endptr == arg)
			argp_error(state, "invalid memory value %s", arg);
		switch (*endptr) {
		case 'G':
		case 'g':
			value <<= 10;
			// fallthrough
		case 'M':
		case 'm':
			value <<= 10;
			// fallthrough
		case 'K':
		case 'k':
			value <<= 10;
			// fallthrough
		case 'B':
		case 'b':
		case 0:
			break;
		default:
			argp_error(state, "invalid memory value %s", arg);
		}
		info->memory = (size_t)value;
		break;
	}
	case 'l':
		info->mlock = 1;
		break;
	case 't':
	{
		char *endptr;
		unsigned long value = strtoul(arg, &endptr, 0);
		if (*endptr != 0)
			argp_error(state, "invalid time value %s", arg);
		info->time = (unsigned)value;
		break;
	}
	case ARGP_KEY_ARG:
		info->file = arg;
		info->parameters = &state->argv[state->next];
		state->next = state->argc;
		break;
	case ARGP_KEY_END:
		if (state->arg_num < 1)
			argp_usage (state);

		if (info->mlock && info->memory == 0)
			argp_error(state, "mlock requires memory limit set");
#if 0
		if (info->dir == NULL) {
			char *sep = strrchr(info->file, '/');
			if (sep) {
				*sep = 0;
				info->dir = info->file;
				info->file = sep + 1;
			} else {
				info->dir = ".";
			}
		}
#endif
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static const struct argp argp = {options, parse_opt, args_doc, doc};

static volatile sig_atomic_t quit = 0;

static void sig_quit(int signum)
{
	switch (signum) {
	case SIGINT:
		if (0 == quit++)
			return;
		break;
	case SIGTERM:
		break;
	case SIGALRM:
		fprintf(stderr, "script running too long");
		break;
	}

	exit(signum);
}


static inline void set_signal(void)
{
	struct sigaction action = {
		.sa_handler = sig_quit,
	};

	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGALRM, &action, NULL);
}

static inline int walk_devices(struct lua_State *ls)
{
	int rc = 0;
	DIR *dir = opendir(DEV_INPUT_PATH);
	if (dir == NULL)
		return errno;
	while (!quit) {
		errno = 0;
		struct dirent *entry = readdir(dir);
		if (entry == NULL) {
			rc = errno;
			break;
		}
		if (entry->d_type == DT_CHR)
			lua_device_event(ls, 1, entry->d_name);
	}
	closedir(dir);
	return rc;
}

int main(int argc, char **argv)
{
	int rc;
	int monitor_fd = -1;
	struct lua_State *ls = NULL;
	struct poll_group_t poll_group;
	struct device_monitor_t monitor;
	struct lua_device_info_t lua_info = {
		.poll_group = &poll_group,
		.dev_dir_fd = -1,
		.load_dir_fd = -1,
	};
	struct argp_info_t argp_info = {
		.time = 1000,
	};

	rc = argp_parse(&argp, argc, argv, 0, NULL, &argp_info);
	if (rc != 0)
		return rc;

	rc = poll_group_init(&poll_group);
	if (rc != 0)
		return rc;

	rc = open(DEV_INPUT_PATH, O_DIRECTORY | O_PATH);
	if (rc < 0) {
		rc = errno;
		goto end;
	} else {
		lua_info.dev_dir_fd = rc;
	}
	if (argp_info.dir) {
		rc = openat(AT_FDCWD, argp_info.dir, O_DIRECTORY | O_PATH);
		if (rc < 0) {
			rc = errno;
			goto end;
		} else {
			lua_info.load_dir_fd = rc;
		}
	}
	rc = device_monitor_init(&monitor, DEV_INPUT_PATH);
	if (rc != 0)
		goto end;
	monitor_fd = device_monitor_get_fd(&monitor);
	rc = poll_group_add(&poll_group, monitor_fd);
	if (rc != 0)
		goto end;

	if (argp_info.mlock) {
		rc = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (rc != 0) {
			rc = errno;
			goto end;
		}
	}

	lua_info.mem_limit = argp_info.memory;
	if (argp_info.time) {
		rc = timer_create(CLOCK_MONOTONIC, NULL, &lua_info.timer_id);
		if (rc != 0) {
			rc = errno;
			goto end;
		}
		lua_info.time_limit = argp_info.time;
	}

	if (argp_info.nice < 0) {
		errno = 0;
		rc = nice(argp_info.nice);
		if (rc == -1 && errno != 0)
			fprintf(stderr, "cannot set nice, continue anyway: %s", strerror(errno));
	}

	set_signal();
	ls = lua_device_create(&lua_info);
	if (ls == NULL)
		goto end;

	rc = lua_device_set_args(ls, argp_info.file, argp_info.parameters);
	// rc is argc for script
	rc = lua_device_load(ls, rc);
	if (rc != 0) {
		goto end;
	}

	rc = walk_devices(ls);
	if (rc != 0)
		goto end;

	while (!quit) {
		int fd;
		rc = poll_group_next(&poll_group, &fd);
		if (rc == EINTR || rc == EAGAIN)
			continue;
		if (rc != 0)
			goto end;
		if (fd == monitor_fd) {
			int event;
			const char *name;
			do {
				rc = device_monitor_next(&monitor, &event, &name);
				if (rc == EINTR)
					continue;
				if (rc == EAGAIN)
					break;
				if (rc != 0)
					goto end;

				switch (event) {
				case DEVICE_MONITOR_EVENT_ADD:
				{
					struct stat statbuf;
					rc = fstatat(lua_info.dev_dir_fd, name, &statbuf, AT_SYMLINK_NOFOLLOW);
					if (rc != 0)
						break;
					if ((statbuf.st_mode & S_IFMT) != S_IFCHR)
						break;
				}
				// fallthrough
				case DEVICE_MONITOR_EVENT_DEL:
					rc = lua_device_event(ls, (event == DEVICE_MONITOR_EVENT_ADD), name);
					break;
				}
			} while (1);
		} else {
			rc = lua_device_handle_fd(ls, fd);
		}
	}

	rc = 0;
end:
	if (ls)
		lua_device_destroy(ls);
	if (monitor_fd >= 0)
		device_monitor_cleanup(&monitor);
	if (lua_info.time_limit > 0)
		timer_delete(lua_info.timer_id);
	if (lua_info.load_dir_fd >= 0)
		close(lua_info.load_dir_fd);
	if (lua_info.dev_dir_fd >= 0)
		close(lua_info.dev_dir_fd);
	poll_group_cleanup(&poll_group);
	return rc;
}
