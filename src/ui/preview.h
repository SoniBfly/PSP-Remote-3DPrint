/*
 * Browser-generated FLPV preview (RGB565) for a model G-code name.
 */
#ifndef FLSUN_PREVIEW_H
#define FLSUN_PREVIEW_H

#define PREV_MAX_W 256
#define PREV_MAX_H 220
#define PREV_BUF_PIXELS (256 * 220)

typedef struct {
	int loaded;
	int w;
	int h;
	char name[64];       /* gcode basename for UI */
	char path[160];      /* flpv path */
	unsigned short *pix; /* RGB565, malloc'd, w*h */
	char err[96];
} PreviewImage;

void preview_clear(PreviewImage *img);
/*
 * Load companion preview for inbox gcode.
 * with_supports=0 → foo.flpv; =1 → foo_sup.flpv.
 */
int  preview_load_for_gcode(const char *gcode_name, PreviewImage *img, int with_supports);
/* Soft-blit into current display framebuffer; returns 0 if drawn. */
int  preview_blit(const PreviewImage *img, int dst_x, int dst_y);
/* True if companion flpv exists (with_supports selects foo vs foo_sup). */
int  preview_exists_for_gcode(const char *gcode_name, int with_supports);
/* Delete both foo.flpv and foo_sup.flpv (best-effort). */
void preview_delete_for_gcode(const char *gcode_name);

#endif
