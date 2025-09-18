#pragma once

struct pollfd;
struct input_event;
struct libevdev;
struct libevdev_uinput;


struct poll_group_t {
	unsigned size;
	unsigned capacity;
	unsigned index;
	struct pollfd *poll_fd;
};

int poll_group_init(struct poll_group_t *group);
void poll_group_cleanup(struct poll_group_t *group);
int poll_group_add(struct poll_group_t *group, int fd);
int poll_group_del(struct poll_group_t *group, int fd);
int poll_group_next(struct poll_group_t *group, int *fd);
