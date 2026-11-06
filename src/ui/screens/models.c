#include "screens.h"
#include "preview.h"
#include "printer.h"
#include "session.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define TILE_COLS 2
#define TILE_ROWS 3
#define TILE_GAP  8
#define TILE_W    ((UI_W - 24 - TILE_GAP) / 2)
#define TILE_H    50

static int s_prog_valid;
static int s_prog_x, s_prog_y, s_prog_w;

static void tile_rect_for(int idx, int scroll_row, UiRect *out)
{
	int row = idx / TILE_COLS - scroll_row;
	int col = idx % TILE_COLS;

	out->x = 12 + col * (TILE_W + TILE_GAP);
	out->y = UI_CONTENT_Y + 8 + row * (TILE_H + TILE_GAP);
	out->w = TILE_W;
	out->h = TILE_H;
}

void models_upload_progress(int done, int total, void *user)
{
	long permille;

	(void)user;
	if (!s_prog_valid)
		return;
	permille = total > 0 ? ((long)done * 1000) / total : 0;
	ui_progress_bar(s_prog_x, s_prog_y, s_prog_w, 6, (int)permille);
}

void models_print_selected(void)
{
	struct session *ss = session_get();
	struct printer *pr = printer_get();
	const struct file_entry *e;
	UiRect r;
	char lab[28];
	int rc;

	if (!ss->confirm_entry.name[0]) return;
	e = &ss->confirm_entry;
	if (!(e->flags & FILE_ON_SD) && (e->flags & FILE_ON_PSP)) {
		tile_rect_for(ss->sel, ss->scroll_row, &r);
		s_prog_valid = 1;
		s_prog_x = r.x + 4;
		s_prog_y = r.y + TILE_H - 10;
		s_prog_w = TILE_W - 8;
	}
	pr->upload_sent = 0;
	pr->upload_total = 0;
	rc = printer_request_print(e);
	if (rc != PRINTER_OK)
		s_prog_valid = 0;
	if (rc == PRINTER_BUSY) {
		session_set_status("busy");
		return;
	}
	if (rc != PRINTER_OK) {
		session_set_statusf("PRINT FAIL: %s", pr->last_error);
		return;
	}

	model_label(e->name, lab, sizeof(lab));
	session_set_statusf("print queued: %s", lab);
}

static void format_size(int bytes, char *out, int outlen)
{
	if (bytes < 0) {
		if (outlen > 0)
			out[0] = 0;
		return;
	}
	if (bytes < 1024 * 1024)
		snprintf(out, outlen, "%dK", (bytes + 512) / 1024);
	else
		snprintf(out, outlen, "%d.%dM", bytes / (1024 * 1024),
			 (bytes % (1024 * 1024)) * 10 / (1024 * 1024));
}

static void draw_grid(void)
{
	struct session *ss = session_get();
	const struct printer *pr = printer_get();
	int row, col, idx;

	for (row = 0; row < TILE_ROWS; row++) {
		for (col = 0; col < TILE_COLS; col++) {
			char label[28], sub[20], sizebuf[12];
			const struct file_entry *e;
			UiRect r;

			idx = (ss->scroll_row + row) * TILE_COLS + col;
			if (idx >= pr->files.count)
				continue;
			e = &pr->files.items[idx];
			tile_rect_for(idx, ss->scroll_row, &r);
			model_label(e->name, label, sizeof(label));
			sub[0] = 0;
			if (idx == ss->sel && ss->sel_size_for == ss->sel && ss->sel_size >= 0) {
				format_size(ss->sel_size, sizebuf, sizeof(sizebuf));
				snprintf(sub, sizeof(sub), "%s", sizebuf);
			}
			ui_draw_tile(r.x, r.y, r.w, r.h, idx == ss->sel, label, sub);
			ui_draw_tile_badge(r.x, r.y, r.w, r.h,
					   ((e->flags & FILE_ON_SD) && (e->flags & FILE_ON_PSP))
						   ? UI_BADGE_NONE
						   : (e->flags & FILE_ON_SD) ? UI_BADGE_SD : UI_BADGE_PSP);
			if (idx == ss->sel_mark)
				ui_text(r.x + TILE_W - 42, r.y + 4, 1, UI_COL_YELLOW, "PRINT");
		}
	}
}

static void models_enter(void)
{
	session_get()->screen = SESSION_SCR_TILES;
}

