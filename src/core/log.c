/*
 * Logging: Memory Stick (DEBUG), TCP stream, ring buffer for HTTP /api/logs.
 * Never blocks the main loop: TCP sends are non-blocking; lines are dropped
 * when there is no client or the send would block.
 */
#include "log.h"

#include <pspkernel.h>
#include <pspthreadman.h>
#ifdef FLSUN_DEBUG
#include <pspiofilemgr.h>
#endif
#include <pspnet_inet.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define LOG_PATH       "ms0:/PSP/GAME/FLSUNRemote/log.txt"
#define LOG_GAME_DIR   "ms0:/PSP/GAME/FLSUNRemote"
#define LOG_LINE_MAX   160
#define LOG_RING_CAP   48
#define LOG_CARD_BATCH 8

typedef struct {
	unsigned id;
	char     text[LOG_LINE_MAX];
} LogEntry;

static SceLwMutexWorkarea s_log_lock;
static int s_log_lock_ready;
static void log_lock(void) { sceKernelLockLwMutex(&s_log_lock,1,NULL); }
static void log_unlock(void) { sceKernelUnlockLwMutex(&s_log_lock,1); }
static int      s_inited;
static int      s_mode;          /* LOG_MODE_OFF / LOG_MODE_NET */
static int      s_port = FLSUN_DEFAULT_LOG_PORT;
static int      s_listen_fd = -1;
static int      s_client_fd = -1;
static int      s_client_got_backlog;
#ifdef FLSUN_DEBUG
static SceUID   s_card_fd = -1;
static int      s_card_pending;
#endif
static unsigned s_seq = 1;       /* next id to assign; 0 is "before anything" */
static int      s_ring_count;
static int      s_ring_head;     /* index of oldest entry when full */
static LogEntry s_ring[LOG_RING_CAP];

static const char s_log_html[] =
	"<!DOCTYPE html><html><head><meta charset=utf-8>"
	"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
	"<title>FLSUN logs</title>"
	"<style>"
	"body{margin:0;background:#111;color:#cfc;font:13px/1.4 monospace}"
	"#bar{padding:8px 12px;background:#1a1a1a;border-bottom:1px solid #333;"
	"display:flex;gap:12px;align-items:center;position:sticky;top:0}"
	"#bar b{color:#8f8}"
	"#log{padding:8px 12px;white-space:pre-wrap;word-break:break-all}"
	".W{color:#fc6}.E{color:#f66}.I{color:#cfc}"
	"button{background:#333;color:#eee;border:1px solid #555;padding:4px 10px;"
	"font:inherit;cursor:pointer}"
	"</style></head><body>"
	"<div id=bar><b>FLSUN logs</b><span id=st>connecting…</span>"
	"<button id=clr type=button>Clear view</button>"
	"<label><input type=checkbox id=fol checked> follow</label></div>"
	"<div id=log></div>"
	"<script>"
	"(function(){"
	"var since=0,el=document.getElementById('log'),"
	"st=document.getElementById('st'),"
	"fol=document.getElementById('fol');"
	"document.getElementById('clr').onclick=function(){el.textContent='';};"
	"function tick(){"
	"fetch('/api/logs?since='+since).then(function(r){"
	"if(!r.ok)throw new Error(r.status);"
	"return r.json();"
	"}).then(function(j){"
	"st.textContent='ok · next='+j.next;"
	"if(j.lines&&j.lines.length){"
	"for(var i=0;i<j.lines.length;i++){"
	"var line=j.lines[i],cls='I';"
	"if(line.indexOf('[W]')===0)cls='W';"
	"else if(line.indexOf('[E]')===0)cls='E';"
	"var d=document.createElement('div');d.className=cls;"
	"d.textContent=line;el.appendChild(d);"
	"}"
	"if(fol.checked)window.scrollTo(0,document.body.scrollHeight);"
	"}"
	"if(typeof j.next==='number')since=j.next;"
	"}).catch(function(e){st.textContent='err '+e;});"
	"}"
	"tick();setInterval(tick,1000);"
	"})();"
	"</script></body></html>";

static void log_set_nonblock(int fd, int on)
{
	int flag = on ? 1 : 0;

	if (fd >= 0)
		sceNetInetSetsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &flag, sizeof(flag));
}

static int log_want_any(void)
{
#ifdef FLSUN_DEBUG
	return 1;
#else
	return s_mode == LOG_MODE_NET;
#endif
}

#ifdef FLSUN_DEBUG
static void log_ensure_dirs(void)
{
	sceIoMkdir("ms0:/PSP", 0777);
	sceIoMkdir("ms0:/PSP/GAME", 0777);
	sceIoMkdir(LOG_GAME_DIR, 0777);
}

static void log_card_open(void)
{
	if (s_card_fd >= 0)
		return;
	log_ensure_dirs();
	s_card_fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0666);
	s_card_pending = 0;
}

static void log_card_flush(void)
{
	/* sceIo has no fsync; reopen would be heavy — just clear the batch counter. */
	if (s_card_fd >= 0 && s_card_pending > 0)
		s_card_pending = 0;
}

