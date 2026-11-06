#include "text.h"
#include "font8x8.h"
#include "render.h"

#include <string.h>

int ui_text_width(int scale, const char *s)
{
	int n;

	if (!s || scale < 1)
		return 0;
	n = (int)strlen(s);
	return n * 8 * scale;
}

void ui_text(int x, int y, int scale, unsigned int abgr, const char *s)
{
	int cx, i, row, col, px, py, sx, sy;
	const unsigned char *g;

	if (!s || scale < 1)
		return;
	cx = x;
	for (i = 0; s[i]; i++) {
		g = font8x8_glyph((unsigned char)s[i]);
		for (row = 0; row < 8; row++) {
			unsigned char bits = g[row];
			for (col = 0; col < 8; col++) {
				/* font8x8: LSB = leftmost pixel */
				if (!(bits & (1u << col)))
					continue;
				px = cx + col * scale;
				py = y + row * scale;
				for (sy = 0; sy < scale; sy++)
					for (sx = 0; sx < scale; sx++)
						ui_pixel(px + sx, py + sy, abgr);
			}
		}
		cx += 8 * scale;
		if (cx >= UI_W)
			break;
	}
}
