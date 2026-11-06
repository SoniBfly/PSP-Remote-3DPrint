#include "key_control.h"

#include <pspctrl.h>

static SceCtrlData s_cur;
static SceCtrlData s_prev;

static const unsigned int s_key_bits[KEY_COUNT] = {
	PSP_CTRL_UP,
	PSP_CTRL_DOWN,
	PSP_CTRL_LEFT,
	PSP_CTRL_RIGHT,
	PSP_CTRL_CROSS,
	PSP_CTRL_CIRCLE,
	PSP_CTRL_SQUARE,
	PSP_CTRL_TRIANGLE,
	PSP_CTRL_SELECT,
	PSP_CTRL_START,
	PSP_CTRL_LTRIGGER,
	PSP_CTRL_RTRIGGER
};

void key_poll(void)
{
	s_prev = s_cur;
	sceCtrlReadBufferPositive(&s_cur, 1);
}

int key_just(enum key_id key)
{
	unsigned int bit;

	if (key < 0 || key >= KEY_COUNT)
		return 0;
	bit = s_key_bits[key];
	return (s_cur.Buttons & bit) && !(s_prev.Buttons & bit);
}

int key_down(enum key_id key)
{
	if (key < 0 || key >= KEY_COUNT)
		return 0;
	return (s_cur.Buttons & s_key_bits[key]) != 0;
}

void key_analog(unsigned int *out_x, unsigned int *out_y)
{
	if (out_x)
		*out_x = s_cur.Lx;
	if (out_y)
		*out_y = s_cur.Ly;
}

/* Analog stick is treated as pushed past this offset from center (128). */
#define KEY_ANALOG_DEAD 32

int key_down_analog(enum key_id key, enum key_axis axis)
{
	int v = (axis == KEY_AXIS_X) ? (int)s_cur.Lx : (int)s_cur.Ly;

	switch (key) {
	case KEY_LEFT:
	case KEY_UP:
		return v < 128 - KEY_ANALOG_DEAD;
	case KEY_RIGHT:
	case KEY_DOWN:
		return v > 128 + KEY_ANALOG_DEAD;
	default:
		return 0;
	}
}
