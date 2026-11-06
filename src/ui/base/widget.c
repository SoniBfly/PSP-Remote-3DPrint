#include "widget.h"
#include "render.h"
#include "text.h"

void ui_draw_tabs(int active_tab, int load_mode)
{
	static const char *names[UI_TAB_COUNT] = {
		"Models", "Tune", "Dash"
	};
	int i, tw, x, tx;
	unsigned int bg, fg;
	char badge[8];

	ui_fill(0, 0, UI_W, UI_TAB_H, UI_COL_PANEL);
	ui_fill(0, UI_TAB_H - 1, UI_W, 1, UI_COL_BORDER);
	tw = UI_W / UI_TAB_COUNT;
	(void)load_mode;
	(void)badge;
	for (i = 0; i < UI_TAB_COUNT; i++) {
		x = i * tw;
		bg = (i == active_tab) ? UI_COL_TAB_ON : UI_COL_TAB;
		ui_fill_round(x + 2, 3, tw - 4, UI_TAB_H - 6, bg);
		if (i == active_tab)
			ui_rect(x + 2, 3, tw - 4, UI_TAB_H - 6, UI_COL_ACCENT);
		fg = (i == active_tab) ? UI_COL_TEXT : UI_COL_DIM;
		tx = x + (tw - ui_text_width(1, names[i])) / 2;
		if (tx < x + 4)
			tx = x + 4;
		ui_text(tx, 7, 1, fg, names[i]);
	}
}

static void ui_circle_fill(int cx, int cy, int r, unsigned int abgr)
{
	int x, y, r2 = r * r;

	for (y = -r; y <= r; y++)
		for (x = -r; x <= r; x++)
			if (x * x + y * y <= r2)
				ui_pixel(cx + x, cy + y, abgr);
}

static void ui_circle_ring(int cx, int cy, int r, int thick, unsigned int abgr)
{
	int x, y, r2 = r * r, ri = r - thick, ri2 = ri > 0 ? ri * ri : 0;

	for (y = -r; y <= r; y++)
		for (x = -r; x <= r; x++) {
			int d = x * x + y * y;
			if (d <= r2 && d >= ri2)
				ui_pixel(cx + x, cy + y, abgr);
		}
}

static void ui_line(int x0, int y0, int x1, int y1, unsigned int abgr)
{
	int dx = x1 - x0, dy = y1 - y0;
	int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
	int err = ax - ay, e2, n;

	for (n = 0; n < 128; n++) {
		ui_pixel(x0, y0, abgr);
		if (x0 == x1 && y0 == y1)
			break;
		e2 = err * 2;
		if (e2 > -ay) {
			err -= ay;
			x0 += sx;
		}
		if (e2 < ax) {
			err += ax;
			y0 += sy;
		}
	}
}

/* Face icons: 0=× Cross, 1=○ Circle, 2=□ Square, 3=△ Triangle */
static void ui_face_icon(int kind, int cx, int cy, int r, unsigned int col)
{
	int s = r > 4 ? r - 2 : r - 1;
	if (s < 2)
		s = 2;

	ui_circle_fill(cx, cy, r, UI_COL_BTN);
	ui_circle_ring(cx, cy, r, 1, UI_COL_BORDER);
	if (kind == 1) {
		ui_circle_ring(cx, cy, s, 2, col);
	} else if (kind == 0) {
		ui_line(cx - s, cy - s, cx + s, cy + s, col);
		ui_line(cx - s + 1, cy - s, cx + s, cy + s - 1, col);
		ui_line(cx + s, cy - s, cx - s, cy + s, col);
		ui_line(cx + s - 1, cy - s, cx - s, cy + s - 1, col);
	} else if (kind == 2) {
		ui_rect(cx - s, cy - s, s * 2 + 1, s * 2 + 1, col);
		if (s > 2)
			ui_rect(cx - s + 1, cy - s + 1, (s - 1) * 2 + 1, (s - 1) * 2 + 1, col);
	} else if (kind == 3) {
		ui_line(cx, cy - s, cx - s, cy + s, col);
		ui_line(cx, cy - s, cx + s, cy + s, col);
		ui_line(cx - s, cy + s, cx + s, cy + s, col);
		ui_line(cx, cy - s + 1, cx - s + 1, cy + s - 1, col);
		ui_line(cx, cy - s + 1, cx + s - 1, cy + s - 1, col);
	}
}

