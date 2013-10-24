#ifndef YT_COMMON_H
#define YT_COMMON_H
#include <unistd.h>
#include <errno.h>
#define __UNUSED__ __attribute__ ((unused))
#include <wayland-util.h>

struct yt_seat;

struct yt_seat_notify_interface *yt_seat_notify_get(struct yt_seat *seat, void **data);
struct wl_event_loop *yt_seat_wl_event_loop_get(struct yt_seat *seat);
struct wl_event_loop *yt_seat_wl_disp_loop_get(struct yt_seat *seat);
#endif // YT_COMMON_H
