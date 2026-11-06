#include "screens.h"
#include "printer.h"
#include "session.h"
#include "ui.h"

#include <stdio.h>

static void confirm_enter(void)
{
	session_get()->screen = SESSION_SCR_CONFIRM;
}

static void confirm_input(void)
{
	struct session *ss = session_get();
	struct printer *pr = printer_get();

	if (ss->confirm_kind == SESSION_CONFIRM_PRINT) {
		if (key_just(KEY_SQUARE)) {
            if (session_any_busy()) { session_set_status("busy: print not queued"); return; }
            if (ss->confirm_upload) {
                if (printer_request_upload(ss->confirm_name,1)==PRINTER_OK)
                    session_set_status("upload + print queued");
                else session_set_statusf("queue FAIL: %s",pr->last_error);
            } else models_print_selected();
			screen_go(&models_screen);
			ss->confirm_name[0] = 0;
		} else if (key_just(KEY_CROSS) || key_just(KEY_CIRCLE) || key_just(KEY_SELECT)) {
			screen_go(&models_screen);
			ss->confirm_name[0] = 0;
			session_set_status("print cancelled");
		}
		return;
	}
	if (key_just(KEY_CROSS)) {
        if (session_any_busy()) { session_set_status("busy: action not queued"); return; }
		if (ss->confirm_kind == SESSION_CONFIRM_HOME) {
			if (printer_request_home() == PRINTER_OK)
				session_set_status("G28 queued");
			else
				session_set_statusf("G28 FAIL: %s", pr->last_error);
			screen_go(&tune_screen);
		} else if (ss->confirm_kind == SESSION_CONFIRM_PSP) {
			if (printer_request_delete_psp(ss->confirm_name) == PRINTER_OK) {
				models_clamp_selection();
				ss->sel_mark = -1;
				models_sync_selection_size();
				session_set_status("delete PSP queued");
			} else {
				session_set_statusf("del FAIL: %s", pr->last_error);
			}
			screen_go(&models_screen);
		} else {
			if (printer_request_delete_sd(ss->confirm_name) == PRINTER_OK) {
				models_clamp_selection();
				ss->sel_mark = -1;
				models_sync_selection_size();
				session_set_status("delete SD queued");
			} else {
				session_set_statusf("del FAIL: %s", pr->last_error);
			}
			screen_go(&models_screen);
		}
		ss->confirm_name[0] = 0;
	}
	if (key_just(KEY_CIRCLE) || key_just(KEY_SELECT)) {
		if (ss->confirm_kind == SESSION_CONFIRM_HOME)
			screen_go(&tune_screen);
		else
			screen_go(&models_screen);
		ss->confirm_name[0] = 0;
		session_set_status("cancelled");
	}
}

static void confirm_draw(void)
{
	struct session *ss = session_get();
	const struct file_entry *selected = NULL;

	ui_draw_tabs(ss->confirm_kind == SESSION_CONFIRM_HOME ? UI_TAB_TUNE : UI_TAB_MODELS, 0);
	if (ss->confirm_kind == SESSION_CONFIRM_PRINT) {
		char label[28], body[64];
		const char *source = "PSP";
		selected = &ss->confirm_entry;
		if (selected) {
			if ((selected->flags & FILE_ON_SD) && (selected->flags & FILE_ON_PSP))
				source = "SD + PSP";
			else if (selected->flags & FILE_ON_SD)
				source = "SD";
		}
		model_label(ss->confirm_name, label, sizeof(label));
		snprintf(body, sizeof(body), "%s  [%s]", label, source);
		ui_draw_modal("Start print?", body, "[SQ] PRINT     [X] cancel");
		ui_draw_status(ss->status_line, "");
		ui_draw_buttons("cancel", "", "PRINT", "");
		return;
	}
	if (ss->confirm_kind == SESSION_CONFIRM_HOME) {
		ui_draw_modal("Home G28?", "Printer will MOVE all axes", "[X] YES     [O] cancel");
	} else {
		char label[28], body[64];
		model_label(ss->confirm_name, label, sizeof(label));
		snprintf(body, sizeof(body), "%s", label);
		ui_draw_modal(ss->confirm_kind == SESSION_CONFIRM_PSP ? "Delete inbox?" : "Delete SD?",
			      body, "[X] YES delete     [O] cancel");
	}
	ui_draw_status(ss->status_line, "");
	ui_draw_buttons("YES", "cancel", "", "");
}

const struct screen confirm_screen = {
	.name = "confirm",
	.enter = confirm_enter,
	.input = confirm_input,
	.draw = confirm_draw,
};
