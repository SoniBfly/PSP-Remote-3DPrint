#include "screen.h"

#define SCREEN_MAX 8

static const struct screen *s_screens[SCREEN_MAX];
static int s_count;
static const struct screen *s_current;

void screens_register(const struct screen *s)
{
	if (!s || s_count >= SCREEN_MAX)
		return;
	s_screens[s_count++] = s;
}

void screen_go(const struct screen *next)
{
	if (!next || next == s_current)
		return;
	if (s_current && s_current->leave)
		s_current->leave();
	s_current = next;
	if (s_current->enter)
		s_current->enter();
}

const struct screen *screen_current(void)
{
	return s_current;
}
