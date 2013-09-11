/*
 * Copyright © 2010 Intel Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <mtdev.h>
#include <errno.h>

#include <wayland-server.h>
#include "evdev.h"
#include "yutani.h"

void evdev_led_update(struct evdev_device *device, enum yutani_led leds)
{
	static const struct {
		enum yutani_led weston;
		int evdev;
	} map[] = {
		{
			LED_NUM_LOCK, LED_NUML}, {
				LED_CAPS_LOCK, LED_CAPSL}, {
					LED_SCROLL_LOCK, LED_SCROLLL},};
	struct input_event ev[ARRAY_LENGTH(map)];
	unsigned int i;

	if (!(device->base.caps & YT_LED))
		return;

	memset(ev, 0, sizeof(ev));
	for (i = 0; i < ARRAY_LENGTH(map); i++) {
		ev[i].type = EV_LED;
		ev[i].code = map[i].evdev;
		ev[i].value = ! !(leds & map[i].weston);
	}

	i = write(device->base.fd, ev, sizeof ev);
	(void)i;		/* no, we really don't care about the return value */
}

static inline void evdev_process_key(struct evdev_device *device, struct input_event *e, int time)
{
	struct yt_seat *master = device->seat;

	/* ignore kernel key repeat */
	if (e->value == 2)
		return;

	switch (e->code) {
		case BTN_LEFT:
		case BTN_RIGHT:
		case BTN_MIDDLE:
		case BTN_SIDE:
		case BTN_EXTRA:
		case BTN_FORWARD:
		case BTN_BACK:
		case BTN_TASK:
			if (master->notify.notify_button)
				master->notify.notify_button((struct yt_device *)device, time,
						e->code,
						e->value ? YT_BUTTON_STATE_PRESSED :
						YT_BUTTON_STATE_RELEASED);
			break;
		case BTN_TOUCH:
			if (master->notify.notify_touch && e->value == 0 && !device->is_mt)
				master->notify.notify_touch((struct yt_device *)device, time, 0, 0, 0,
						YT_TOUCH_STATE_UP);
			break;
		default:
			if (master->notify.notify_key)
				master->notify.notify_key((struct yt_device *)device, time, e->code,
						e->value ? YT_KEY_STATE_PRESSED :
						YT_KEY_STATE_RELEASED,
						0);
			break;
	}
}

static void evdev_process_touch(struct evdev_device *device, struct input_event *e)
{
	switch (e->code) {
		case ABS_MT_SLOT:
			device->mt.slot = e->value;
			break;
		case ABS_MT_TRACKING_ID:
			if (e->value >= 0)
				device->pending_events |= EVDEV_ABSOLUTE_MT_DOWN;
			else
				device->pending_events |= EVDEV_ABSOLUTE_MT_UP;
			break;
		case ABS_MT_POSITION_X:
			device->mt.x[device->mt.slot] = e->value;
			device->pending_events |= EVDEV_ABSOLUTE_MT_MOTION;
			break;
		case ABS_MT_POSITION_Y:
			device->mt.y[device->mt.slot] = e->value;
			device->pending_events |= EVDEV_ABSOLUTE_MT_MOTION;
			break;
	}
}

static inline void evdev_process_absolute_motion(struct evdev_device *device,
		struct input_event *e)
{
	switch (e->code) {
		case ABS_X:
			device->abs.x = e->value;
			device->pending_events |= EVDEV_ABSOLUTE_MOTION;
			break;
		case ABS_Y:
			device->abs.y = e->value;
			device->pending_events |= EVDEV_ABSOLUTE_MOTION;
			break;
	}
}

static inline void evdev_process_relative(struct evdev_device *device,
		struct input_event *e, uint32_t time)
{
	struct yt_seat *master = device->seat;

	switch (e->code) {
		case REL_X:
			device->rel.dx += wl_fixed_from_int(e->value);
			device->pending_events |= EVDEV_RELATIVE_MOTION;
			break;
		case REL_Y:
			device->rel.dy += wl_fixed_from_int(e->value);
			device->pending_events |= EVDEV_RELATIVE_MOTION;
			break;
		case REL_WHEEL:
			switch (e->value) {
				case -1:
					/* Scroll down */
				case 1:
					/* Scroll up */
					if (master->notify.notify_axis)
						master->notify.notify_axis((struct yt_device *)device,
								time,
								YT_AXIS_TYPE_VERTICAL_SCROLL,
								-1 * e->value);
					break;
				default:
					break;
			}
			break;
		case REL_HWHEEL:
			switch (e->value) {
				case -1:
					/* Scroll left */
				case 1:
					/* Scroll right */
					if (master->notify.notify_axis)
						master->notify.notify_axis((struct yt_device *)device,
								time,
								YT_AXIS_TYPE_HORIZONTAL_SCROLL,
								e->value);
					break;
				default:
					break;

			}
	}
}

