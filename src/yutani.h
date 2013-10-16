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

#ifndef YUTANI_H
#define YUTANI_H

#include <wayland-util.h>
#include <wayland-server.h>
enum yt_key_state_update {
	YT_KEY_STATE_NONE
};

enum yt_led_state {
	YT_LED_NUM_LOCK = (1 << 0),
	YT_LED_CAPS_LOCK = (1 << 1),
	YT_LED_SCROLL_LOCK = (1 << 2),
};

enum yt_device_capability {
	YT_KEYBOARD = (1 << 0),
	YT_BUTTON = (1 << 1),
	YT_MOTION_ABS = (1 << 2),
	YT_MOTION_REL = (1 << 3),
	YT_TOUCH = (1 << 4),
	YT_LED = (1 << 5)
};

enum yt_button_state {
	YT_BUTTON_STATE_PRESSED,
	YT_BUTTON_STATE_RELEASED
};

enum yt_key_state {
	YT_KEY_STATE_PRESSED,
	YT_KEY_STATE_RELEASED
};

enum yt_touch_state {
	YT_TOUCH_STATE_DOWN,
	YT_TOUCH_STATE_UP,
	YT_TOUCH_STATE_MOVE
};

enum yt_axis_type {
	YT_AXIS_TYPE_VERTICAL_SCROLL,
	YT_AXIS_TYPE_HORIZONTAL_SCROLL,
};

enum {
	TTY_ENTER_VT,
	TTY_LEAVE_VT
};

struct yt_device {
	struct wl_list all_devices_link;
	struct wl_list seat_link;
	enum yt_device_capability caps;
	char *devnode;
	char *devname;
	int fd;
	int timer_fd;
};

struct yt_seat_notify_interface {
	void (*notify_motion)(struct yt_device *device, uint32_t time,
			wl_fixed_t dx, wl_fixed_t dy);
	void (*notify_motion_absolute)(struct yt_device *device, uint32_t time,
			wl_fixed_t x, wl_fixed_t y);
	void (*notify_button)(struct yt_device *device, uint32_t time, int32_t button,
			enum yt_button_state state);
	void (*notify_axis)(struct yt_device *device, uint32_t time, enum yt_axis_type axis,
			wl_fixed_t value);
	void (*notify_modifiers)(struct yt_device *device, uint32_t serial);
	void (*notify_key)(struct yt_device *device, uint32_t time, uint32_t key,
			enum yt_key_state state, enum yt_key_state_update update_state);
	void (*notify_touch)(struct yt_device *device, uint32_t time, int touch_id,
			wl_fixed_t x, wl_fixed_t y, enum yt_touch_state state);
};

struct yt_seat {
	char *name;
	struct wl_list devices;
	int tty_event_fd;
	int tty_signal_fd;
};

struct yt_hotplug_cbs {
	void (*add_cb)(struct yt_device *dev, void *data);
	void (*del_cb)(struct yt_device *dev, void *data);
};

int yt_device_init(struct yt_hotplug_cbs *plug, void *data);
struct wl_list *yt_device_get_devices();
int yt_device_add_to_seat(struct yt_device *device, struct yt_seat *seat);
int yt_device_del_from_seat(struct yt_device *device, struct yt_seat *seat);
int yt_device_handle(struct yt_device *device);
int yt_device_timer_handle(struct yt_device *device);
struct yt_seat *yt_seat_create(const char *name,
		struct yt_seat_notify_interface *notify, void *data);
void yt_device_led_state_set(struct yt_seat *seat, enum yt_led_state state);
enum yt_led_state yt_device_led_state_get(struct yt_seat *seat);
void yt_device_hotplug_handle();
void yt_device_user_data_set(struct yt_device *device, void *user_data);
void *yt_device_user_data_get(struct yt_device *device);

typedef void (*yt_tty_vt_func_t)(void *data, int event);
int yt_tty_create(struct yt_seat *seat, int tty_fd, int tty_nr, yt_tty_vt_func_t vt_func, void *data);
int yt_tty_handle(struct yt_seat *seat);
void yt_tty_destroy(struct yt_seat *seat);

void yt_tty_reset(struct yt_seat *seat);
int yt_tty_on_input(struct yt_seat *seat);
int yt_tty_activate_vt(struct yt_seat *seat, int vt);
#endif /* YUTANI_H */
