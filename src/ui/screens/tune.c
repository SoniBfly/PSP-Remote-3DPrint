#include "screens.h"
#include "config.h"
#include "printer.h"
#include "session.h"
#include "ui.h"

#include <stdio.h>

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static void apply_heat(void)
{
	struct printer *pr = printer_get();
	int hot = clamp_int(cfg_pla_hotend(), 0, 280);
	int bed = clamp_int(cfg_pla_bed(), 0, 110);

	cfg_set_pla_hotend(hot);
	cfg_set_pla_bed(bed);
	if (printer_request_set_hotend(hot) != PRINTER_OK) {
		session_set_statusf("M104 FAIL: %s", pr->last_error);
		return;
	}
	if (printer_request_set_bed(bed) != PRINTER_OK) {
		session_set_statusf("M140 FAIL: %s", pr->last_error);
		return;
	}
	cfg_save_if_dirty();
	session_set_statusf("heat queued %d/%d", hot, bed);
}

static void heat_off(void)
{
	struct printer *pr = printer_get();

	if (printer_request_set_hotend(0) != PRINTER_OK) {
		session_set_statusf("off FAIL: %s", pr->last_error);
		return;
	}
	if (printer_request_set_bed(0) != PRINTER_OK) {
		session_set_statusf("bed off FAIL: %s", pr->last_error);
		return;
	}
	session_set_status("heater OFF queued (not emergency stop)");
}

static void tune_enter(void)
{
	session_get()->screen = SESSION_SCR_TUNE;
}

static void tune_leave(void)
{
	cfg_save_if_dirty();
}

static void tune_input(void)
{
	struct session *ss = session_get();
	struct printer *pr = printer_get();

	if (key_just(KEY_SELECT)) { screen_go(&dash_screen); return; }
	if (key_just(KEY_UP) && ss->tune_row > 0)
		ss->tune_row--;
	if (key_just(KEY_DOWN) && ss->tune_row < SESSION_TUNE_NROWS - 1)
		ss->tune_row++;
	if (ss->tune_row != SESSION_TUNE_ACTIONS &&
	    (key_just(KEY_LEFT) || key_just(KEY_RIGHT))) {
		int delta = key_just(KEY_RIGHT) ? 5 : -5;
		if (ss->tune_row == SESSION_TUNE_HOTEND)
			cfg_set_pla_hotend(clamp_int(cfg_pla_hotend() + delta, 0, 280));
		else if (ss->tune_row == SESSION_TUNE_BED)
			cfg_set_pla_bed(clamp_int(cfg_pla_bed() + delta, 0, 110));
		else if (ss->tune_row == SESSION_TUNE_SPEED)
			cfg_set_speed_pct(clamp_int(cfg_speed_pct() + delta, 10, 200));
		else if (ss->tune_row == SESSION_TUNE_FLOW)
			cfg_set_flow_pct(clamp_int(cfg_flow_pct() + delta, 10, 200));
	}
	if (key_just(KEY_TRIANGLE)) {
        if (session_any_busy()) { session_set_status("busy: M105 not queued"); return; }
		if (printer_request_temps() == PRINTER_OK)
			session_set_status("M105 queued");
		else
			session_set_status("M105 FAIL");
	}
	if (key_just(KEY_CROSS)) {
        if (session_any_busy()) { session_set_status("busy: apply not queued"); return; }
		int rc = PRINTER_OK;
		if (ss->tune_row == SESSION_TUNE_HOTEND) {
			int hot = clamp_int(cfg_pla_hotend(), 0, 280);
			cfg_set_pla_hotend(hot);
			rc = printer_request_set_hotend(hot);
			if (rc == PRINTER_OK) {
				cfg_save_if_dirty();
				session_set_statusf("queued M104 S%d", hot);
			} else {
				session_set_statusf("M104 FAIL: %s", pr->last_error);
			}
		} else if (ss->tune_row == SESSION_TUNE_BED) {
			int bed = clamp_int(cfg_pla_bed(), 0, 110);
			cfg_set_pla_bed(bed);
			rc = printer_request_set_bed(bed);
			if (rc == PRINTER_OK) {
				cfg_save_if_dirty();
				session_set_statusf("queued M140 S%d", bed);
			} else {
				session_set_statusf("M140 FAIL: %s", pr->last_error);
			}
		} else if (ss->tune_row == SESSION_TUNE_SPEED) {
			int spd = clamp_int(cfg_speed_pct(), 10, 200);
			cfg_set_speed_pct(spd);
			rc = printer_request_set_speed(spd);
			if (rc == PRINTER_OK) {
				cfg_save_if_dirty();
				session_set_statusf("queued M220 S%d", spd);
			} else {
				session_set_statusf("M220 FAIL: %s", pr->last_error);
			}
		} else if (ss->tune_row == SESSION_TUNE_FLOW) {
			int flow = clamp_int(cfg_flow_pct(), 10, 200);
			cfg_set_flow_pct(flow);
			rc = printer_request_set_flow(flow);
			if (rc == PRINTER_OK) {
				cfg_save_if_dirty();
				session_set_statusf("queued M221 S%d", flow);
			} else {
				session_set_statusf("M221 FAIL: %s", pr->last_error);
			}
		} else {
			cfg_set_pla_hotend(200);
			cfg_set_pla_bed(60);
			apply_heat();
			printer_request_temps();
		}
	}
	if (key_just(KEY_CIRCLE)) {
		heat_off();
		printer_request_temps();
	}
	if (key_just(KEY_SQUARE)) {
        if (session_any_busy()) { session_set_status("busy: home not queued"); return; }
		ss->confirm_kind = SESSION_CONFIRM_HOME;
		ss->confirm_name[0] = 0;
		screen_go(&confirm_screen);
	}
}