static inline void evdev_process_absolute(struct evdev_device *device, struct input_event *e)
{
	if (device->is_mt) {
		evdev_process_touch(device, e);
	} else {
		evdev_process_absolute_motion(device, e);
	}
}

static inline int is_motion_event(struct input_event *e)
{
	switch (e->type) {
		case EV_REL:
			switch (e->code) {
				case REL_X:
				case REL_Y:
					return 1;
			}
			break;
		case EV_ABS:
			switch (e->code) {
				case ABS_X:
				case ABS_Y:
				case ABS_MT_POSITION_X:
				case ABS_MT_POSITION_Y:
					return 1;
			}
	}

	return 0;
}

static void transform_absolute(struct evdev_device *device)
{
	if (!device->abs.apply_calibration)
		return;

	device->abs.x = device->abs.x * device->abs.calibration[0] +
		device->abs.y * device->abs.calibration[1] +
		device->abs.calibration[2];

	device->abs.y = device->abs.x * device->abs.calibration[3] +
		device->abs.y * device->abs.calibration[4] +
		device->abs.calibration[5];
}

static void evdev_flush_motion(struct evdev_device *device, uint32_t time)
{
	struct yt_seat *master = device->seat;

	if (!(device->pending_events & EVDEV_SYN))
		return;

	device->pending_events &= ~EVDEV_SYN;
	if (device->pending_events & EVDEV_RELATIVE_MOTION) {
		if (master->notify.notify_motion)
			master->notify.notify_motion((struct yt_device *)device, time, device->rel.dx, device->rel.dy);
		device->pending_events &= ~EVDEV_RELATIVE_MOTION;
		device->rel.dx = 0;
		device->rel.dy = 0;
	}
	if (device->pending_events & EVDEV_ABSOLUTE_MT_DOWN) {
		if (master->notify.notify_touch)
			master->notify.notify_touch((struct yt_device *)device, time, device->mt.slot,
					wl_fixed_from_int(device->mt.x[device->mt.slot]),
					wl_fixed_from_int(device->mt.y[device->mt.slot]),
					YT_TOUCH_STATE_DOWN);
		device->pending_events &= ~EVDEV_ABSOLUTE_MT_DOWN;
		device->pending_events &= ~EVDEV_ABSOLUTE_MT_MOTION;
	}
	if (device->pending_events & EVDEV_ABSOLUTE_MT_MOTION) {
		if (master->notify.notify_touch)
			master->notify.notify_touch((struct yt_device *)device, time, device->mt.slot,
					wl_fixed_from_int(device->mt.x[device->mt.slot]),
					wl_fixed_from_int(device->mt.y[device->mt.slot]),
					YT_TOUCH_STATE_MOVE);
		device->pending_events &= ~EVDEV_ABSOLUTE_MT_DOWN;
		device->pending_events &= ~EVDEV_ABSOLUTE_MT_MOTION;
	}
	if (device->pending_events & EVDEV_ABSOLUTE_MT_UP) {
		if (master->notify.notify_touch)
			master->notify.notify_touch((struct yt_device *)device, time, device->mt.slot,
					wl_fixed_from_int(device->mt.x[device->mt.slot]),
					wl_fixed_from_int(device->mt.y[device->mt.slot]),
					WL_TOUCH_UP);

		device->pending_events &= ~EVDEV_ABSOLUTE_MT_UP;
	}
	if (device->pending_events & EVDEV_ABSOLUTE_MOTION) {
		transform_absolute(device);
		if (master->notify.notify_motion_absolute)
			master->notify.notify_motion_absolute((struct yt_device *)device, time,
					wl_fixed_from_int(device->abs.x),
					wl_fixed_from_int(device->abs.y));
		device->pending_events &= ~EVDEV_ABSOLUTE_MOTION;
	}
}

static void fallback_process(struct evdev_dispatch *dispatch __UNUSED__,
		struct evdev_device *device,
		struct input_event *event, uint32_t time)
{
	switch (event->type) {
		case EV_REL:
			evdev_process_relative(device, event, time);
			break;
		case EV_ABS:
			evdev_process_absolute(device, event);
			break;
		case EV_KEY:
			evdev_process_key(device, event, time);
			break;
		case EV_SYN:
			device->pending_events |= EVDEV_SYN;
			break;
	}
}

