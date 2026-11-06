/*
 * MKS TCP (:8080) + HTTP upload (:80).
 */
#include "printer_mks.h"
#include "socket.h"
#include "model_storage.h"
#include "config.h"
#include "log.h"

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspnet_inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip_cr(char *s)
{
	char *cr = strchr(s, '\r');
	if (cr)
		*cr = 0;
}



static uint64_t s_file_wait_deadline;

static int ascii_eq_ci(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = *a;
		char cb = *b;
		if (ca >= 'A' && ca <= 'Z')
			ca = (char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = (char)(cb - 'A' + 'a');
		if (ca != cb)
			return 0;
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

static int is_junk_sd_name(const char *line)
{
	int len;
	const char *dot;

	if (!line || !line[0])
		return 1;
	if (line[0] == '.')
		return 1;
	if (strstr(line, "System Volume"))
		return 1;
	if (strstr(line, "Spotlight"))
		return 1;
	len = (int)strlen(line);
	if (len >= 4) {
		const char *suf = line + len - 4;
		if (ascii_eq_ci(suf, ".DIR"))
			return 1;
	}
	dot = strrchr(line, '.');
	if (!dot || dot == line || dot[1] == 0)
		return 1;
	if (ascii_eq_ci(dot, ".gcode") || ascii_eq_ci(dot, ".gco") ||
	    ascii_eq_ci(dot, ".g"))
		return 0;
	return 1;
}

/* Empty buffer is NOT success. Prefer explicit MKS tokens / "ok". */
static int mks_reply_ok(const char *buf)
{
    int ok = 0;
    if (!buf) return 0;
    while (*buf) {
        char line[256]; int n = 0;
        while (*buf && *buf != '\n' && *buf != '\r') {
            if (n < (int)sizeof(line)-1) {
                char c = *buf;
                line[n++] = c >= 'A' && c <= 'Z' ? c + 32 : c;
            }
            buf++;
        }
        while (*buf == '\n' || *buf == '\r') buf++;
        line[n] = 0;
        char *p=line;int echoed=0;
        while (*p==' ' || *p=='\t') p++;
        if (!strncmp(p,"echo:",5)) {
            echoed=1;p+=5;while (*p==' ' || *p=='\t') p++;
            /* A command echo can contain filenames such as error.gcode. */
            if ((p[0]=='m' || p[0]=='g') && p[1]>='0' && p[1]<='9') continue;
        }
        if (!strncmp(p,"not",3) && (p[3]==' ' || p[3]=='\t')) {
            const char *q=p+3;while (*q==' ' || *q=='\t') q++;
            if (!strncmp(q,"ok",2) && (!q[2] || q[2]==' ' || q[2]=='\t')) return 0;
        }
        for (const char *w=p;*w;) {
            while (*w==' ' || *w=='\t' || *w==':') w++;
            const char *e=w;
            while (*e && *e!=' ' && *e!='\t' && *e!=':') e++;
            const char *last=e;
            while (last>w && (last[-1]=='.' || last[-1]=='!' || last[-1]==',')) last--;
            int len=(int)(last-w);
            if ((len==5 && !memcmp(w,"error",5)) || (len==4 && !memcmp(w,"fail",4)) ||
                (len==6 && !memcmp(w,"failed",6)) || (len==7 && !memcmp(w,"unknown",7))) return 0;
            w=e;
        }
        if (!echoed && ((!strncmp(p,"ok",2) && (!p[2] || p[2]==' ' || p[2]=='\t')) ||
            !strcmp(p,"file selected"))) ok = 1;
    }
    return ok;
}

/* One-shot: connect, send "CMD\r\n", collect reply. */
static int mks_transact(const char *cmd,
			char *buf, int cap, int timeout_ms,
			const char *early_stop, int quiet_ms)
{
	int fd;
	char line[96];
	int n;
	int clen;

	if (!cmd || !buf || cap < 2)
		return -1;

	clen = snprintf(line, sizeof(line), "%s\r\n", cmd);
	if (clen <= 0 || clen >= (int)sizeof(line))
		return -1;

	if (s_file_wait_deadline) {
		uint64_t now = sock_now_ms();
		if (now >= s_file_wait_deadline) return -2;
		if ((uint64_t)timeout_ms > s_file_wait_deadline - now)
			timeout_ms = (int)(s_file_wait_deadline - now);
	}
	struct cfg_printer_connection connection;
    if (cfg_copy_printer_connection(&connection)!=0) return -1;
    fd = sock_connect_timeout(connection.ip, connection.port, timeout_ms < 2500 ? timeout_ms : 2500);
	if (fd < 0)
		return fd;

	if (send_all(fd, line, clen) != 0) {
		sceNetInetClose(fd);
		return -4;
	}

	if (s_file_wait_deadline) {
		uint64_t now = sock_now_ms();
		timeout_ms = now < s_file_wait_deadline ? (int)(s_file_wait_deadline - now) : 0;
	}
	n = sock_recv_deadline(fd, buf, cap, timeout_ms, early_stop, quiet_ms);
	sceNetInetClose(fd);
	return n;
}

int mks_fetch_temps(MksTemps *out)
{
	char buf[512];
	int n;
	const char *tp;
	const char *bp;
	int t = 0, tt = 0, b = 0, bt = 0;
	int got_t = 0, got_b = 0;
	char art[256];

	memset(out, 0, sizeof(*out));
	n = mks_transact("M105", buf, sizeof(buf), 2500, NULL, 300);
	if (n == -1 || n == -2 || n == -3) {
		snprintf(out->err, sizeof(out->err), "connect fail (%d)", n);
		return -1;
	}
	if (n == -4) {
		snprintf(out->err, sizeof(out->err), "send fail");
		return -1;
	}
	if (n <= 0) {
		snprintf(out->err, sizeof(out->err), "no reply / timeout");
		return -1;
	}

	snprintf(out->raw, sizeof(out->raw), "%.*s", (int)sizeof(out->raw)-1, buf);

	tp = strstr(buf, "T:");
	if (tp) {
		tp += 2;
		while (*tp == ' ')
			tp++;
		if (*tp >= '0' && *tp <= '9') {
			while (*tp >= '0' && *tp <= '9') {
				t = t * 10 + (*tp - '0');
				tp++;
			}
			while (*tp && *tp != '/')
				tp++;
			if (*tp == '/') {
				tp++;
				while (*tp == ' ')
					tp++;
				while (*tp >= '0' && *tp <= '9') {
					tt = tt * 10 + (*tp - '0');
					tp++;
				}
			}
			got_t = 1;
		}
	}

	bp = buf;
	while ((bp = strstr(bp, "B:")) != 0) {
		if (bp > buf && bp[-1] == '@') {
			bp += 2;
			continue;
		}
		bp += 2;
		while (*bp == ' ')
			bp++;
		if (*bp >= '0' && *bp <= '9') {
			while (*bp >= '0' && *bp <= '9') {
				b = b * 10 + (*bp - '0');
				bp++;
			}
			while (*bp && *bp != '/')
				bp++;
			if (*bp == '/') {
				bp++;
				while (*bp == ' ')
					bp++;
				while (*bp >= '0' && *bp <= '9') {
					bt = bt * 10 + (*bp - '0');
					bp++;
				}
			}
			got_b = 1;
		}
		break;
	}

	if (got_t && got_b) {
		out->hotend = (float)t;
		out->hotend_target = (float)tt;
		out->bed = (float)b;
		out->bed_target = (float)bt;
		out->ok = 1;
		out->err[0] = 0;
	} else {
		snprintf(out->err, sizeof(out->err), "parse t%d b%d n=%d", got_t, got_b, n);
		out->ok = 0;
	}

	snprintf(art, sizeof(art), "ok=%d T=%d/%d B=%d/%d got=%d%d n=%d\nraw=%.128s\n",
		 out->ok, t, tt, b, bt, got_t, got_b, n, buf);
	LOG_I("M105 %s", art);

	return out->ok ? 0 : -1;
}

int mks_list_files(MksFileList *out)
{
	char buf[4096];
	int n;
	char *line;
	char *next;
	int in_list = 0;
	char art[512];
	int i;
	int art_n;

	memset(out, 0, sizeof(*out));
	n = mks_transact("M20", buf, sizeof(buf), 6000, "End file list", 800);
	if (n < 0) {
		snprintf(out->err, sizeof(out->err), "connect/send fail (%d)", n);
		return -1;
	}
	if (n == 0) {
		snprintf(out->err, sizeof(out->err), "no reply / timeout");
		return -1;
	}

	line = buf;
	while (line && *line) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		strip_cr(line);
		while (*line == ' ' || *line == '\t')
			line++;

		if (strncmp(line, "Begin file list", 15) == 0) {
			in_list = 1;
			line = next;
			continue;
		}
		if (strncmp(line, "End file list", 13) == 0)
			break;
		if (in_list && !is_junk_sd_name(line) && out->count < MKS_MAX_FILES) {
			strncpy(out->names[out->count], line, MKS_NAME_LEN - 1);
			out->names[out->count][MKS_NAME_LEN - 1] = 0;
			out->count++;
		}
		line = next;
	}

	art_n = snprintf(art, sizeof(art), "count=%d\n", out->count);
	for (i = 0; i < out->count && art_n < (int)sizeof(art) - 2; i++) {
		int w = snprintf(art + art_n, sizeof(art) - (size_t)art_n, "%s\n",
				 out->names[i]);
		if (w < 0)
			break;
		art_n += w;
	}
	LOG_I("M20 %s", art);

	if (!in_list && out->count == 0) {
		snprintf(out->err, sizeof(out->err), "no file list in reply");
		return -1;
	}
	return 0;
}

int mks_print_status(MksPrintStatus *out)
{
	char buf[256];
	int n;
	const char *p;
	int a = 0, b = 0;
	int got_frac = 0;

	memset(out, 0, sizeof(*out));
	out->progress = -1.0f;

	n = mks_transact("M27", buf, sizeof(buf), 2000, NULL, 350);
	if (n < 0) {
		snprintf(out->err, sizeof(out->err), "connect/send fail (%d)", n);
		return -1;
	}
	if (n <= 0) {
		snprintf(out->err, sizeof(out->err), "no reply");
		return -1;
	}

	snprintf(out->raw, sizeof(out->raw), "%.*s", (int)sizeof(out->raw)-1, buf);
	out->ok = 1;

	if (strstr(buf, "Not SD printing") || strstr(buf, "M27 0"))
		out->printing = 0;
	else if (strstr(buf, "SD printing") || strstr(buf, "M27 1"))
		out->printing = 1;

	p = strchr(buf, '%');
	if (p && p > buf) {
		const char *q = p - 1;
		int val = 0;
		int mul = 1;
		while (q >= buf && *q >= '0' && *q <= '9') {
			val += (*q - '0') * mul;
			mul *= 10;
			q--;
		}
		out->progress = (float)val;
		out->printing = 1;
	}

	p = buf;
	while (*p) {
		if (*p >= '0' && *p <= '9') {
			a = 0;
			while (*p >= '0' && *p <= '9') {
				a = a * 10 + (*p - '0');
				p++;
			}
			if (*p == '/') {
				p++;
				b = 0;
				while (*p >= '0' && *p <= '9') {
					b = b * 10 + (*p - '0');
					p++;
				}
				if (b > 0) {
					out->progress = 100.0f * (float)a / (float)b;
					out->printing = (a < b) ? 1 : 0;
					got_frac = 1;
				}
			}
			continue;
		}
		p++;
	}
	(void)got_frac;
	return 0;
}

int mks_file_size(const char *filename, int *size_out)
{
	char cmd[96];
	char buf[256];
	int n;
	const char *p;
	int size = 0;

	if (size_out)
		*size_out = -1;
	if (!filename || !filename[0])
		return -1;

	snprintf(cmd, sizeof(cmd), "M23 %s", filename);
	n = mks_transact(cmd, buf, sizeof(buf), 2500, NULL, 400);
	if (n <= 0)
		return -1;
	if (!strstr(buf, "File selected") && !strstr(buf, "Size:"))
		return -1;

	p = strstr(buf, "Size:");
	if (!p)
		return -1;
	p += 5;
	while (*p == ' ')
		p++;
	while (*p >= '0' && *p <= '9') {
		size = size * 10 + (*p - '0');
		p++;
	}
	if (size_out)
		*size_out = size;
	return 0;
}

int mks_select_and_print(const char *filename,
			 char *err, int errlen)
{
	char cmd[96];
	char buf[256];
	int n;

	if (err && errlen > 0)
		err[0] = 0;
	if (!filename || !filename[0]) {
		if (err && errlen > 0)
			snprintf(err, errlen, "no file");
		return -1;
	}

	snprintf(cmd, sizeof(cmd), "M23 %s", filename);
	n = mks_transact(cmd, buf, sizeof(buf), 2000, NULL, 350);
	if (n < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M23 connect fail");
		return -1;
	}
	if (n == 0 || !mks_reply_ok(buf)) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M23 bad reply");
		return -1;
	}
	sceKernelDelayThread(300 * 1000);
	n = mks_transact("M24", buf, sizeof(buf), 2000, NULL, 350);
	if (n < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M24 connect fail");
		return -1;
	}
	if (n == 0 || !mks_reply_ok(buf)) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M24 bad reply");
		return -1;
	}
	return 0;
}

