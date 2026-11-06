#ifndef FLSUN_UI_WIDGET_H
#define FLSUN_UI_WIDGET_H

#define UI_TAB_H 30
/* Bottom chrome: status left + compact PSP face-button diamond right. */
#define UI_BTN_H 64
#define UI_STATUS_H 0
#define UI_CONTENT_Y UI_TAB_H
#define UI_CONTENT_H (UI_H - UI_TAB_H - UI_BTN_H)
#define UI_BTN_Y (UI_H - UI_BTN_H)
/* Soft-button diamond: wide for labels, short under tiles. */
#define UI_BB_W 200
#define UI_BB_H 62
#define UI_BB_X (UI_W - 4 - UI_BB_W)
#define UI_BB_Y (UI_H - 2 - UI_BB_H)
#define UI_BB_R 7
#define UI_BB_ARM 15
/* Face-button colors (ABGR), classic Sony accents. */
#define UI_COL_BTN_TRI   0xFF60C070u
#define UI_COL_BTN_CIR   0xFF5050E0u
#define UI_COL_BTN_CRO   0xFFE09050u
#define UI_COL_BTN_SQR   0xFFC060C0u

enum {
	UI_TAB_MODELS = 0,
	UI_TAB_TUNE = 1,
	UI_TAB_DASH = 2,
	UI_TAB_COUNT = 3
};

typedef struct {
	int x, y, w, h;
} UiRect;

void ui_draw_tabs(int active_tab, int load_mode);
void ui_draw_status(const char *left, const char *right);
void ui_draw_buttons(const char *a, const char *b, const char *c, const char *d);
void ui_draw_tile(int x, int y, int w, int h, int selected,
		  const char *title, const char *subtitle);

#define UI_BADGE_NONE 0
#define UI_BADGE_SD   1
#define UI_BADGE_PSP  2
void ui_draw_tile_badge(int x, int y, int w, int h, int kind);
void ui_draw_tune_row(int y, int selected, const char *label, const char *value);
void ui_draw_modal(const char *title, const char *body, const char *hint);
int ui_tab_at(int px, int py);
int ui_button_at(int px, int py);

#endif