/* Centers of diamond: 0=× bottom, 1=○ right, 2=□ left, 3=△ top */
static void ui_bb_centers(int *cx, int *cy)
{
	int mid_x = UI_BB_X + UI_BB_W / 2;
	int mid_y = UI_BB_Y + UI_BB_H / 2;
	int arm = UI_BB_ARM;

	cx[0] = mid_x;
	cy[0] = mid_y + arm;
	cx[1] = mid_x + arm;
	cy[1] = mid_y;
	cx[2] = mid_x - arm;
	cy[2] = mid_y;
	cx[3] = mid_x;
	cy[3] = mid_y - arm;
}

void ui_draw_status(const char *left, const char *right)
{
	int y = UI_BTN_Y;
	int maxc, i;
	char buf[40];

	ui_fill(0, y, UI_BB_X - 4, 1, UI_COL_BORDER);
	if (left && left[0]) {
		maxc = (UI_BB_X - 16) / 8;
		if (maxc > 36)
			maxc = 36;
		for (i = 0; left[i] && i < maxc; i++)
			buf[i] = left[i];
		buf[i] = 0;
		ui_text(8, y + 8, 1, UI_COL_TEXT, buf);
	}
	if (right && right[0]) {
		maxc = (UI_BB_X - 16) / 8;
		if (maxc > 36)
			maxc = 36;
		for (i = 0; right[i] && i < maxc; i++)
			buf[i] = right[i];
		buf[i] = 0;
		ui_text(8, y + 24, 1, UI_COL_DIM, buf);
	}
}

void ui_draw_buttons(const char *a, const char *b, const char *c, const char *d)
{
	const char *labs[4];
	unsigned int cols[4] = {
		UI_COL_BTN_CRO, UI_COL_BTN_CIR, UI_COL_BTN_SQR, UI_COL_BTN_TRI
	};
	int kinds[4] = { 0, 1, 2, 3 };
	int cx[4], cy[4];
	int i, r = UI_BB_R, tw, lx, ly, j, maxc;
	char lbuf[10];

	labs[0] = a;
	labs[1] = b;
	labs[2] = c;
	labs[3] = d;

	ui_fill_round(UI_BB_X, UI_BB_Y, UI_BB_W, UI_BB_H, UI_COL_PANEL);
	ui_rect(UI_BB_X, UI_BB_Y, UI_BB_W, UI_BB_H, UI_COL_BORDER);
	ui_bb_centers(cx, cy);

	for (i = 0; i < 4; i++) {
		if (!labs[i] || !labs[i][0])
			continue;
		ui_face_icon(kinds[i], cx[i], cy[i], r, cols[i]);
		maxc = 8;
		for (j = 0; labs[i][j] && j < maxc; j++)
			lbuf[j] = labs[i][j];
		lbuf[j] = 0;
		tw = ui_text_width(1, lbuf);
		if (i == 3) {
			lx = cx[i] - tw / 2;
			ly = cy[i] - r - 10;
		} else if (i == 0) {
			lx = cx[i] - tw / 2;
			ly = cy[i] + r + 2;
		} else if (i == 2) {
			lx = cx[i] - r - tw - 2;
			ly = cy[i] - 4;
		} else {
			lx = cx[i] + r + 2;
			ly = cy[i] - 4;
		}
		if (lx < UI_BB_X + 2)
			lx = UI_BB_X + 2;
		if (lx + tw > UI_BB_X + UI_BB_W - 2)
			lx = UI_BB_X + UI_BB_W - 2 - tw;
		if (ly < UI_BB_Y + 2)
			ly = UI_BB_Y + 2;
		if (ly > UI_BB_Y + UI_BB_H - 10)
			ly = UI_BB_Y + UI_BB_H - 10;
		ui_text(lx, ly, 1, UI_COL_DIM, lbuf);
	}
}

