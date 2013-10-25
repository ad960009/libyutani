#ifndef YT_COMMON_H
#define YT_COMMON_H
#include <unistd.h>
#include <errno.h>
#define __UNUSED__ __attribute__ ((unused))
struct yt_seat_notify_interface *yt_seat_notify_get(struct yt_seat *seat, void **data);
#endif // YT_COMMON_H
