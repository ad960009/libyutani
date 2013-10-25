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
#include <sys/epoll.h>

#include "yutani.h"

#define EPOLL_SIZE 10

struct yt_seat *seat;
int epoll_fd;

char *name;
int alldevices = 0;

void handle_add(struct yt_device *device, void *data)
{
	printf("Device added: %s\n", device->devname);
	if (strstr(device->devname, name) || alldevices) {
		int fd_evdev;
		struct epoll_event ev;
		ev.events = EPOLLIN;
		fd_evdev = yt_device_add_to_seat(device, seat);
		ev.data.ptr = device;
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_evdev, &ev);
		printf("epoll add fd: %d, device: %s\n", fd_evdev, device->devname);
	}
}

void handle_del(struct yt_device *device, void *data)
{
	printf("Device removed: %s\n", device->devname);
	if (strstr(device->devname, name) || alldevices) {
		struct epoll_event ev;
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, device->fd, &ev);
		printf("epoll del fd: %d, device: %s\n", device->fd, device->devname);
	}
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

struct yt_seat_notify_interface notify_api = {
	.notify_motion = motion_cb,
	.notify_button = button_cb,
	.notify_key = key_cb,
	.notify_axis = axis_cb,
	.notify_modifiers = modifier_cb,
	.notify_touch = touch_cb
};

int main(int argc, char *argv[])
{
	struct yt_device *device;
	struct epoll_event ev, events[EPOLL_SIZE];

	int udev_fd, i;
	struct wl_list *devlist;

	if (argc != 2) {
		printf("Usage: %s <inputname>|all\n", argv[0]);
		return 1;
	}

	name = strdup(argv[1]);
	if (strcmp(name, "all") == 0)
		alldevices = 1;

	seat = yt_seat_create("seat0", &notify_api, NULL);

	epoll_fd = epoll_create(128);
	if (epoll_fd < 0)
		return -1;

	udev_fd = yt_device_init(&plug_api, NULL);
	if (udev_fd < 0) {
		printf("Failed to init yt_device: %s\n", strerror(errno));
		return 1;
	}
	ev.events = EPOLLIN;
	ev.data.ptr = NULL;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udev_fd, &ev);

	devlist = yt_device_get_devices();

	wl_list_for_each(device, devlist, all_devices_link) {
		printf("Considering %s\n", device->devname);
		if (strstr(device->devname, name) || alldevices) {
			printf("Adding %s\n", device->devname);
			int fd_evdev;
			fd_evdev = yt_device_add_to_seat(device, seat);
			ev.data.ptr = device;
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_evdev, &ev);
		}
	}

	while(1) {
		int n = epoll_wait(epoll_fd, events, EPOLL_SIZE, -1);
		for (i = 0; i < n; i++)
		{
			if (events[i].data.ptr == NULL) {
				printf("udev event\n");
				yt_device_hotplug_handle();
			}
			else {
				struct yt_device *dev = events[i].data.ptr;
				yt_device_handle(dev);
			}

		}
	}

	return 0;
}
