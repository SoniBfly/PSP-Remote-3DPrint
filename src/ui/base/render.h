#ifndef FLSUN_UI_RENDER_H
#define FLSUN_UI_RENDER_H

#define UI_W 480
#define UI_H 272

/* ABGR8888 (matches preview_blit / PSP FB) */
#define UI_COL_BG        0xFF1A1420u
#define UI_COL_PANEL     0xFF2A2438u
#define UI_COL_TILE      0xFF3A3450u
#define UI_COL_TILE_SEL  0xFF5A4A28u
#define UI_COL_ACCENT    0xFFD8B040u
#define UI_COL_TAB       0xFF28243Au
#define UI_COL_TAB_ON    0xFF484060u
#define UI_COL_BTN       0xFF34304Cu
#define UI_COL_BTN_HOT   0xFF504878u
#define UI_COL_TEXT      0xFFF0F0F0u
#define UI_COL_DIM       0xFFA0A0B0u
#define UI_COL_OK        0xFF60C080u
#define UI_COL_WARN      0xFF5090E0u
#define UI_COL_YELLOW    0xFF00E0F0u
#define UI_COL_DANGER    0xFF5050E0u
#define UI_COL_BORDER    0xFF605878u

void ui_begin(void);
void ui_end(void);
void ui_begin_direct(void);
void ui_end_direct(void);
void ui_fill(int x, int y, int w, int h, unsigned int abgr);
void ui_rect(int x, int y, int w, int h, unsigned int abgr);
void ui_fill_round(int x, int y, int w, int h, unsigned int abgr);
void ui_pixel(int x, int y, unsigned int abgr);
int ui_pointer(int *out_x, int *out_y);
int ui_dump_bmp(const char *path);

#endif
