AC_PREREQ([2.64])

m4_define([_major_version], [0])
m4_define([_minor_version], [1])
m4_define([_micro_version], [0])
m4_define([_version],
          [_major_version._minor_version._micro_version])

AC_INIT([yutani],
        [_version],
        [],
        [yutani],
        [http://wayland.freedesktop.org/])

AC_SUBST([_VERSION_MAJOR], [_major_version])
AC_SUBST([_VERSION_MINOR], [_minor_version])
AC_SUBST([_VERSION_MICRO], [_micro_version])
AC_SUBST([_VERSION], [_version])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES(YT, [libudev wayland-server mtdev])
PKG_CHECK_MODULES(EXAMPLE, [wayland-server])

YT_LIBS="$YT_LIBS -lm"

GCC_CFLAGS="-Wall -Wextra -fvisibility=hidden"
AC_SUBST(GCC_CFLAGS)

AC_CONFIG_FILES([Makefile
                 yutani.pc
		 src/Makefile
])
AC_OUTPUT
