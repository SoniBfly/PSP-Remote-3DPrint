/*
 * Application / UI selection state (not nozzle temps — those live in printer).
 */
#ifndef FLSUN_SESSION_H
#define FLSUN_SESSION_H
#include "printer.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	SESSION_SCR_TILES = 0,
	SESSION_SCR_CONFIRM = 1,
	SESSION_SCR_DASH = 2,
	SESSION_SCR_PREVIEW = 3,
	SESSION_SCR_TUNE = 4
};

enum {
	SESSION_CONFIRM_SD = 0,
	SESSION_CONFIRM_PSP = 1, /* local models/ folder (legacy "inbox") */
	SESSION_CONFIRM_HOME = 2,
	SESSION_CONFIRM_PRINT = 3
};

enum {
	SESSION_TUNE_HOTEND = 0,
	SESSION_TUNE_BED = 1,
	SESSION_TUNE_SPEED = 2,
	SESSION_TUNE_FLOW = 3,
	SESSION_TUNE_ACTIONS = 4,
	SESSION_TUNE_NROWS = 5
};

#define SESSION_STATUS_LEN 128
#define SESSION_NAME_LEN   64

struct session {
	int screen;
	int sel;
	int sel_mark;
	int scroll_row;
	int tune_row;
	int ip_octet; /* dash: selected IPv4 octet 0..3 */
	int ui_paused;
	int busy; /* local UI busy (preview load, etc.); printer has its own */

	int confirm_kind;
    int confirm_upload;
    struct file_entry confirm_entry;
	char confirm_name[SESSION_NAME_LEN];

	char preview_gcode[SESSION_NAME_LEN];
	int preview_supports;
	int preview_has_sup;

	/* Size shown on the selected tile (from file_entry.size; no M23 spam). */
	int sel_size;
	int sel_size_for;

	int did_list;
	char status_line[SESSION_STATUS_LEN];
};

void session_init(void);
struct session *session_get(void);

void session_set_status(const char *msg);
void session_set_statusf(const char *fmt, ...);
void session_set_busy(int busy);
int session_is_busy(void);

/* True when either session or printer is busy (gate input). */
int session_any_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_SESSION_H */