void ui_draw_tile(int x, int y, int w, int h, int selected,
		  const char *title, const char *subtitle)
{
	unsigned int bg = selected ? UI_COL_TILE_SEL : UI_COL_TILE;
	unsigned int border = selected ? UI_COL_ACCENT : UI_COL_BORDER;
	char tbuf[28];
	int i;

	ui_fill_round(x, y, w, h, bg);
	ui_rect(x, y, w, h, border);
	if (selected)
		ui_fill(x + 2, y + 2, 4, h - 4, UI_COL_ACCENT);

	if (title) {
		int maxc = (w - 24) / 8;
		if (maxc > 26)
			maxc = 26;
		if (maxc < 4)
			maxc = 4;
		for (i = 0; title[i] && i < maxc; i++)
			tbuf[i] = title[i];
		tbuf[i] = 0;
		ui_text(x + 12, y + (subtitle && subtitle[0] ? 6 : 14), 1, UI_COL_TEXT, tbuf);
	}
	if (subtitle && subtitle[0])
		ui_text(x + 12, y + h - 16, 1, UI_COL_DIM, subtitle);
}

void ui_draw_tile_badge(int x, int y, int w, int h, int kind)
{
	if (kind == UI_BADGE_SD) {
		int x1 = x + w - 3, y1 = y + h - 3;
		int x0 = x1 - 12, y0 = y1 - 10;
		int yy;

		for (yy = y0; yy <= y1; yy++) {
			int span = ((yy - y0) * (x1 - x0)) / (y1 - y0);
			ui_fill(x1 - span, yy, span + 1, 1, UI_COL_YELLOW);
		}
	} else if (kind == UI_BADGE_PSP) {
		int cx = x + w - 17, cy = y + h - 11;

		ui_circle_fill(cx - 5, cy, 4, UI_COL_DIM);
		ui_circle_fill(cx, cy - 3, 5, UI_COL_DIM);
		ui_circle_fill(cx + 5, cy, 4, UI_COL_DIM);
		ui_fill(cx - 9, cy, 19, 6, UI_COL_DIM);
	}
}

void ui_draw_tune_row(int y, int selected, const char *label, const char *value)
{
	unsigned int bg = selected ? UI_COL_TILE_SEL : UI_COL_TILE;
	unsigned int border = selected ? UI_COL_ACCENT : UI_COL_BORDER;
	int x = 12, w = UI_W - 24, h = 28;

	ui_fill_round(x, y, w, h, bg);
	ui_rect(x, y, w, h, border);
	if (selected)
		ui_fill(x + 2, y + 2, 4, h - 4, UI_COL_ACCENT);
	ui_text(x + 14, y + 6, 1, UI_COL_TEXT, label ? label : "");
	if (value) {
		int tw = ui_text_width(1, value);
		ui_text(x + w - 14 - tw, y + 6, 1, selected ? UI_COL_ACCENT : UI_COL_DIM,
			value);
	}
}

void ui_draw_modal(const char *title, const char *body, const char *hint)
{
	int x = 40, y = 50, w = 400, h = 140;

	ui_fill(0, UI_TAB_H, UI_W, UI_H - UI_TAB_H - UI_BTN_H, 0xCC101018u);
	ui_fill_round(x, y, w, h, UI_COL_PANEL);
	ui_rect(x, y, w, h, UI_COL_ACCENT);
	if (title)
		ui_text(x + 20, y + 20, 1, UI_COL_TEXT, title);
	if (body)
		ui_text(x + 20, y + 56, 1, UI_COL_DIM, body);
	if (hint)
		ui_text(x + 20, y + 100, 1, UI_COL_ACCENT, hint);
}

int ui_tab_at(int px, int py)
{
	int tw;

	if (py < 0 || py >= UI_TAB_H || px < 0 || px >= UI_W)
		return -1;
	tw = UI_W / UI_TAB_COUNT;
	return px / tw;
}

int ui_button_at(int px, int py)
{
	int cx[4], cy[4], i, dx, dy, best = -1, best_d, d, hit_r2;

	if (px < UI_BB_X || px >= UI_BB_X + UI_BB_W ||
	    py < UI_BB_Y || py >= UI_BB_Y + UI_BB_H)
		return -1;
	ui_bb_centers(cx, cy);
	hit_r2 = (UI_BB_R + 4) * (UI_BB_R + 4);
	best_d = hit_r2 + 1;
	for (i = 0; i < 4; i++) {
		dx = px - cx[i];
		dy = py - cy[i];
		d = dx * dx + dy * dy;
		if (d <= hit_r2 && d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}