int mks_pause(char *err, int errlen)
{
	char buf[128];
	int n;

	if (err && errlen > 0)
		err[0] = 0;
	n = mks_transact("M25", buf, sizeof(buf), 2000, NULL, 350);
	if (n < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M25 fail (%d)", n);
		return -1;
	}
	if (n == 0 || !mks_reply_ok(buf)) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M25 bad reply");
		return -1;
	}
	return 0;
}

int mks_resume(char *err, int errlen)
{
	char buf[128];
	int n;

	if (err && errlen > 0)
		err[0] = 0;
	n = mks_transact("M24", buf, sizeof(buf), 2000, NULL, 350);
	if (n < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M24 fail (%d)", n);
		return -1;
	}
	if (n == 0 || !mks_reply_ok(buf)) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M24 bad reply");
		return -1;
	}
	return 0;
}

/* Shared helper for simple M/G commands that expect "ok". */
static int mks_simple_cmd(const char *cmd,
			  char *err, int errlen)
{
	char buf[256];
	int n;

	if (err && errlen > 0)
		err[0] = 0;
	n = mks_transact(cmd, buf, sizeof(buf), 2500, NULL, 400);
	if (n < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "connect fail (%d)", n);
		return -1;
	}
	if (n == 0 || !mks_reply_ok(buf)) {
		if (err && errlen > 0)
			snprintf(err, errlen, "bad reply / empty");
		return -1;
	}
	return 0;
}

