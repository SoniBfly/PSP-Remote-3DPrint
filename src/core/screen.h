#ifndef FLSUN_SCREEN_H
#define FLSUN_SCREEN_H

#include <pspctrl.h>

struct screen {
	const char *name;
	void (*enter)(void);
	void (*input)(void);
	void (*draw)(void);
	void (*leave)(void);
};

void screens_register(const struct screen *s);
void screen_go(const struct screen *next);
const struct screen *screen_current(void);

#endif
