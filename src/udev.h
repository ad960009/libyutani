#ifndef UDEV_H_
#define UDEV_H_

#include <wayland-util.h>
#include <libudev.h>
#include "yutani.h"

#define container_of(ptr, type, member) ({                              \
		const __typeof__( ((type *)0)->member ) *__mptr = (ptr);        \
		(type *)( (char *)__mptr - offsetof(type,member) );})

struct udev_context {
	struct wl_list devices_list;
	int udev_fd;
	struct udev_monitor *udev_monitor;
	struct udev *udev;
	struct yt_hotplug_cbs hotplug_cb;
	void *hotplug_data;
	struct wl_event_source *soruce;
};

int evdev_enable_udev_monitor(struct udev_context *master);
void evdev_disable_udev_monitor(struct udev_context *seat);
void evdev_add_devices(struct udev_context *master);
struct evdev_device *device_added(struct udev_device *udev_device, struct udev_context *master);
int evdev_udev_handler(int fd, uint mask, void *data);

#endif /* UDEV_H_ */