static void log_card_close(void)
{
	if (s_card_fd >= 0) {
		sceIoClose(s_card_fd);
		s_card_fd = -1;
	}
	s_card_pending = 0;
}

static void log_card_write_line(const char *line)
{
	int n;

	if (s_card_fd < 0)
		log_card_open();
	if (s_card_fd < 0)
		return;
	n = (int)strlen(line);
	if (n > 0)
		sceIoWrite(s_card_fd, line, n);
	sceIoWrite(s_card_fd, "\n", 1);
	s_card_pending++;
	if (s_card_pending >= LOG_CARD_BATCH)
		log_card_flush();
}
#endif /* FLSUN_DEBUG */

static void log_close_client(void)
{
	if (s_client_fd >= 0) {
		sceNetInetClose(s_client_fd);
		s_client_fd = -1;
	}
	s_client_got_backlog = 0;
}

static void log_stop_listen(void)
{
	log_close_client();
	if (s_listen_fd >= 0) {
		sceNetInetClose(s_listen_fd);
		s_listen_fd = -1;
	}
}

static int log_start_listen(void)
{
	struct sockaddr_in name;
	int fd;
	int yes = 1;

	log_stop_listen();
	fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	sceNetInetSetsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	memset(&name, 0, sizeof(name));
	name.sin_len = sizeof(name);
	name.sin_family = AF_INET;
	name.sin_port = htons((unsigned short)s_port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	if (sceNetInetBind(fd, (struct sockaddr *)&name, sizeof(name)) < 0) {
		sceNetInetClose(fd);
		return -1;
	}
	if (sceNetInetListen(fd, 1) < 0) {
		sceNetInetClose(fd);
		return -1;
	}
	log_set_nonblock(fd, 1);
	s_listen_fd = fd;
	return 0;
}

/* Non-blocking send of one line + '\n'. Returns 0 ok, -1 drop/close. */
static int log_client_send_line(const char *line)
{
	char buf[LOG_LINE_MAX + 2];
	int len;
	int sent = 0;
	int n;

	if (s_client_fd < 0)
		return -1;
	len = snprintf(buf, sizeof(buf), "%s\n", line);
	if (len <= 0)
		return 0;
	if (len >= (int)sizeof(buf))
		len = (int)sizeof(buf) - 1;
	while (sent < len) {
		n = (int)sceNetInetSend(s_client_fd, buf + sent, len - sent, 0);
		if (n > 0) {
			sent += n;
			continue;
		}
		/* Would block or error — drop the line, keep the client. */
		return -1;
	}
	return 0;
}

static void log_send_backlog(void)
{
	int i;
	int idx;

	if (s_client_fd < 0 || s_client_got_backlog)
		return;
	for (i = 0; i < s_ring_count; i++) {
		idx = (s_ring_head + i) % LOG_RING_CAP;
		if (log_client_send_line(s_ring[idx].text) != 0)
			break;
	}
	s_client_got_backlog = 1;
}

static void log_ring_push(unsigned id, const char *text)
{
	int idx;

	if (s_ring_count < LOG_RING_CAP) {
		idx = (s_ring_head + s_ring_count) % LOG_RING_CAP;
		s_ring_count++;
	} else {
		idx = s_ring_head;
		s_ring_head = (s_ring_head + 1) % LOG_RING_CAP;
	}
	s_ring[idx].id = id;
	{
		size_t n = strlen(text);
		if (n >= LOG_LINE_MAX)
			n = LOG_LINE_MAX - 1;
		memcpy(s_ring[idx].text, text, n);
		s_ring[idx].text[n] = 0;
	}
}

void log_init(void)
{
    if (!s_log_lock_ready) {
        if (sceKernelCreateLwMutex(&s_log_lock,"log_state",0,0,NULL)<0) return;
        s_log_lock_ready=1;
    }
	if (s_inited)
		return;
	s_inited = 1;
	s_mode = LOG_MODE_OFF;
	s_port = FLSUN_DEFAULT_LOG_PORT;
	s_seq = 1;
	s_ring_count = 0;
	s_ring_head = 0;
#ifdef FLSUN_DEBUG
	log_card_open();
#endif
}

static void log_term_impl(void)
{
	log_stop_listen();
#ifdef FLSUN_DEBUG
	log_card_close();
#endif
	s_inited = 0;
}

static void log_apply_config_impl(int mode, int port)
{
	int want_net;

	if (!s_inited)
		log_init();
	if (mode != LOG_MODE_NET)
		mode = LOG_MODE_OFF;
	if (port <= 0 || port >= 65536)
		port = FLSUN_DEFAULT_LOG_PORT;

	want_net = (mode == LOG_MODE_NET);
	if (!want_net) {
		s_mode = LOG_MODE_OFF;
		log_stop_listen();
		return;
	}

	if (s_mode == LOG_MODE_NET && s_port == port && s_listen_fd >= 0) {
		s_mode = LOG_MODE_NET;
		return;
	}
	s_mode = LOG_MODE_NET;
	s_port = port;
	if (log_start_listen() != 0) {
		/* Keep mode NET so HTTP /logs still works; TCP just failed. */
		s_listen_fd = -1;
	}
}

static void log_poll_impl(void)
{
	struct sockaddr_in client;
	int alen;
	int cfd;

	if (!s_inited)
		return;

#ifdef FLSUN_DEBUG
	log_card_flush();
#endif

	if (s_mode != LOG_MODE_NET || s_listen_fd < 0)
		return;

	/* Drop a half-dead client if recv sees close (peek). */
	if (s_client_fd >= 0) {
		char peek;
		int r = (int)sceNetInetRecv(s_client_fd, &peek, 1, 0);
		if (r == 0)
			log_close_client();
		/* r < 0: would-block, fine; r > 0: discard unexpected input */
	}

	memset(&client, 0, sizeof(client));
	alen = (int)sizeof(client);
	cfd = sceNetInetAccept(s_listen_fd, (struct sockaddr *)&client,
			       (socklen_t *)&alen);
	if (cfd <= 0)
		return;

	log_set_nonblock(cfd, 1);
	/* One client at a time — replace the previous. */
	log_close_client();
	s_client_fd = cfd;
	s_client_got_backlog = 0;
	log_send_backlog();
}

static int log_net_active_impl(void)
{
	return s_mode == LOG_MODE_NET;
}

static unsigned log_seq_impl(void)
{
	return s_seq;
}

static int log_export_since_impl(unsigned since, char *out, int outlen, unsigned *next_out)
{
	int i, idx, n, used;
	int first = 1;

	if (!out || outlen < 32)
		return -1;
	if (next_out)
		*next_out = s_seq-1;

	used = snprintf(out, outlen, "{\"next\":%u,\"lines\":[", s_seq-1);
	if (used < 0 || used >= outlen)
		return -1;

	for (i = 0; i < s_ring_count; i++) {
		const char *t;
		int j;

		idx = (s_ring_head + i) % LOG_RING_CAP;
		if (s_ring[idx].id <= since)
			continue;
		t = s_ring[idx].text;
		/* Append ,"escaped" */
		n = snprintf(out + used, outlen - used, first ? "\"" : ",\"");
		if (n < 0 || used + n >= outlen)
			return -1;
		used += n;
		first = 0;
		for (j = 0; t[j]; j++) {
			char c = t[j];
			if (c == '"' || c == '\\') {
				if (used + 2 >= outlen)
					return -1;
				out[used++] = '\\';
				out[used++] = c;
			} else if ((unsigned char)c < 0x20) {
				if (used + 1 >= outlen)
					return -1;
				out[used++] = ' ';
			} else {
				if (used + 1 >= outlen)
					return -1;
				out[used++] = c;
			}
		}
		if (used + 1 >= outlen)
			return -1;
		out[used++] = '"';
		out[used] = 0;
	}

	if (used + 3 >= outlen)
		return -1;
	out[used++] = ']';
	out[used++] = '}';
	out[used] = 0;
	return used;
}

const char *log_html_page(void)
{
	return s_log_html;
}

void log_write(char level, const char *fmt, ...)
{
	char body[LOG_LINE_MAX - 8];
	char line[LOG_LINE_MAX];
	va_list ap;
	unsigned id;

    if (!s_inited) log_init();
    if (!s_log_lock_ready) return;
    log_lock();
    if (!log_want_any() || !fmt) { log_unlock(); return; }

	va_start(ap, fmt);
	vsnprintf(body, sizeof(body), fmt, ap);
	va_end(ap);

	snprintf(line, sizeof(line), "[%c] %s", level ? level : 'I', body);
	id = s_seq++;
	if (s_seq == 0)
		s_seq = 1; /* skip 0 so "since=0" means "everything" */
	log_ring_push(id, line);

#ifdef FLSUN_DEBUG
	log_card_write_line(line);
#endif

	if (s_mode == LOG_MODE_NET && s_client_fd >= 0 && s_client_got_backlog)
		log_client_send_line(line);
    log_unlock();
}

void log_apply_config(int mode,int port)
{
    if (!s_inited) log_init();
    if (!s_log_lock_ready) return;
    log_lock();log_apply_config_impl(mode,port);log_unlock();
}
void log_term(void)
{
    if (!s_log_lock_ready) return;
    log_lock();log_term_impl();log_unlock();
}
void log_poll(void)
{
    if (!s_log_lock_ready) return;
    log_lock();log_poll_impl();log_unlock();
}
int log_net_active(void)
{
    if (!s_log_lock_ready) return 0;
    log_lock();int on=log_net_active_impl();log_unlock();return on;
}
unsigned log_seq(void)
{
    if (!s_log_lock_ready) return 0;
    log_lock();unsigned n=log_seq_impl();log_unlock();return n;
}
int log_export_since(unsigned since,char *out,int cap,unsigned *next)
{
    if (!s_log_lock_ready) return -1;
    log_lock();int rc=log_export_since_impl(since,out,cap,next);log_unlock();return rc;
}