int mks_set_hotend(int celsius, char *err, int errlen)
{
	char cmd[32];

	if (celsius < 0)
		celsius = 0;
	if (celsius > 300)
		celsius = 300;
	snprintf(cmd, sizeof(cmd), "M104 S%d", celsius);
	return mks_simple_cmd(cmd, err, errlen);
}

int mks_set_bed(int celsius, char *err, int errlen)
{
	char cmd[32];

	if (celsius < 0)
		celsius = 0;
	if (celsius > 120)
		celsius = 120;
	snprintf(cmd, sizeof(cmd), "M140 S%d", celsius);
	return mks_simple_cmd(cmd, err, errlen);
}

int mks_set_speed(int percent, char *err, int errlen)
{
	char cmd[32];

	if (percent < 10)
		percent = 10;
	if (percent > 300)
		percent = 300;
	snprintf(cmd, sizeof(cmd), "M220 S%d", percent);
	return mks_simple_cmd(cmd, err, errlen);
}

int mks_set_flow(int percent, char *err, int errlen)
{
	char cmd[32];

	if (percent < 10)
		percent = 10;
	if (percent > 300)
		percent = 300;
	snprintf(cmd, sizeof(cmd), "M221 S%d", percent);
	return mks_simple_cmd(cmd, err, errlen);
}

