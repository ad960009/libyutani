#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <libudev.h>
#include <string.h>
#include <wayland-util.h>
#include "yutani.h"
#include "evdev.h"
#include "udev.h"
#include "common.h"

int evdev_udev_handler(int fd __UNUSED__, uint mask __UNUSED__, void *data)
{
	struct udev_context *seat = data;
	struct udev_device *udev_device;
	struct evdev_device *device;
	struct yt_device *yt_dev, *next;
	const char *action;
	const char *devnode;

	udev_device = udev_monitor_receive_device(seat->udev_monitor);
	if (!udev_device)
		return 1;

	action = udev_device_get_action(udev_device);
	if (!action)
		goto out;

	if (strncmp("event", udev_device_get_sysname(udev_device), 5) != 0)
		goto out;

	if (!strcmp(action, "add")) {
		device = device_added(udev_device, seat);
		seat->hotplug_cb.add_cb(&device->base, seat->hotplug_data);
	} else if (!strcmp(action, "remove")) {
		devnode = udev_device_get_devnode(udev_device);

		wl_list_for_each_safe(yt_dev, next, &seat->devices_list, all_devices_link) {
			if (!strcmp(yt_dev->devnode, devnode)) {
				seat->hotplug_cb.del_cb(yt_dev, seat->hotplug_data);
				evdev_device_destroy(evdev_device(yt_dev));
				break;
			}
		}
	}

out:
	udev_device_unref(udev_device);

	return 0;
}

int evdev_enable_udev_monitor(struct udev_context *master)
{
	int fd;

	master->udev = udev_new();
	if (!master->udev) {
		printf("udev: Failed to create udev object.\n");
		return 0;
	}

	master->udev_monitor = udev_monitor_new_from_netlink(master->udev, "udev");
	if (!master->udev_monitor) {
		printf("udev: failed to create the udev monitor\n");
		return 0;
	}

	udev_monitor_filter_add_match_subsystem_devtype(master->udev_monitor,
			"input", NULL);

	if (udev_monitor_enable_receiving(master->udev_monitor)) {
		printf("udev: failed to bind the udev monitor\n");
		udev_monitor_unref(master->udev_monitor);
		return 0;
	}

	fd = udev_monitor_get_fd(master->udev_monitor);
	master->udev_fd = fd;

	return fd;
}

void evdev_disable_udev_monitor(struct udev_context *seat)
{
	if (!seat->udev_monitor)
		return;

	udev_monitor_unref(seat->udev_monitor);
	seat->udev_monitor = NULL;
	udev_unref(seat->udev);
}

void evdev_add_devices(struct udev_context *master)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	struct udev_device *device;
	const char *path, *sysname;

	e = udev_enumerate_new(master->udev);
	udev_enumerate_add_match_subsystem(e, "input");
	udev_enumerate_scan_devices(e);
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		device = udev_device_new_from_syspath(master->udev, path);

		sysname = udev_device_get_sysname(device);
		if (strncmp("event", sysname, 5) != 0) {
			udev_device_unref(device);
			continue;
		}

		device_added(device, master);

		udev_device_unref(device);
	}
	udev_enumerate_unref(e);
#if 0
	evdev_notify_keyboard_focus(&master->base, &master->devices_list);
#endif

	if (wl_list_empty(&master->devices_list)) {
		printf("warning: no input devices on entering Weston. "
				"Possible causes:\n"
				"\t- no permissions to read /dev/input/event*\n"
				"\t- seats misconfigured "
				"(Weston backend option 'seat', "
				"udev device property ID_SEAT)\n");
	}
}

static const char default_seat[] = "seat0";

struct evdev_device *device_added(struct udev_device *udev_device, struct udev_context *master)
{
	struct evdev_device *device;
	const char *devnode;

	devnode = udev_device_get_devnode(udev_device);

	/* Use non-blocking mode so that we can loop on read on
	 * evdev_device_data() until all events on the fd are
	 * read.  mtdev_get() also expects this. */
	device = evdev_device_create(devnode);
	if (!device) {
		printf("not using input device '%s'.\n", devnode);
		return NULL;
	}
	wl_list_insert(&master->devices_list, &device->base.all_devices_link);

	return device;
}
