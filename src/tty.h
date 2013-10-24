#ifndef TTY_H
#define TTY_H

#include "yutani.h"
struct tty;

int tty_event_fd_get(struct tty *tty);
int tty_signal_fd_get(struct tty *tty);
int on_tty_input(int fd, uint32_t mask, void *data);
int tty_activate_vt(struct tty *tty, int vt);
struct tty *tty_create(int tty_fd, int tty_nr, yt_tty_vt_func_t vt_func, void *data, struct yt_seat *seat);
void tty_reset(struct tty *tty);
void tty_destroy(struct tty *tty);
#endif //TTY_H