static void models_input(void)
{
	struct session *ss = session_get();
	struct printer *pr = printer_get();
	int sel_row;

	models_clamp_selection();
	if (key_just(KEY_SELECT) && pr->files.count > 0) {
        if (session_any_busy()) { session_set_status("busy: print not queued"); return; }
        ss->confirm_entry=pr->files.items[ss->sel];
        ss->confirm_upload=0;
		strncpy(ss->confirm_name, pr->files.items[ss->sel].name,
			sizeof(ss->confirm_name) - 1);
		ss->confirm_name[sizeof(ss->confirm_name) - 1] = 0;
		ss->confirm_kind = SESSION_CONFIRM_PRINT;
		ss->sel_mark = -1;
		screen_go(&confirm_screen);
        return;
	}
	if (key_just(KEY_LEFT) && (ss->sel % TILE_COLS) > 0)
		ss->sel--;
	if (key_just(KEY_RIGHT) && (ss->sel % TILE_COLS) < TILE_COLS - 1 &&
	    ss->sel + 1 < pr->files.count)
		ss->sel++;
	if (key_just(KEY_UP) && ss->sel >= TILE_COLS)
		ss->sel -= TILE_COLS;
	if (key_just(KEY_DOWN) && ss->sel + TILE_COLS < pr->files.count)
		ss->sel += TILE_COLS;
	sel_row = pr->files.count > 0 ? ss->sel / TILE_COLS : 0;
	if (sel_row < ss->scroll_row)
		ss->scroll_row = sel_row;
	if (sel_row >= ss->scroll_row + TILE_ROWS)
		ss->scroll_row = sel_row - TILE_ROWS + 1;
	if (pr->files.count > 0 && ss->sel != ss->sel_size_for)
		models_sync_selection_size();
	if (key_just(KEY_CIRCLE)) {
        if (session_any_busy()) { session_set_status("busy: refresh not queued"); return; }
		if (printer_request_files() == PRINTER_OK)
			session_set_status("refresh queued");
		else
			session_set_statusf("list FAIL: %s", pr->last_error);
		models_clamp_selection();
		ss->sel_mark = -1;
		models_sync_selection_size();
		printer_request_status();
	}
	if (key_just(KEY_TRIANGLE)) {
        if (session_any_busy()) { session_set_status("busy: pause/resume not queued"); return; }
		if (ss->ui_paused) {
			if (printer_request_resume() == PRINTER_OK) {
			
				session_set_status("resume queued");
			} else {
				session_set_status("RESUME FAIL");
			}
		} else {
			if (printer_request_pause() == PRINTER_OK) {

				session_set_status("pause queued");
			} else {
				session_set_status("PAUSE FAIL");
			}
		}
	}
	if (key_just(KEY_SQUARE) && pr->files.count > 0) {
        if (session_any_busy()) { session_set_status("busy: delete not queued"); return; }
		strncpy(ss->confirm_name, pr->files.items[ss->sel].name,
			sizeof(ss->confirm_name) - 1);
		ss->confirm_name[sizeof(ss->confirm_name) - 1] = 0;
		ss->confirm_kind = (pr->files.items[ss->sel].flags & FILE_ON_SD)
			? SESSION_CONFIRM_SD : SESSION_CONFIRM_PSP;
		screen_go(&confirm_screen);
	}
}

static void models_draw(void)
{
	struct session *ss = session_get();
	const struct printer *pr = printer_get();
	char right[48];

	ui_draw_tabs(UI_TAB_MODELS, 0);
	if (!ss->did_list)
		ui_text(24, UI_CONTENT_Y + 60, 1, UI_COL_DIM, "loading tiles from printer...");
	else if (pr->files.count == 0)
		ui_text(24, UI_CONTENT_Y + 60, 1, UI_COL_DIM, "(empty)  Refresh to retry");
	else
		draw_grid();
	if (ss->ui_paused)
		snprintf(right, sizeof(right), "PAUSED  %d", pr->files.count);
	else if (pr->status == PRINTER_STATUS_PRINTING)
		snprintf(right, sizeof(right), "PRINT  %d", pr->files.count);
	else
		snprintf(right, sizeof(right), "%d models", pr->files.count);
	if (pr->busy && pr->upload_total > 0) {
        s_prog_x=12; s_prog_y=UI_CONTENT_Y+2; s_prog_w=UI_W-24;
		int fillw = ((s_prog_w - 4) * pr->upload_sent) / pr->upload_total;
		ui_fill_round(s_prog_x, s_prog_y, s_prog_w, 6, UI_COL_PANEL);
		ui_rect(s_prog_x, s_prog_y, s_prog_w, 6, UI_COL_BORDER);
		if (fillw > 0)
			ui_fill(s_prog_x + 2, s_prog_y + 2, fillw, 2, UI_COL_ACCENT);
	} else if (!pr->busy) {
		s_prog_valid = 0;
	}
	ui_draw_status(ss->status_line, right);
	ui_draw_buttons("", "Refresh", "Delete", "Pause");
}

const struct screen models_screen = {
	.name = "models",
	.enter = models_enter,
	.input = models_input,
	.draw = models_draw,
};
