#include "monitor.h"
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/inotify.h>

#define MONITOR_BUFFER_SIZE 0x1000

int device_monitor_init(struct device_monitor_t *monitor, const char *path)
{
	int rc;
	monitor->fd = inotify_init1(IN_NONBLOCK);
	if (monitor->fd < 0)
		return errno;
	rc = inotify_add_watch(monitor->fd, path, IN_CREATE | IN_DELETE | IN_ONLYDIR);
	if (rc < 0) {
		rc = errno;
		goto err;
	}
	monitor->buffer = malloc(MONITOR_BUFFER_SIZE);
	if (monitor->buffer == NULL) {
		rc = ENOMEM;
		goto err;
	}
	monitor->index = 0;
	monitor->length = 0;
	return 0;
err:
	close(monitor->fd);
	return rc;
}

void device_monitor_cleanup(struct device_monitor_t *monitor)
{
	free(monitor->buffer);
	close(monitor->fd);
}
int device_monitor_get_fd(struct device_monitor_t *monitor)
{
	return monitor->fd;
}
int device_monitor_next(struct device_monitor_t *monitor, int *event, const char **name)
{
	int rc = 0;
	do {
		ssize_t len;
		while (monitor->index < monitor->length) {
			struct inotify_event *ev = (struct inotify_event *)(monitor->buffer + monitor->index);
			if (ev->mask & IN_CREATE) {
				rc = DEVICE_MONITOR_EVENT_ADD;
			}
			else if (ev->mask & IN_DELETE)
				rc = DEVICE_MONITOR_EVENT_DEL;

			monitor->index += sizeof(struct inotify_event) + ev->len;
			if (rc) {
				*event = rc;
				*name = ev->name;
				return 0;
			}
		}
		len = read(monitor->fd, monitor->buffer, MONITOR_BUFFER_SIZE);
		if (len < 0) {
			rc = errno;
			monitor->length = 0;
		} else {
			monitor->length = len;
			monitor->index = 0;
		}
	} while (rc == 0);
	return rc;
}
