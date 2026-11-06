#include "screens.h"
#include "preview.h"
#include "printer.h"
#include "session.h"
#include "ui.h"

#include <pspkernel.h>
#include <stdio.h>
#include <string.h>

static PreviewImage s_preview;

void preview_open(const char *gcode_name)
{
	struct session *ss = session_get();

	session_set_busy(1);
	strncpy(ss->preview_gcode, gcode_name, sizeof(ss->preview_gcode) - 1);
	ss->preview_gcode[sizeof(ss->preview_gcode) - 1] = 0;
	ss->preview_has_sup = preview_exists_for_gcode(ss->preview_gcode, 1);
	ss->preview_supports = 0;
	if (preview_load_for_gcode(ss->preview_gcode, &s_preview, 0) != 0 && ss->preview_has_sup) {
		ss->preview_supports = 1;
		preview_load_for_gcode(ss->preview_gcode, &s_preview, 1);
	}
	if (s_preview.loaded) {
		screen_go(&preview_screen);
		session_set_status(ss->preview_supports ? "preview supports" : "preview");
	} else {
		session_set_status(s_preview.err[0] ? s_preview.err : "no preview");
	}
	session_set_busy(0);
}

static void preview_enter(void)
{
	session_get()->screen = SESSION_SCR_PREVIEW;
}

static void preview_leave(void)
{
	struct session *ss = session_get();

	preview_clear(&s_preview);
	ss->preview_gcode[0] = 0;
}

static void preview_input(void)
{
	struct session *ss = session_get();
	struct printer *pr = printer_get();

	if (key_just(KEY_CIRCLE) || key_just(KEY_SELECT)) {
		screen_go(&models_screen);
		session_set_status("models");
        return;
	}
	if (ss->preview_has_sup &&
	    (key_just(KEY_LEFT) || key_just(KEY_RIGHT) || key_just(KEY_SQUARE))) {
		session_set_busy(1);
		ss->preview_supports = !ss->preview_supports;
		if (preview_load_for_gcode(ss->preview_gcode, &s_preview, ss->preview_supports) != 0) {
			ss->preview_supports = !ss->preview_supports;
			preview_load_for_gcode(ss->preview_gcode, &s_preview, ss->preview_supports);
			session_set_status("toggle fail");
		} else {
			session_set_status(ss->preview_supports ? "supports ON" : "supports OFF");
		}
		session_set_busy(0);
	}
	if (key_just(KEY_CROSS) && ss->preview_gcode[0]) {
        if (session_any_busy()) { session_set_status("busy: upload not queued"); return; }
		if (printer_request_upload(ss->preview_gcode, 0) == PRINTER_OK)
			session_set_statusf("upload queued: %s", ss->preview_gcode);
		else
			session_set_statusf("up FAIL: %s", pr->last_error);
		models_sync_selection_size();
	}
    if (key_just(KEY_TRIANGLE) && ss->preview_gcode[0]) {
        if (session_any_busy()) { session_set_status("busy: print not queued"); return; }
        snprintf(ss->confirm_name,sizeof(ss->confirm_name),"%s",ss->preview_gcode);
        memset(&ss->confirm_entry,0,sizeof(ss->confirm_entry));
        snprintf(ss->confirm_entry.name,sizeof(ss->confirm_entry.name),"%s",ss->preview_gcode);
        ss->confirm_entry.flags=FILE_ON_PSP;
        ss->confirm_kind=SESSION_CONFIRM_PRINT;
        ss->confirm_upload=1;
        screen_go(&confirm_screen);
        return;
    }
}

static void preview_draw(void)
{
	struct session *ss = session_get();
	char label[28], leftstat[96];

	ui_draw_tabs(UI_TAB_MODELS, 0);
	if (s_preview.loaded) {
		int dx = (UI_W - s_preview.w) / 2;
		int dy = UI_CONTENT_Y + 4;
		if (dx < 0)
			dx = 0;
		preview_blit(&s_preview, dx, dy);
	} else {
		ui_text(24, UI_CONTENT_Y + 40, 1, UI_COL_DIM,
			s_preview.err[0] ? s_preview.err : "no image — re-Send via Load");
	}
	model_label(ss->preview_gcode, label, sizeof(label));
	snprintf(leftstat, sizeof(leftstat), "%s%s", label,
		 ss->preview_has_sup ? (ss->preview_supports ? "  [SUP]" : "  [NOSUP]") : "");
	ui_draw_status(leftstat, ss->status_line);
	ui_draw_buttons("Upload", "Back", ss->preview_has_sup ? "SUP" : "", "Up+Print");
}

const struct screen preview_screen = {
	.name = "preview",
	.enter = preview_enter,
	.input = preview_input,
	.draw = preview_draw,
	.leave = preview_leave,
};
