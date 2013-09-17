/*
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <mtdev.h>

#include <libudev.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include "yutani.h"
#include "udev.h"
#include "evdev.h"

#if defined(__GNUC__) && __GNUC__ >= 4
#define YT_EXPORT __attribute__ ((visibility("default")))
#else
#define YT_EXPORT
#endif

/* Deprecated attribute */
#if defined(__GNUC__) && __GNUC__ >= 4
#define YT_DEPRECATED __attribute__ ((deprecated))
#else
#define YT_DEPRECATED
#endif

struct udev_context *uctx;

YT_EXPORT int yt_device_init(struct yt_hotplug_cbs *plug, void *data)
{
	int fd;

	uctx = calloc(1, sizeof(struct udev_context));
	wl_list_init(&uctx->devices_list);

	if (plug)
		uctx->hotplug_cb = *plug;
	uctx->hotplug_data = data;

	fd = evdev_enable_udev_monitor(uctx);

	evdev_add_devices(uctx);

	return fd;
}

YT_EXPORT void yt_device_hotplug_handle()
{
	evdev_udev_handler(uctx->udev_fd, 0, uctx);
}

YT_EXPORT struct wl_list *yt_device_get_devices()
{
	return &uctx->devices_list;
}

YT_EXPORT int yt_device_add_to_seat(struct yt_device *device, struct yt_seat *seat)
{
	struct evdev_device *dev = evdev_device(device);
	dev->seat = seat;
	device->fd = open(device->devnode, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (!(device->fd < 0))
	{
		wl_list_insert(&seat->devices, &device->seat_link);
	}

	return device->fd;
}

YT_EXPORT int yt_device_del_from_seat(struct yt_device *device, struct yt_seat *seat __UNUSED__)
{
	struct evdev_device *dev = evdev_device(device);
	if (!(device->fd < 0))
	{
		wl_list_remove(&device->seat_link);
		close(device->fd);
		device->fd = -1;
	}
	dev->seat = NULL;
	return 0;
}

YT_EXPORT int yt_device_handle(struct yt_device *device)
{
	struct evdev_device *dev = evdev_device(device);
	struct input_event ev[32];
	ssize_t len;

	do {
		len = read(device->fd, &ev, sizeof(ev));

		if (len < 0 || len % sizeof(ev[0]) != 0) {
			/* FIXME: destroy device? */
			return 1;
		}

		evdev_process_events(dev, ev, len/sizeof(ev[0]));
	} while (len > 0);

	return 1;

}

YT_EXPORT struct yt_seat *yt_seat_create(const char *name,
		struct yt_seat_notify_interface *notify, void *data)
{
	if (!name)
		return NULL;

	struct yt_seat *seat = calloc(1, sizeof(struct yt_seat));
	if (!seat)
		return NULL;

	seat->name = strdup(name);

	wl_list_init(&seat->devices);

	if (notify)
		memcpy(&seat->notify, notify, sizeof(struct yt_seat_notify_interface));

	seat->notify_data = data;

	return seat;
}

YT_EXPORT void yt_device_user_data_set(struct yt_device *device, void *user_data)
{
	struct evdev_device *dev = evdev_device(device);
	dev->user_data = user_data;
}

YT_EXPORT void *yt_device_user_data_get(struct yt_device *device)
{
	struct evdev_device *dev = evdev_device(device);
	return dev->user_data;
}

YT_EXPORT void yt_device_leds_state_set(struct yt_device *device, enum yt_led_state state)
{
	struct evdev_device *dev = evdev_device(device);
	if (!(device->caps & YT_LED))
		return;
	if (dev->led_state == state)
		return;
	evdev_led_update(dev, state);
}

YT_EXPORT enum yt_led_state yt_seat_leds_state_get(struct yt_device *device)
{
	struct evdev_device *dev = evdev_device(device);
	return dev->led_state;
}
