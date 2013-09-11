#ifndef COMPAT_H
#define COMPAT_H

#include <stdio.h>



#define weston_log(...) fprintf(stderr, __VA_ARGS__)

/* FIXME */
	NONE = 0,
};

enum weston_keyboard_modifier {
	MODIFIER_CTRL = (1 << 0),
	MODIFIER_ALT = (1 << 1),
	MODIFIER_SUPER = (1 << 2),
	MODIFIER_SHIFT = (1 << 3),
};

#endif /* COMPAT_H */
