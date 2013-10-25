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

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/major.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/signalfd.h>
#include "tty.h"
/* Introduced in 2.6.38 */
#ifndef K_OFF
#define K_OFF 0x04
#endif
#include "common.h"

struct tty {
	void *data;
	int event_fd;
	int signal_fd;
	struct termios terminal_attributes;

	yt_tty_vt_func_t vt_func;
	int vt, starting_vt, has_vt;
	int kb_mode;
};

int tty_event_fd_get(struct tty *tty)
{
	return tty->event_fd;
}

int tty_signal_fd_get(struct tty *tty)
{
	return tty->signal_fd;
}

int tty_vt_handler(int signal_number __UNUSED__, void *data)
{
	struct tty *tty = data;

	if (tty->has_vt) {
		tty->vt_func(tty->data, TTY_LEAVE_VT);
		tty->has_vt = 0;

		ioctl(tty->event_fd, VT_RELDISP, 1);
	} else {
		ioctl(tty->event_fd, VT_RELDISP, VT_ACKACQ);

		tty->vt_func(tty->data, TTY_ENTER_VT);
		tty->has_vt = 1;
	}

	return 1;
}

int on_tty_input(int fd __UNUSED__, uint32_t mask __UNUSED__, void *data)
{
	struct tty *tty = data;

	/* Ignore input to tty.  We get keyboard events from evdev */
	tcflush(tty->event_fd, TCIFLUSH);

	return 1;
}

static int try_open_vt(struct tty *tty)
{
	int tty0, fd;
	char filename[16];

	tty0 = open("/dev/tty0", O_WRONLY | O_CLOEXEC);
	if (tty0 < 0) {
		printf("could not open tty0: %s(%d)\n", strerror(errno), errno);
		return -1;
	}

	if (ioctl(tty0, VT_OPENQRY, &tty->vt) < 0 || tty->vt == -1) {
		printf("could not open tty0: %s(%d)\n", strerror(errno), errno);
		close(tty0);
		return -1;
	}

	close(tty0);
	snprintf(filename, sizeof filename, "/dev/tty%d", tty->vt);
	printf("using new vt %s\n", filename);
	fd = open(filename, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0)
		return fd;

	return fd;
}

int tty_activate_vt(struct tty *tty, int vt)
{
	return ioctl(tty->event_fd, VT_ACTIVATE, vt);
}

struct tty *tty_create(int tty_fd, int tty_nr, yt_tty_vt_func_t vt_func, void *data)
{
	struct termios raw_attributes;
	struct vt_mode mode = { 0, 0, 0, 0, 0 };
	int ret;
	struct tty *tty;
	struct stat buf;
	char filename[16];
	struct vt_stat vts;

	tty = malloc(sizeof *tty);
	if (tty == NULL)
		return NULL;

	memset(tty, 0, sizeof *tty);
	tty->data = data;
	tty->vt_func = vt_func;

	tty->event_fd = tty_fd;
	if (tty->event_fd < 0)
		tty->event_fd = STDIN_FILENO;

	if (tty_nr > 0) {
		snprintf(filename, sizeof filename, "/dev/tty%d", tty_nr);
		printf("using %s\n", filename);
		tty->event_fd = open(filename, O_RDWR | O_NOCTTY | O_CLOEXEC);
		tty->vt = tty_nr;
	} else if (fstat(tty->event_fd, &buf) == 0 &&
			major(buf.st_rdev) == TTY_MAJOR && minor(buf.st_rdev) > 0) {
		if (tty->event_fd == STDIN_FILENO)
			tty->event_fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
		tty->vt = minor(buf.st_rdev);
	} else {
		/* Fall back to try opening a new VT.  This typically
		 * requires root. */
		tty->event_fd = try_open_vt(tty);
	}

	if (tty->event_fd <= 0) {
		printf("failed to open tty: %s(%d)\n", strerror(errno), errno);
		free(tty);
		return NULL;
	}

	if (ioctl(tty->event_fd, VT_GETSTATE, &vts) == 0)
		tty->starting_vt = vts.v_active;
	else
		tty->starting_vt = tty->vt;