static void fallback_destroy(struct evdev_dispatch *dispatch __UNUSED__)
{
	return;
}

struct evdev_dispatch_interface fallback_interface = {
	fallback_process,
	fallback_destroy
};

static const struct evdev_dispatch fallback_dispatch = {
	.interface = &fallback_interface
};

void evdev_process_events(struct evdev_device *device,
		struct input_event *ev, int count)
{
	struct evdev_dispatch *dispatch = device->dispatch;
	struct input_event *e, *end;
	uint32_t time = 0;

	device->pending_events = 0;

	e = ev;
	end = e + count;
	for (e = ev; e < end; e++) {
		time = e->time.tv_sec * 1000 + e->time.tv_usec / 1000;

		/* we try to minimize the amount of notifications to be
		 * forwarded to the compositor, so we accumulate motion
		 * events and send as a bunch */
		if (!is_motion_event(e))
			evdev_flush_motion(device, time);

		dispatch->interface->process(dispatch, device, e, time);
	}

	evdev_flush_motion(device, time);
}

int evdev_device_data(int fd, uint32_t mask __UNUSED__, void *data)
{
	struct evdev_device *device = data;
	struct input_event ev[32];
	int len;

	/* If the compositor is repainting, this function is called only once
	 * per frame and we have to process all the events available on the
	 * fd, otherwise there will be input lag. */
	do {
		if (device->mtdev)
			len = mtdev_get(device->mtdev, fd, ev, ARRAY_LENGTH(ev)) * sizeof(struct input_event);
		else
			len = read(fd, &ev, sizeof ev);

		if (len < 0 || len % sizeof ev[0] != 0) {
			/* FIXME: call evdev_device_destroy when errno is ENODEV. */
			return 1;
		}

		evdev_process_events(device, ev, len / sizeof ev[0]);
		printf("\n");

	} while (len > 0);

	return 1;
}

static int evdev_handle_device(struct evdev_device *device)
{
	struct input_absinfo absinfo;
	unsigned long ev_bits[NBITS(EV_MAX)];
	unsigned long abs_bits[NBITS(ABS_MAX)];
	unsigned long rel_bits[NBITS(REL_MAX)];
	unsigned long key_bits[NBITS(KEY_MAX)];
	int has_key, has_abs;
	unsigned int i;

	has_key = 0;
	has_abs = 0;
	device->base.caps = 0;

	ioctl(device->base.fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits);
	if (TEST_BIT(ev_bits, EV_ABS)) {
		has_abs = 1;

		ioctl(device->base.fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)),
				abs_bits);
		if (TEST_BIT(abs_bits, ABS_X)) {
			ioctl(device->base.fd, EVIOCGABS(ABS_X), &absinfo);
			device->abs.min_x = absinfo.minimum;
			device->abs.max_x = absinfo.maximum;
			device->base.caps |= YT_MOTION_ABS;
		}
		if (TEST_BIT(abs_bits, ABS_Y)) {
			ioctl(device->base.fd, EVIOCGABS(ABS_Y), &absinfo);
			device->abs.min_y = absinfo.minimum;
			device->abs.max_y = absinfo.maximum;
			device->base.caps |= YT_MOTION_ABS;
		}
		if (TEST_BIT(abs_bits, ABS_MT_SLOT)) {
			ioctl(device->base.fd, EVIOCGABS(ABS_MT_POSITION_X),
					&absinfo);
			device->abs.min_x = absinfo.minimum;
			device->abs.max_x = absinfo.maximum;
			ioctl(device->base.fd, EVIOCGABS(ABS_MT_POSITION_Y),
					&absinfo);
			device->abs.min_y = absinfo.minimum;
			device->abs.max_y = absinfo.maximum;
			device->is_mt = 1;
			device->mt.slot = 0;
			device->base.caps |= YT_TOUCH;
		}
	}
	if (TEST_BIT(ev_bits, EV_REL)) {
		ioctl(device->base.fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)),
				rel_bits);
		if (TEST_BIT(rel_bits, REL_X) || TEST_BIT(rel_bits, REL_Y))
			device->base.caps |= YT_MOTION_REL;
	}
	if (TEST_BIT(ev_bits, EV_KEY)) {
		has_key = 1;
		ioctl(device->base.fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
		if (TEST_BIT(key_bits, BTN_TOOL_FINGER) &&
				!TEST_BIT(key_bits, BTN_TOOL_PEN) && has_abs)
			device->dispatch = evdev_touchpad_create(device);
		for (i = KEY_ESC; i < KEY_MAX; i++) {
			if (i >= BTN_MISC && i < KEY_OK)
				continue;
			if (TEST_BIT(key_bits, i)) {
				device->base.caps |= YT_KEYBOARD;
				break;
			}
		}
		for (i = BTN_MISC; i < KEY_OK; i++) {
			if (TEST_BIT(key_bits, i)) {
				device->base.caps |= YT_BUTTON;
				break;
			}
		}
	}
	if (TEST_BIT(ev_bits, EV_LED)) {
		device->base.caps |= YT_LED;
	}

	/* This rule tries to catch accelerometer devices and opt out. We may
	 * want to adjust the protocol later adding a proper event for dealing
	 * with accelerometers and implement here accordingly */
	if (!has_abs && !has_key && !device->is_mt) {
		printf("input device %s, %s "
				"ignored: unsupported device type\n",
				device->base.devname, device->base.devnode);
		return 0;
	}

	printf("\t\tdevice %s, %s\n"
			"\t\t\tcaps: 0x2%x, KEYBOARD: %c, BUTTON: %c, MOTION_ABS: %c, MOTION_REL: %c, TOUCH: %c, LED:%c\n",
			device->base.devname, device->base.devnode, device->base.caps,
			device->base.caps & YT_KEYBOARD ? 'O' : 'X',
			device->base.caps & YT_BUTTON ? 'O' : 'X',
			device->base.caps & YT_MOTION_ABS ? 'O' : 'X',
			device->base.caps & YT_MOTION_REL ? 'O' : 'X',
			device->base.caps & YT_TOUCH ? 'O' : 'X',
			device->base.caps & YT_LED ? 'O' : 'X');

	return 1;
}
/*
static int evdev_configure_device(struct evdev_device *device)
{
	if ((device->base.caps &
				(YT_MOTION_ABS | YT_MOTION_REL | YT_BUTTON))) {
		//		weston_seat_init_pointer(device->seat);
	}
	if ((device->base.caps & YT_KEYBOARD)) {
		//		if (weston_seat_init_keyboard(device->seat, NULL) < 0)
		//			return -1;
	}
	if ((device->base.caps & YT_TOUCH)) {
		//		weston_seat_init_touch(device->seat);
	}

	return 0;
}*/

