lib_LTLIBRARIES = libyutani.la
noinst_PROGRAMS = yt_evdev_example

include_HEADERS =				\
	evdev.h					\
	yutani.h					\
	udev.h

libyutani_la_LIBADD = $(YT_LIBS)
libyutani_la_CFLAGS = $(YT_CFLAGS) $(GCC_CFLAGS)
libyutani_la_SOURCES =				\
	evdev.c					\
	yutani.c					\
	udev.c					\
	evdev-touchpad.c

yt_evdev_example_LDADD = libyutani.la $(YT_LIBS) $(EXAMPLE_LIBS)
yt_evdev_example_CFLAGS = $(EXAMPLE_CFLAGS)
yt_evdev_example_SOURCES =				\
	yt_evdev_example.c
