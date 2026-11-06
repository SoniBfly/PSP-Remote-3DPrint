#include "screens.h"
#include "config.h"
#include "printer.h"
#include "session.h"
#include "ui.h"

#include <stdio.h>

static const struct dash_ops *s_ops;
static int s_frames;

void dash_set_ops(const struct dash_ops *ops)
{
	s_ops = ops;
}

static void dash_enter(void)
{
	session_get()->screen = SESSION_SCR_DASH;
}

static void dash_input(void)
{
	struct session *ss = session_get();

	s_frames++;
	if (!session_any_busy() && s_ops && (s_frames % 120) == 0)
		s_ops->refresh_local_ip();
	if (!s_ops)
		return;
	if (key_just(KEY_SELECT)) { screen_go(&models_screen); return; }
	if (key_just(KEY_LEFT) && ss->ip_octet > 0)
		ss->ip_octet--;
	if (key_just(KEY_RIGHT) && ss->ip_octet < 3)
		ss->ip_octet++;
	if (!session_any_busy() && key_just(KEY_UP))
		cfg_bump_printer_ip_octet_at(ss->ip_octet, +1);
	if (!session_any_busy() && key_just(KEY_DOWN))
		cfg_bump_printer_ip_octet_at(ss->ip_octet, -1);
	if (key_just(KEY_CROSS)) {
        if (session_any_busy()) { session_set_status("busy: M105 not queued"); return; }
		if (printer_request_temps() == PRINTER_OK)
			session_set_status("M105 queued");
		else
			session_set_status("M105 FAIL");
		printer_request_status();
	}
	if (key_just(KEY_CIRCLE)) {
		if (s_ops->http_listening())
			s_ops->http_stop();
		else
			s_ops->http_start();
	}
	if (key_just(KEY_SQUARE)) {
		if (cfg_save_force() == 0) {
			s_ops->apply_log_config();
			session_set_status("cfg saved");
		} else {
			session_set_status("cfg SAVE FAIL");
		}
	}
	if (key_just(KEY_TRIANGLE)) {
		screen_go(&tune_screen);
		session_set_status("tune");
	}
}

static void draw_printer_ip(int x, int y, int selected)
{
	unsigned o[4];
	char part[8];
	int i;

	if (sscanf(cfg_printer_ip(), "%u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) != 4) {
		ui_text(x, y, 1, UI_COL_TEXT, cfg_printer_ip());
		return;
	}
	for (i = 0; i < 4; i++) {
		snprintf(part, sizeof(part), "%u", o[i]);
		ui_text(x, y, 1, i == selected ? UI_COL_DIM : UI_COL_TEXT, part);
		x += ui_text_width(1, part);
		if (i < 3) {
			ui_text(x, y, 1, UI_COL_TEXT, ".");
			x += ui_text_width(1, ".");
		}
	}
}

static void dash_draw(void)
{
	struct session *ss = session_get();
	const struct printer *pr = printer_get();
	char valbuf[24], right[48];

	ui_draw_tabs(UI_TAB_DASH, 0);
	ui_fill_round(12, UI_CONTENT_Y + 8, UI_W - 24, 120, UI_COL_PANEL);
	ui_rect(12, UI_CONTENT_Y + 8, UI_W - 24, 120, UI_COL_BORDER);
	ui_text(24, UI_CONTENT_Y + 20, 1, UI_COL_DIM, "printer");
	draw_printer_ip(120, UI_CONTENT_Y + 20, ss->ip_octet);
	ui_text(24, UI_CONTENT_Y + 40, 1, UI_COL_DIM, "local");
	ui_text(120, UI_CONTENT_Y + 40, 1, UI_COL_TEXT, s_ops ? s_ops->local_ip() : "");
	if (pr->temps_ok) {
		snprintf(valbuf, sizeof(valbuf), "T %.0f/%.0f", pr->hotend, pr->hotend_target);
		ui_text(24, UI_CONTENT_Y + 64, 1, UI_COL_TEXT, valbuf);
		snprintf(valbuf, sizeof(valbuf), "B %.0f/%.0f", pr->bed, pr->bed_target);
		ui_text(160, UI_CONTENT_Y + 64, 1, UI_COL_TEXT, valbuf);
	} else {
		ui_text(24, UI_CONTENT_Y + 64, 1, UI_COL_DIM, "temps: press M105");
	}
	snprintf(valbuf, sizeof(valbuf), "print: %s", ss->ui_paused ? "PAUSED" :
		 (pr->status == PRINTER_STATUS_PRINTING ? "ACTIVE" : "idle"));
	ui_text(24, UI_CONTENT_Y + 84, 1, UI_COL_TEXT, valbuf);
	ui_text(24, UI_CONTENT_Y + 104, 1, UI_COL_DIM, s_ops ? s_ops->http_status() : "");
	snprintf(right, sizeof(right), "preset %d/%d  IP part %d/4", cfg_pla_hotend(),
		 cfg_pla_bed(), ss->ip_octet + 1);
	ui_draw_status(ss->status_line, right);
	ui_draw_buttons("UD edit", "HTTP", "Save", "Tune");
}

const struct screen dash_screen = {
	.name = "dash",
	.enter = dash_enter,
	.input = dash_input,
	.draw = dash_draw,
};
