/*
 * FLSUN Remote logging.
 *
 * Modes:
 *   CARD  — append to ms0:/PSP/GAME/FLSUNRemote/log.txt (compile-time, -DFLSUN_DEBUG)
 *   NET   — stream over TCP (log_port, default 9999) + HTTP /logs and /api/logs
 *           (runtime: flsun.ini `log=net`; default `log=off`)
 *   OFF   — runtime default; macros become cheap early returns
 *
 * CARD and NET may both be active in a DEBUG build when log=net.
 */
#ifndef FLSUN_LOG_H
#define FLSUN_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#define FLSUN_DEFAULT_LOG_PORT 9999

/* Runtime mode from flsun.ini (card is compile-time only). */
enum {
	LOG_MODE_OFF = 0,
	LOG_MODE_NET = 1
};

void log_init(void);
void log_term(void);

/* Apply runtime settings from config (log mode/port). Safe to call more than once. */
void log_apply_config(int mode, int port);

/* Accept TCP clients / flush card buffer. Call once per frame with httpd_poll. */
void log_poll(void);

/* 1 when runtime mode is NET (HTTP /logs and TCP listener active). */
int log_net_active(void);

/* Current sequence number (next id that will be assigned). */
unsigned log_seq(void);

/*
 * Export lines with id > since into out as JSON:
 *   {"next":N,"lines":["[I] ...", ...]}
 * Returns bytes written (excluding NUL), or -1 if out is too small.
 * *next_out receives the next sequence number (same as log_seq()).
 */
int log_export_since(unsigned since, char *out, int outlen, unsigned *next_out);

/* Lightweight HTML viewer page (polls /api/logs). */
const char *log_html_page(void);

/* Core writer; prefer LOG_I / LOG_W / LOG_E. */
void log_write(char level, const char *fmt, ...)
#ifdef __GNUC__
	__attribute__((format(printf, 2, 3)))
#endif
	;

#define LOG_I(...) log_write('I', __VA_ARGS__)
#define LOG_W(...) log_write('W', __VA_ARGS__)
#define LOG_E(...) log_write('E', __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_LOG_H */
