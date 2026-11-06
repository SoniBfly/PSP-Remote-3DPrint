/*
 * Stage 4: FLPV preview load + soft blit to PSP framebuffer.
 * Format: "FLPV" + u16le w,h + RGB565 pixels top-down.
 * Companions: foo.flpv (no supports / primary), foo_sup.flpv (with supports).
 */
#include "preview.h"
#include "config.h"
#include "model_storage.h"

#include <pspdisplay.h>
#include <pspiofilemgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resolve <slug>.gcode → models/<slug>/preview.flpv (or preview_sup.flpv). */
static int preview_slug(const char *name, char *slug)
{
    ModelFileKind kind;
    return model_storage_resolve_legacy_name(name,slug,MODEL_STORAGE_SLUG_LEN,&kind);
}

void preview_clear(PreviewImage *img)
{
	if (!img)
		return;
	if (img->pix) {
		free(img->pix);
		img->pix = NULL;
	}
	img->loaded = 0;
	img->w = img->h = 0;
	img->name[0] = 0;
	img->path[0] = 0;
	img->err[0] = 0;
}

int preview_exists_for_gcode(const char *name,int sup)
{
    char slug[MODEL_STORAGE_SLUG_LEN];
    if (preview_slug(name,slug)) return 0;
    return model_storage_artifact_exists_try(slug,sup ? MODEL_FILE_PREVIEW_SUP : MODEL_FILE_PREVIEW);
}

/* Retained for source compatibility; deletion is owned by printer/model_storage. */
void preview_delete_for_gcode(const char *name) { (void)name; }

static unsigned short read_u16le(const unsigned char *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

int preview_load_for_gcode(const char *gcode_name, PreviewImage *img, int with_supports)
{
	char slug[MODEL_STORAGE_SLUG_LEN];
    int size;
	unsigned char *buf = NULL;
	int n, need, w, h, i;
	unsigned short *pix;

	preview_clear(img);
	if (!gcode_name || !gcode_name[0]) {
		snprintf(img->err, sizeof(img->err), "no name");
		return -1;
	}
	strncpy(img->name, gcode_name, sizeof(img->name) - 1);
    if (preview_slug(gcode_name,slug)) {
        snprintf(img->err,sizeof(img->err),"unsupported name"); return -1;
    }
    n=model_storage_read_try(slug,with_supports ? MODEL_FILE_PREVIEW_SUP : MODEL_FILE_PREVIEW,
                             (char **)&buf,&size);
    if (n<0 || n<8 || n>8+PREV_BUF_PIXELS*2) {
        free(buf);
        snprintf(img->err,sizeof(img->err),n==-6 ? "storage busy; retry" : "no valid preview");
        return -1;
    }
	if (buf[0] != 'F' || buf[1] != 'L' || buf[2] != 'P' || buf[3] != 'V') {
		free(buf);
		snprintf(img->err, sizeof(img->err), "bad magic");
		return -1;
	}
	w = (int)read_u16le(buf + 4);
	h = (int)read_u16le(buf + 6);
	if (w < 1 || h < 1 || w > PREV_MAX_W || h > PREV_MAX_H) {
		free(buf);
		snprintf(img->err, sizeof(img->err), "bad %dx%d", w, h);
		return -1;
	}
	need = 8 + w * h * 2;
	if (n < need) {
		free(buf);
		snprintf(img->err, sizeof(img->err), "truncated flpv");
		return -1;
	}
	pix = (unsigned short *)malloc((size_t)(w * h * 2));
	if (!pix) {
		free(buf);
		snprintf(img->err, sizeof(img->err), "oom pix");
		return -1;
	}
	for (i = 0; i < w * h; i++)
		pix[i] = read_u16le(buf + 8 + i * 2);
	free(buf);

	img->pix = pix;
	img->w = w;
	img->h = h;
	img->loaded = 1;
	img->err[0] = 0;
	return 0;
}

static unsigned int rgb565_to_8888(unsigned short c)
{
	unsigned int r = (c >> 11) & 0x1F;
	unsigned int g = (c >> 5) & 0x3F;
	unsigned int b = c & 0x1F;
	r = (r << 3) | (r >> 2);
	g = (g << 2) | (g >> 4);
	b = (b << 3) | (b >> 2);
	/* pspDebugScreen framebuffer is typically ABGR 8888 */
	return 0xFF000000u | (b << 16) | (g << 8) | r;
}

int preview_blit(const PreviewImage *img, int dst_x, int dst_y)
{
	void *top = NULL;
	int bufw = 0, fmt = 0;
	int x, y, dw, dh;
	int max_w = 480, max_h = 272;

	if (!img || !img->loaded || !img->pix)
		return -1;
	if (sceDisplayGetFrameBuf(&top, &bufw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 ||
	    !top || bufw <= 0)
		return -1;

	dw = img->w;
	dh = img->h;
	if (dst_x < 0) {
		dw += dst_x;
		dst_x = 0;
	}
	if (dst_y < 0) {
		dh += dst_y;
		dst_y = 0;
	}
	if (dst_x + dw > max_w)
		dw = max_w - dst_x;
	if (dst_y + dh > max_h)
		dh = max_h - dst_y;
	if (dw <= 0 || dh <= 0)
		return -1;

	if (fmt == PSP_DISPLAY_PIXEL_FORMAT_8888) {
		unsigned int *fb = (unsigned int *)top;
		for (y = 0; y < dh; y++) {
			const unsigned short *src = img->pix + y * img->w;
			unsigned int *dst = fb + (dst_y + y) * bufw + dst_x;
			for (x = 0; x < dw; x++)
				dst[x] = rgb565_to_8888(src[x]);
		}
		return 0;
	}
	if (fmt == PSP_DISPLAY_PIXEL_FORMAT_565) {
		unsigned short *fb = (unsigned short *)top;
		for (y = 0; y < dh; y++) {
			const unsigned short *src = img->pix + y * img->w;
			unsigned short *dst = fb + (dst_y + y) * bufw + dst_x;
			memcpy(dst, src, (size_t)dw * 2);
		}
		return 0;
	}
	return -1;
}
