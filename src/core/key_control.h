#ifndef FLSUN_KEY_CONTROL_H
#define FLSUN_KEY_CONTROL_H

/* PSP button ids, one per physical key. Use with key_just()/key_down(). */
enum key_id {	KEY_UP = 0,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_CROSS,
	KEY_CIRCLE,
	KEY_SQUARE,
	KEY_TRIANGLE,
	KEY_SELECT,
	KEY_START,
	KEY_L,
	KEY_R,
	KEY_COUNT
};

/* Read the pad once per frame; call before any key_*() query. */
void key_poll(void);

/* Non-zero if the key transitioned from up to down this frame. */
int key_just(enum key_id key);

/* Non-zero if the key is currently held. */
int key_down(enum key_id key);

/* Analog stick axes, 0..255 (128 = center). Valid after key_poll(). */
void key_analog(unsigned int *out_x, unsigned int *out_y);

/* Which analog axis to read. */
enum key_axis {
	KEY_AXIS_X = 0,
	KEY_AXIS_Y
};

/* Non-zero if the analog stick is pushed toward the given direction
 * (KEY_UP/DOWN/LEFT/RIGHT) past the dead zone; 0 for non-direction keys. */
int key_down_analog(enum key_id key, enum key_axis axis);

#endif
