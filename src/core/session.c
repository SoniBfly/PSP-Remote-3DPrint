#include "session.h"
#include "printer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static struct session s_session;

void session_init(void)
{
	memset(&s_session, 0, sizeof(s_session));
	s_session.screen = SESSION_SCR_TILES;
	s_session.sel_mark = -1;
	s_session.sel_size = -1;
	s_session.sel_size_for = -1;
	s_session.tune_row = SESSION_TUNE_HOTEND;
	snprintf(s_session.status_line, sizeof(s_session.status_line), "loading…");
}

struct session *session_get(void)
{
	return &s_session;
}

void session_set_status(const char *msg)
{
	if (!msg)
		msg = "";
	snprintf(s_session.status_line, sizeof(s_session.status_line), "%s", msg);
}

void session_set_statusf(const char *fmt, ...)
{
	va_list ap;

	if (!fmt)
		return;
	va_start(ap, fmt);
	vsnprintf(s_session.status_line, sizeof(s_session.status_line), fmt, ap);
	va_end(ap);
}

void session_set_busy(int busy)
{
	s_session.busy = busy ? 1 : 0;
}

int session_is_busy(void)
{
	return s_session.busy;
}

int session_any_busy(void)
{
	return s_session.busy || printer_is_busy();
}