	if (tty->starting_vt != tty->vt) {
		if (ioctl(tty->event_fd, VT_ACTIVATE, tty->vt) < 0 ||
				ioctl(tty->event_fd, VT_WAITACTIVE, tty->vt) < 0) {
			printf("failed to swtich to new vt\n");
			goto err;
		}
	}

	if (tcgetattr(tty->event_fd, &tty->terminal_attributes) < 0) {
		printf("could not get terminal attributes: %s(%d)\n", strerror(errno), errno);
		goto err;
	}

	/* Ignore control characters and disable echo */
	raw_attributes = tty->terminal_attributes;
	cfmakeraw(&raw_attributes);

	/* Fix up line endings to be normal (cfmakeraw hoses them) */
	raw_attributes.c_oflag |= OPOST | OCRNL;

	if (tcsetattr(tty->event_fd, TCSANOW, &raw_attributes) < 0)
		printf("could not put terminal into raw mode: %s(%d)\n", strerror(errno), errno);

	ioctl(tty->event_fd, KDGKBMODE, &tty->kb_mode);
	ret = ioctl(tty->event_fd, KDSKBMODE, K_OFF);
	if (ret) {
		ret = ioctl(tty->event_fd, KDSKBMODE, K_RAW);
		if (ret) {
			printf("failed to set keyboard mode on tty: %s(%d)\n", strerror(errno), errno);
			goto err_attr;
		}
	}

	ret = ioctl(tty->event_fd, KDSETMODE, KD_GRAPHICS);
	if (ret) {
		printf("failed to set KD_GRAPHICS mode on tty: %s(%d)\n", strerror(errno), errno);
		goto err_kdkbmode;
	}

	tty->has_vt = 1;
	mode.mode = VT_PROCESS;
	mode.relsig = SIGUSR1;
	mode.acqsig = SIGUSR1;
	if (ioctl(tty->event_fd, VT_SETMODE, &mode) < 0) {
		printf("failed to take control of vt handling\n");
		goto err_kdmode;
	}

	sigset_t sig_mask;
	sigemptyset(&sig_mask);
	sigaddset(&sig_mask, SIGUSR1);
	if ((tty->signal_fd = signalfd(-1, &sig_mask, SFD_CLOEXEC)) < 0)
		goto err_vtmode;
	sigprocmask(SIG_BLOCK, &sig_mask, NULL);

	return tty;

err_vtmode:
	ioctl(tty->event_fd, VT_SETMODE, &mode);

err_kdmode:
	ioctl(tty->event_fd, KDSETMODE, KD_TEXT);

err_kdkbmode:
	ioctl(tty->event_fd, KDSKBMODE, tty->kb_mode);

err_attr:
	tcsetattr(tty->event_fd, TCSANOW, &tty->terminal_attributes);

err:
	close(tty->event_fd);
	free(tty);
	return NULL;
}

void tty_reset(struct tty *tty)
{
	struct vt_mode mode = { 0, 0, 0, 0, 0 };

	if (ioctl(tty->event_fd, KDSKBMODE, tty->kb_mode))
		printf("failed to restore keyboard mode: %s(%d)\n", strerror(errno), errno);

	if (ioctl(tty->event_fd, KDSETMODE, KD_TEXT))
		printf("failed to set KD_TEXT mode on tty: %s(%d)\n", strerror(errno), errno);

	if (tcsetattr(tty->event_fd, TCSANOW, &tty->terminal_attributes) < 0)
		printf("could not restore terminal to canonical mode\n");

	mode.mode = VT_AUTO;
	if (ioctl(tty->event_fd, VT_SETMODE, &mode) < 0)
		printf("could not reset vt handling\n");

	if (tty->has_vt && tty->vt != tty->starting_vt) {
		ioctl(tty->event_fd, VT_ACTIVATE, tty->starting_vt);
		ioctl(tty->event_fd, VT_WAITACTIVE, tty->starting_vt);
	}
}

void tty_destroy(struct tty *tty)
{
	close(tty->signal_fd);

	tty_reset(tty);

	close(tty->event_fd);

	free(tty);
}
