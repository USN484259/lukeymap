#define _GNU_SOURCE
#include "poll_group.h"
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#define CAPACITY_INC_STEP 4

int poll_group_init(struct poll_group_t *group)
{
	group->capacity = CAPACITY_INC_STEP;
	group->size = 0;
	group->index = 0;
	group->poll_fd = (struct pollfd *)malloc(sizeof(struct pollfd) * group->capacity);
	if (group->poll_fd == NULL)
		return -ENOMEM;
	return 0;
}

void poll_group_cleanup(struct poll_group_t *group)
{
	free(group->poll_fd);
	memset(group, 0, sizeof(struct poll_group_t));
}

static unsigned poll_group_find(struct poll_group_t *group, int fd)
{
	unsigned i;
	for (i = 0; i < group->size; i++) {
		if (group->poll_fd[i].fd == fd)
			break;
	}
	return i;
}

int poll_group_add(struct poll_group_t *group, int fd)
{
	if (poll_group_find(group, fd) < group->size)
		return -EEXIST;

	if (group->size == group->capacity) {
		unsigned new_cap = group->capacity + CAPACITY_INC_STEP;
		void *p = realloc(group->poll_fd, sizeof(struct pollfd) * new_cap);
		if (p == NULL)
			return -ENOMEM;
		group->poll_fd = (struct pollfd *)p;
		group->capacity = new_cap;
	}
	group->poll_fd[group->size].fd = fd;
	group->poll_fd[group->size].events = POLLIN;
	group->poll_fd[group->size].revents = 0;
	group->size++;
	group->index = group->size;
	return 0;
}

int poll_group_del(struct poll_group_t *group, int fd)
{
	unsigned index = poll_group_find(group, fd);
	if (index >= group->size)
		return -EINVAL;
	group->poll_fd[index] = group->poll_fd[--group->size];
	group->index = group->size;
	return 0;
}

int poll_group_next(struct poll_group_t *group, int *fd)
{
	int rc = 0;
	do {
		for (; group->index < group->size; ++group->index) {
			if (group->poll_fd[group->index].revents & POLLIN) {
				*fd = group->poll_fd[group->index].fd;
				group->poll_fd[group->index].revents = 0;
				return 0;
			}
		}
		rc = poll(group->poll_fd, group->size, -1);
		if (rc < 0)
			break;
		group->index = 0;
	} while (1);
	return errno;
}