static void tune_draw(void)
{
	struct session *ss = session_get();
	const struct printer *pr = printer_get();
	char temps[96], valbuf[24];
	int y = UI_CONTENT_Y + 24;

	ui_draw_tabs(UI_TAB_TUNE, 0);
	if (pr->temps_ok)
		snprintf(temps, sizeof(temps), "now T %.0f/%.0f  B %.0f/%.0f", pr->hotend,
			 pr->hotend_target, pr->bed, pr->bed_target);
	else
		snprintf(temps, sizeof(temps), "now T --/--  B --/--");
	ui_text(12, UI_CONTENT_Y + 6, 1, UI_COL_DIM, temps);
	snprintf(valbuf, sizeof(valbuf), "%d C", cfg_pla_hotend());
	ui_draw_tune_row(y, ss->tune_row == SESSION_TUNE_HOTEND, "Nozzle", valbuf);
	y += 32;
	snprintf(valbuf, sizeof(valbuf), "%d C", cfg_pla_bed());
	ui_draw_tune_row(y, ss->tune_row == SESSION_TUNE_BED, "Bed", valbuf);
	y += 32;
	snprintf(valbuf, sizeof(valbuf), "%d %%", cfg_speed_pct());
	ui_draw_tune_row(y, ss->tune_row == SESSION_TUNE_SPEED, "Speed", valbuf);
	y += 32;
	snprintf(valbuf, sizeof(valbuf), "%d %%", cfg_flow_pct());
	ui_draw_tune_row(y, ss->tune_row == SESSION_TUNE_FLOW, "Flow", valbuf);
	y += 32;
	ui_draw_tune_row(y, ss->tune_row == SESSION_TUNE_ACTIONS, "PLA 200/60", "apply");
	ui_draw_status(ss->status_line, "LR +/-5");
	ui_draw_buttons("Apply", "OFF", "Home?", "M105");
}

const struct screen tune_screen = {
	.name = "tune",
	.enter = tune_enter,
	.input = tune_input,
	.draw = tune_draw,
	.leave = tune_leave,
};
