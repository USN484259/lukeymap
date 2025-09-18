#pragma once
#include <stdint.h>

struct device_monitor_t {
	unsigned index;
	unsigned length;
	int fd;
	uint8_t *buffer;
};

enum {
	DEVICE_MONITOR_EVENT_ADD = 1,
	DEVICE_MONITOR_EVENT_DEL = 2,
};

int device_monitor_init(struct device_monitor_t *monitor, const char *path);
void device_monitor_cleanup(struct device_monitor_t *monitor);
int device_monitor_get_fd(struct device_monitor_t *monitor);
int device_monitor_next(struct device_monitor_t *monitor, int *event, const char **name);