int mks_home(char *err, int errlen)
{
	char buf[256];
	int n;

	if (err && errlen > 0)
		err[0] = 0;
	n = mks_transact("G28", buf, sizeof(buf), 8000, NULL, 800);
	if (n < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "G28 connect fail (%d)", n);
		return -1;
	}
	if (n == 0 || !mks_reply_ok(buf)) {
		if (err && errlen > 0)
			snprintf(err, errlen, "G28 bad reply");
		return -1;
	}
	return 0;
}

int mks_delete(const char *filename, char *err, int errlen)
{
	char cmd[96];
	char buf[256];
	int n;

	if (err && errlen > 0)
		err[0] = 0;
	if (!filename || !filename[0]) {
		if (err && errlen > 0)
			snprintf(err, errlen, "no file");
		return -1;
	}

	snprintf(cmd, sizeof(cmd), "M30 %s", filename);
	n = mks_transact(cmd, buf, sizeof(buf), 2500, NULL, 400);
	if (n < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "M30 connect fail (%d)", n);
		return -1;
	}
	if (n == 0 || strstr(buf, "Deletion failed") || strstr(buf, "Unknown") ||
	    !mks_reply_ok(buf)) {
		if (err && errlen > 0)
			snprintf(err, errlen, "Deletion failed");
		return -1;
	}
	return 0;
}

static int mks_name_on_list(const MksFileList *list, const char *filename)
{
	int i;

	if (!list || !filename)
		return 0;
	for (i = 0; i < list->count; i++) {
		if (ascii_eq_ci(list->names[i], filename))
			return 1;
	}
	return 0;
}

