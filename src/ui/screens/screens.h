#ifndef FLSUN_SCREENS_H
#define FLSUN_SCREENS_H

#include "screen.h"
#include "key_control.h"

extern const struct screen models_screen;
extern const struct screen tune_screen;
extern const struct screen dash_screen;
extern const struct screen preview_screen;
extern const struct screen confirm_screen;

void model_label(const char *filename, char *out, int outlen);
void models_clamp_selection(void);
void models_sync_selection_size(void);
void models_print_selected(void);
void models_upload_progress(int done, int total, void *user);
void preview_open(const char *gcode_name);

struct dash_ops {
	void (*refresh_local_ip)(void);
	const char *(*local_ip)(void);
	const char *(*http_status)(void);
	int (*http_listening)(void);
	void (*http_start)(void);
	void (*http_stop)(void);
	void (*apply_log_config)(void);
};

void dash_set_ops(const struct dash_ops *ops);

#endif
