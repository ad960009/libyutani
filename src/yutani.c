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
#include "tty.h"
#include "common.h"

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

struct yt_seat_internal {
	struct yt_seat base;
	struct tty *tty;
	struct wl_event_loop *event_loop;
	struct wl_event_loop *disp_loop;
	struct wl_event_source *hotplug_source;
	struct yt_seat_notify_interface notify;
	void *notify_data;
};

static inline struct yt_seat_internal *yt_seat_internal(struct yt_seat *seat)
{
	return (struct yt_seat_internal *)seat;
}

struct yt_seat_notify_interface *yt_seat_notify_get(struct yt_seat *seat, void **data)
{
	if (data)
		*data = yt_seat_internal(seat)->notify_data;
	return &(yt_seat_internal(seat)->notify);
}

struct wl_event_loop *yt_seat_wl_event_loop_get(struct yt_seat *seat)
{
	return yt_seat_internal(seat)->event_loop;
}

struct wl_event_loop *yt_seat_wl_disp_loop_get(struct yt_seat *seat)
{
	return yt_seat_internal(seat)->disp_loop;
}

static int __yt_device_hotplug_handle(int fd, uint32_t mask __UNUSED__, void *data)
{
	evdev_udev_handler(fd, 0, data);
	return 1;
}

YT_EXPORT int yt_device_init(struct yt_hotplug_cbs *plug, void *data, struct yt_seat *seat)
{
	int fd;
	struct wl_event_loop *loop;
	struct yt_seat_internal *seat_i = yt_seat_internal(seat);

	uctx = calloc(1, sizeof(struct udev_context));
	wl_list_init(&uctx->devices_list);
	loop = seat_i->event_loop;

	if (plug)
		uctx->hotplug_cb = *plug;
	uctx->hotplug_data = data;

	fd = evdev_enable_udev_monitor(uctx);
	seat_i->hotplug_source = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, __yt_device_hotplug_handle, uctx);

	evdev_add_devices(uctx);

	return 0;
}


YT_EXPORT struct wl_list *yt_device_get_devices()
{
	return &uctx->devices_list;
}

static int __yt_device_handle(int fd, uint32_t mask, void *data)
{
	return evdev_device_data(fd, mask, data);
}

YT_EXPORT int yt_device_add_to_seat(struct yt_device *device, struct yt_seat *seat)
{
	struct evdev_device *dev = evdev_device(device);
	struct yt_seat_internal *seat_i = yt_seat_internal(seat);
	dev->seat = seat;
	dev->fd = open(device->devnode, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (!(dev->fd < 0))
	{
		wl_list_insert(&seat->devices, &device->seat_link);
		dev->source = wl_event_loop_add_fd(seat_i->event_loop, dev->fd, WL_EVENT_READABLE, __yt_device_handle, dev);
	}

	return dev->fd;
}

YT_EXPORT int yt_device_del_from_seat(struct yt_device *device, struct yt_seat *seat __UNUSED__)
{
	struct evdev_device *dev = evdev_device(device);
	if (!(dev->fd < 0))
	{
		if (!wl_list_empty(&device->seat_link))
			wl_list_remove(&device->seat_link);
		wl_event_source_remove(dev->source);
		close(dev->fd);
		dev->fd = -1;
	}
	dev->seat = NULL;
	return 0;
}

YT_EXPORT struct yt_seat *yt_seat_create(struct yt_seat_create_param *param)
{
	if (!param || !param->seat_name || !param->display_loop || !param->event_loop)
		return NULL;

	struct yt_seat_internal *seat = calloc(1, sizeof(struct yt_seat_internal));
	if (!seat)
		return NULL;
	seat->event_loop = param->event_loop;

	seat->disp_loop = param->display_loop;

	seat->base.name = strdup(param->seat_name);

	wl_list_init(&seat->base.devices);

	if (param->notify)
		memcpy(&seat->notify, param->notify, sizeof(struct yt_seat_notify_interface));

	seat->notify_data = param->notify_data;

	seat->tty = NULL;
	return (struct yt_seat *)seat;
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

YT_EXPORT void yt_device_led_state_set(struct yt_device *device, enum yt_led_state state)
{
	struct evdev_device *dev = evdev_device(device);
	if (!(device->caps & YT_LED))
		return;
	if (dev->led_state == state)
		return;
	evdev_led_update(dev, state);
}

YT_EXPORT enum yt_led_state yt_device_led_state_get(struct yt_device *device)
{
	struct evdev_device *dev = evdev_device(device);
	return dev->led_state;
}

YT_EXPORT int yt_tty_create(struct yt_seat *seat, int tty_fd, int tty_nr, yt_tty_vt_func_t vt_func, void *data)
{
	struct yt_seat_internal *seat_i = yt_seat_internal(seat);
	void *tty = tty_create(tty_fd, tty_nr, vt_func, data, seat);
	if (tty) {
		seat_i->tty = tty;
		return 1;
	}
	return 0;
}

YT_EXPORT void yt_tty_destroy(struct yt_seat *seat)
{
	struct yt_seat_internal *seat_i = yt_seat_internal(seat);
	if (seat_i->tty)
		tty_destroy(seat_i->tty);
	seat_i->tty = NULL;
}

YT_EXPORT void yt_tty_reset(struct yt_seat *seat)
{
	struct yt_seat_internal *seat_i = yt_seat_internal(seat);
	if (seat_i->tty)
		tty_reset(seat_i->tty);
}

YT_EXPORT int yt_tty_activate_vt(struct yt_seat *seat, int vt)
{
	struct yt_seat_internal *seat_i = yt_seat_internal(seat);
	if (seat_i->tty)
		return tty_activate_vt(seat_i->tty, vt);
	return -1;
}
