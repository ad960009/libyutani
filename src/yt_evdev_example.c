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

#define _GNU_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-util.h>
#include <libudev.h>
#include <wayland-client.h>

#include "yutani.h"


struct yt_seat *seat;

char *name;
int alldevices = 0;

void handle_add(struct yt_device *device, void *data)
{
	printf("Device added: %s\n", device->devname);
	if (strstr(device->devname, name) || alldevices) {
		if(yt_device_add_to_seat(device, seat) < 0)
			printf("failed yt_device_add_to_seat(\"%s\")\n", device->devname);
	}
}

void handle_del(struct yt_device *device, void *data)
{
	printf("Device removed: %s\n", device->devname);
	yt_device_del_from_seat(device, seat);
}

struct yt_hotplug_cbs plug_api = {
	.add_cb = &handle_add,
	.del_cb = &handle_del
};

void motion_cb(struct yt_device *device, void *data, uint32_t time,
			wl_fixed_t dx, wl_fixed_t dy)
{
	printf("motion: (%i, %i)\n", wl_fixed_to_int(dx), wl_fixed_to_int(dy));
}

void button_cb(struct yt_device *device, void *data, uint32_t time, int32_t button,
			enum yt_button_state state)
{
	printf("button: %i %s\n", button, state==YT_BUTTON_STATE_PRESSED?"pressed":"released");
}

void axis_cb(struct yt_device *device, void *data, uint32_t time, enum yt_axis_type axis,
			wl_fixed_t value)
{
	printf("axis: (%i: %i)\n", axis, wl_fixed_to_int(value));
}

void modifier_cb(struct yt_device *device, void *data, uint32_t serial)
{
	printf("%s, serial: 0x%x\n", __func__, serial);
}

void key_cb(struct yt_device *device, void *data, uint32_t time, uint32_t key,
			enum yt_key_state state,
			enum yt_key_state_update update_state)
{
	printf("key: %i %s\n", key, state==YT_KEY_STATE_PRESSED?"pressed":"released");
}

void touch_cb(struct yt_device *device, void *data, uint32_t time, int touch_id,
		wl_fixed_t x, wl_fixed_t y, enum yt_touch_state state)
{
	printf("touch_id: %d, (%i, %i), touch_type: %d(%s)\n", touch_id,
			wl_fixed_to_int(x),
			wl_fixed_to_int(y),
			state,
			state == YT_TOUCH_STATE_DOWN ? "DOWN" : (state == YT_TOUCH_STATE_UP ? "UP" : "MOVE")
		  );
}

static int read_input(int fd, uint32_t mask, void *data)
{
	wl_event_loop_dispatch(data, 0);

	return 1;
}

struct yt_seat_notify_interface notify_api = {
	.notify_motion = motion_cb,
	.notify_button = button_cb,
	.notify_key = key_cb,
	.notify_axis = axis_cb,
	.notify_modifiers = modifier_cb,
	.notify_touch = touch_cb
};

static void idle(void *data)
{
	return;
}

int main(int argc, char *argv[])
{
	struct yt_device *device;

	int udev_fd, i;
	struct wl_list *devlist;
	struct wl_display *display;
	struct wl_event_loop *disp_loop;
	struct wl_event_loop *event_loop;

	if (argc != 2) {
		printf("Usage: %s <inputname>|all\n", argv[0]);
		return 1;
	}

	name = strdup(argv[1]);
	if (strcmp(name, "all") == 0)
		alldevices = 1;

	display = wl_display_connect(NULL);

	//disp_loop = wl_display_get_event_loop(display);
	disp_loop = wl_event_loop_create();
	event_loop = wl_event_loop_create();
	struct yt_seat_create_param seat_param;
	seat_param.display_loop = disp_loop;
	seat_param.event_loop = event_loop;
	seat_param.notify = &notify_api;
	seat_param.seat_name = "seat0";
	seat_param.notify_data = NULL;
	
	seat = yt_seat_create(&seat_param);
	
	if (yt_device_init(&plug_api, NULL, seat) < 0) {
		printf("Failed to init yt_device: %s\n", strerror(errno));
		return 1;
	}

	devlist = yt_device_get_devices();

	wl_list_for_each(device, devlist, all_devices_link) {
		printf("Considering %s\n", device->devname);
		if (strstr(device->devname, name) || alldevices) {
			printf("Adding %s\n", device->devname);
			yt_device_add_to_seat(device, seat);
		}
	}

	int fd = wl_event_loop_get_fd(event_loop);
	struct wl_event_source *source = wl_event_loop_add_fd(disp_loop,
			fd, WL_EVENT_READABLE, read_input, event_loop);
	wl_event_loop_add_idle(disp_loop, idle, NULL);

	while(1)
		wl_event_loop_dispatch(disp_loop, 1);

	return 0;
}