struct evdev_device *evdev_device_create(const char *path)
{
	struct evdev_device *device;
	char devname[256] = "unknown";

	device = calloc(1, sizeof(struct evdev_device));
	if (device == NULL)
		return NULL;

	device->is_mt = 0;
	device->mtdev = NULL;
	device->base.devnode = strdup(path);
	device->mt.slot = -1;
	device->rel.dx = 0;
	device->rel.dy = 0;
	device->dispatch = NULL;

	device->base.fd = open(path, O_RDWR | O_CLOEXEC);
	if (device->base.fd < 0) {
		printf("Failed to open %s: %s\n", path, strerror(errno));
		goto err1;
	}

	ioctl(device->base.fd, EVIOCGNAME(sizeof(devname)), devname);
	device->base.devname = strdup(devname);

	if (!evdev_handle_device(device)) {
		goto err1;
	}

/*	if (evdev_configure_device(device) == -1)
		goto err1;
*/
	/* If the dispatch was not set up use the fallback. */
	if (device->dispatch == NULL)
		device->dispatch = &fallback_dispatch;

	if (device->is_mt) {
		device->mtdev = mtdev_new_open(device->base.fd);
		if (!device->mtdev)
			printf("mtdev failed to open for %s\n", path);
	}

	close(device->base.fd);
	return device;

//err2:
//	device->dispatch->interface->destroy(device->dispatch);
err1:
	if (!(device->base.fd < 0))
		close(device->base.fd);
	free(device->base.devname);
	free(device->base.devnode);
	free(device);
	return NULL;
}

void evdev_device_destroy(struct evdev_device *device)
{
	struct evdev_dispatch *dispatch;

	dispatch = device->dispatch;
	if (dispatch)
		dispatch->interface->destroy(dispatch);

	if (!wl_list_empty(&device->base.seat_link))
		wl_list_remove(&device->base.seat_link);
	if (!(device->base.fd < 0))
		close(device->base.fd);

	if (!wl_list_empty(&device->base.all_devices_link))
		wl_list_remove(&device->base.all_devices_link);

	if (device->mtdev)
		mtdev_close_delete(device->mtdev);
	close(device->base.fd);
	free(device->base.devname);
	free(device->base.devnode);
	free(device);
}