int mks_wait_file_on_sd(const char *filename,
			int timeout_ms, char *err, int errlen)
{
	MksFileList list;
	uint64_t deadline = sock_now_ms() + sock_remaining_ms(timeout_ms < 500 ? 500 : timeout_ms);

	if (err && errlen > 0)
		err[0] = 0;
	if (!filename || !filename[0]) {
		if (err && errlen > 0)
			snprintf(err, errlen, "no file");
		return -1;
	}
	if (timeout_ms < 500)
		timeout_ms = 500;

	uint64_t previous = sock_limit_begin(timeout_ms);
	while (sock_now_ms() < deadline && !sock_task_expired()) {
		s_file_wait_deadline = deadline;
		int rc = mks_list_files(&list);
		s_file_wait_deadline = 0;
		if (rc == 0 && mks_name_on_list(&list, filename)) { sock_limit_end(previous); return 0; }
		uint64_t now = sock_now_ms();
		if (now < deadline && !sock_task_expired()) {
			int left = sock_remaining_ms((int)(deadline - now));
			sceKernelDelayThread((left < 400 ? left : 400) * 1000);
		}

	}
	sock_limit_end(previous);
	if (err && errlen > 0)
		snprintf(err, errlen, "not in M20 after upload");
	return -1;
}

int mks_upload_ex(const char *filename,
		  const void *data, int size,
		  FlsunUploadProgress prog, void *prog_user,
		  char *err, int errlen)
{
	char safe[MKS_NAME_LEN];
	char hdr[320];
	char resp[512];
	int fd;
	int n;
	int sent;
	int hdrlen;
	const char *body;
	int body_len;

	if (err && errlen > 0)
		err[0] = 0;
	if (!filename || !filename[0] || !data || size < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "bad args");
		return -1;
	}
	if (size > cfg_max_upload()) {
		if (err && errlen > 0)
			snprintf(err, errlen, "too big (max 4MiB)");
		return -1;
	}

	model_storage_sanitize_filename(filename, safe, (int)sizeof(safe));
	if (!safe[0]) {
		if (err && errlen > 0)
			snprintf(err, errlen, "bad name");
		return -1;
	}

	struct cfg_printer_connection connection;
    if (cfg_copy_printer_connection(&connection)!=0) {
        if (err && errlen>0) snprintf(err,errlen,"config unavailable");
        return -1;
    }
    fd = sock_connect_timeout(connection.ip, cfg_mks_http_port(), 4000);
	if (fd < 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "HTTP connect fail (%d)", fd);
		return -1;
	}

	hdrlen = snprintf(hdr, sizeof(hdr),
			  "POST /upload?X-Filename=%s HTTP/1.0\r\n"
			  "Host: %s\r\n"
			  "Content-Type: application/octet-stream\r\n"
			  "Content-Length: %d\r\n"
			  "Connection: close\r\n"
			  "\r\n",
			  safe, connection.ip, size);
	if (hdrlen <= 0 || hdrlen >= (int)sizeof(hdr)) {
		sceNetInetClose(fd);
		if (err && errlen > 0)
			snprintf(err, errlen, "hdr overflow");
		return -1;
	}

	if (send_all(fd, hdr, hdrlen) != 0) {
		sceNetInetClose(fd);
		if (err && errlen > 0)
			snprintf(err, errlen, "send hdr fail");
		return -1;
	}

	body = (const char *)data;
	body_len = size;
	sent = 0;
	while (sent < body_len) {
		int chunk = body_len - sent;
		if (chunk > 4096)
			chunk = 4096;
		if (send_all(fd, body + sent, chunk) != 0) {
			sceNetInetClose(fd);
			if (err && errlen > 0)
				snprintf(err, errlen, "send body fail @%d", sent);
			return -1;
		}
		sent += chunk;
		if (prog)
			prog(sent, body_len, prog_user);
	}

	n = sock_recv_deadline(fd, resp, sizeof(resp), 20000, NULL, 800);
	sceNetInetClose(fd);
	if (n <= 0) {
		if (err && errlen > 0)
			snprintf(err, errlen, "no HTTP reply");
		return -1;
	}

	if (strstr(resp, "\"err\":0") || strstr(resp, "err\":0") ||
	    strstr(resp, "\"err\": 0"))
		return 0;
	if (strstr(resp, "200") && !strstr(resp, "err"))
		return 0;
	if (err && errlen > 0) {
		char *line = strstr(resp, "\r\n\r\n");
		if (line)
			line += 4;
		else
			line = resp;
		snprintf(err, errlen, "upload reject: %.60s", line);
	}
	return -1;
}

int mks_upload(const char *filename,
	       const void *data, int size, char *err, int errlen)
{
	return mks_upload_ex(filename, data, size, NULL, NULL, err, errlen);
}
