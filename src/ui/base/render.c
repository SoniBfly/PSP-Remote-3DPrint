#include "render.h"
#include "log.h"
#include "key_control.h"

#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int *s_fb;
static int s_bufw;
static int s_fmt;
static int s_ready;
static unsigned int *s_logged_fb; /* log ui_end only when fb changes */

static int clampi(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

void ui_begin_direct(void)
{
	void *top = NULL;
	int bufw = 0, fmt = 0;

	s_ready = 0;
	s_fb = NULL;
	if (sceDisplayGetFrameBuf(&top, &bufw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 ||
	    !top || bufw <= 0) {
		LOG_W("ui getfb FAIL top=%p bufw=%d fmt=%d", top, bufw, fmt);
		return;
	}
	if (fmt != PSP_DISPLAY_PIXEL_FORMAT_8888) {
		/* Also accept 565 — convert path later; for now report. */
		LOG_W("ui fmt=%d (want 8888=3) bufw=%d", fmt, bufw);
		return;
	}
	s_fb = (unsigned int *)top;
	s_bufw = bufw;
	s_fmt = fmt;
	s_ready = 1;
}

void ui_begin(void)
{
	ui_begin_direct();
	ui_fill(0, 0, UI_W, UI_H, UI_COL_BG);
}

void ui_end_direct(void)
{
	if (s_ready && s_fb) {
		/* Make sure the buffer we painted is the one on screen. */
		sceDisplaySetFrameBuf(s_fb, s_bufw, PSP_DISPLAY_PIXEL_FORMAT_8888,
				     PSP_DISPLAY_SETBUF_IMMEDIATE);
		/* Do not log every frame — only when the framebuffer pointer changes. */
		if (s_fb != s_logged_fb) {
			LOG_I("ui ok fb=%p bufw=%d pix0=0x%08X",
			      (void *)s_fb, s_bufw, (unsigned)s_fb[0]);
			s_logged_fb = s_fb;
		}
	}
	s_ready = 0;
	s_fb = NULL;
}

void ui_end(void)
{
	ui_end_direct();
}

void ui_fill(int x, int y, int w, int h, unsigned int abgr)
{
	int x0, y0, x1, y1, yy, xx;

	if (!s_ready || w <= 0 || h <= 0)
		return;
	x0 = clampi(x, 0, UI_W);
	y0 = clampi(y, 0, UI_H);
	x1 = clampi(x + w, 0, UI_W);
	y1 = clampi(y + h, 0, UI_H);
	for (yy = y0; yy < y1; yy++) {
		unsigned int *row = s_fb + yy * s_bufw;
		for (xx = x0; xx < x1; xx++)
			row[xx] = abgr;
	}
}

void ui_rect(int x, int y, int w, int h, unsigned int abgr)
{
	if (w <= 0 || h <= 0)
		return;
	ui_fill(x, y, w, 1, abgr);
	ui_fill(x, y + h - 1, w, 1, abgr);
	ui_fill(x, y, 1, h, abgr);
	ui_fill(x + w - 1, y, 1, h, abgr);
}

void ui_fill_round(int x, int y, int w, int h, unsigned int abgr)
{
	/* Soft corners: inset corners by 2px. */
	if (w < 6 || h < 6) {
		ui_fill(x, y, w, h, abgr);
		return;
	}
	ui_fill(x + 2, y, w - 4, h, abgr);
	ui_fill(x, y + 2, w, h - 4, abgr);
	ui_fill(x + 1, y + 1, w - 2, h - 2, abgr);
}

void ui_pixel(int x, int y, unsigned int abgr)
{
	if (!s_ready || x < 0 || y < 0 || x >= UI_W || y >= UI_H)
		return;
	s_fb[y * s_bufw + x] = abgr;
}

int ui_dump_bmp(const char *path)
{
	void *top = NULL;
	int bufw = 0, fmt = 0;
	unsigned int *fb;
	unsigned char hdr[54];
	int row_stride, y, x;
	SceUID fd;
	unsigned char *row = NULL;

	if (!path)
		return -1;
	if (sceDisplayGetFrameBuf(&top, &bufw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 ||
	    !top || bufw <= 0)
		return -1;
	if (fmt != PSP_DISPLAY_PIXEL_FORMAT_8888)
		return -2;
	fb = (unsigned int *)top;
	row_stride = UI_W * 3;
	row_stride = (row_stride + 3) & ~3;
	row = (unsigned char *)malloc((size_t)row_stride);
	if (!row)
		return -3;
	memset(hdr, 0, sizeof(hdr));
	hdr[0] = 'B';
	hdr[1] = 'M';
	{
		int file_size = 54 + row_stride * UI_H;
		hdr[2] = (unsigned char)(file_size);
		hdr[3] = (unsigned char)(file_size >> 8);
		hdr[4] = (unsigned char)(file_size >> 16);
		hdr[5] = (unsigned char)(file_size >> 24);
	}
	hdr[10] = 54;
	hdr[14] = 40;
	hdr[18] = (unsigned char)(UI_W);
	hdr[19] = (unsigned char)(UI_W >> 8);
	hdr[22] = (unsigned char)(UI_H);
	hdr[23] = (unsigned char)(UI_H >> 8);
	hdr[26] = 1;
	hdr[28] = 24;
	fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
	if (fd < 0) {
		free(row);
		return -4;
	}
	sceIoWrite(fd, hdr, 54);
	/* BMP is bottom-up */
	for (y = UI_H - 1; y >= 0; y--) {
		memset(row, 0, (size_t)row_stride);
		for (x = 0; x < UI_W; x++) {
			unsigned int p = fb[y * bufw + x];
			unsigned char r = (unsigned char)(p & 0xFF);
			unsigned char g = (unsigned char)((p >> 8) & 0xFF);
			unsigned char b = (unsigned char)((p >> 16) & 0xFF);
			row[x * 3 + 0] = b;
			row[x * 3 + 1] = g;
			row[x * 3 + 2] = r;
		}
		sceIoWrite(fd, row, row_stride);
	}
	sceIoClose(fd);
	free(row);
	return 0;
}

int ui_pointer(int *out_x, int *out_y)
{
	unsigned int ax, ay;
	int px, py;

	if (!out_x || !out_y)
		return 0;
	/* Left analog as optional pointer when L is held (dev aid on Mac). */
	if (!key_down(KEY_L))
		return 0;
	key_analog(&ax, &ay);
	if (!key_down_analog(KEY_LEFT, KEY_AXIS_X) &&
	    !key_down_analog(KEY_RIGHT, KEY_AXIS_X) &&
	    !key_down_analog(KEY_UP, KEY_AXIS_Y) &&
	    !key_down_analog(KEY_DOWN, KEY_AXIS_Y))
		return 0;
	px = ((int)ax * (UI_W - 1)) / 255;
	py = ((int)ay * (UI_H - 1)) / 255;
	*out_x = clampi(px, 0, UI_W - 1);
	*out_y = clampi(py, 0, UI_H - 1);
	return 1;
}
