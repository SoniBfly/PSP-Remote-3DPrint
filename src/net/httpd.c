/*
 * HTTP Load server: www/, /inbox.json, /inbox/<name>, /upload, /preview, slicer, logs.
 * File I/O goes through model_storage (models/<slug>/…).
 */
#include "httpd.h"
#include "socket.h"
#include "model_storage.h"
#include "config.h"
#include "log.h"
#include "net.h"

#include <pspkernel.h>
#include <pspthreadman.h>
#include <pspiofilemgr.h>
#include <pspnet_inet.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define WWW_DIR    cfg_www_dir()
#define SLICER_CFG cfg_slicer_json_path()
#define SLICER_CFG_MAX CFG_SLICER_JSON_MAX

static int g_listen_fd = -1;
static int g_http_port;
static char g_httpd_status[64] = "HTTP off";
static char g_httpd_event[128] = "-";
static SceUID g_httpd_thread = -1;
static SceLwMutexWorkarea s_http_lock;
static int s_http_lock_ready;
static void http_lock(void) { sceKernelLockLwMutex(&s_http_lock,1,NULL); }
static void http_unlock(void) { sceKernelUnlockLwMutex(&s_http_lock,1); }

static int httpd_thread_main(SceSize args, void *argp);

static const char HTTP_FALLBACK[] =
	"HTTP/1.0 200 OK\r\n"
	"Content-Type: text/html; charset=utf-8\r\n"
	"Connection: close\r\n"
	"\r\n"
	"<!DOCTYPE html><html><head><meta charset=utf-8>"
	"<title>FLSUN Load</title></head><body style=\"font-family:sans-serif;background:#111;color:#eee\">"
	"<h1>FLSUN Load</h1>"
	"<p>Copy <code>www/index.html</code> next to the EBOOT (full preview page).</p>"
	"<p>Bare upload still works:</p>"
	"<input type=file id=f accept=.gcode,.gco,.g>"
	"<button id=b>Send gcode only</button><pre id=o>ready</pre>"
	"<script>"
	"document.getElementById('b').onclick=async()=>{"
	"const f=document.getElementById('f').files[0],o=document.getElementById('o');"
	"if(!f){o.textContent='pick file';return;}"
	"o.textContent='uploading…';"
	"try{const r=await fetch('/upload?name='+encodeURIComponent(f.name),"
	"{method:'POST',body:f,headers:{'Content-Type':'application/octet-stream'}});"
	"o.textContent=await r.text();}catch(e){o.textContent=''+e;}"
	"};"
	"</script></body></html>";

static void httpd_set_event(const char *msg)
{
	http_lock();
	strncpy(g_httpd_event, msg, sizeof(g_httpd_event) - 1);
	g_httpd_event[sizeof(g_httpd_event) - 1] = 0;
    http_unlock();
}

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

static int url_decode_copy(const char *src, int srclen, char *dst, int dstlen)
{
	int i = 0, j = 0;

	while (i < srclen && j < dstlen - 1) {
		if (src[i] == '%' && i + 2 < srclen) {
			int hi, lo;
			char h = src[i + 1], l = src[i + 2];
			hi = (h >= '0' && h <= '9') ? h - '0' :
			     (h >= 'A' && h <= 'F') ? h - 'A' + 10 :
			     (h >= 'a' && h <= 'f') ? h - 'a' + 10 : -1;
			lo = (l >= '0' && l <= '9') ? l - '0' :
			     (l >= 'A' && l <= 'F') ? l - 'A' + 10 :
			     (l >= 'a' && l <= 'f') ? l - 'a' + 10 : -1;
			if (hi >= 0 && lo >= 0) {
				dst[j++] = (char)((hi << 4) | lo);
				i += 3;
				continue;
			}
		}
		dst[j++] = (src[i] == '+') ? ' ' : src[i];
		i++;
	}
	dst[j] = 0;
	return j > 0 && i == srclen ? 0 : -1;
}

static int url_query_name(const char *path, char *name, int namelen)
{
	const char *q = strchr(path, '?');
	const char *p;
	int len = 0;

	name[0] = 0;
	if (!q)
		return -1;
	p = strstr(q, "name=");
	if (!p)
		return -1;
	p += 5;
	while (p[len] && p[len] != '&')
		len++;
	return url_decode_copy(p, len, name, namelen);
}

static int url_query_mode(const char *path, char *mode, int modelen)
{
	const char *q = strchr(path, '?');
	const char *p;
	int len = 0;

	mode[0] = 0;
	if (!q)
		return -1;
	p = strstr(q, "mode=");
	if (!p)
		return -1;
	p += 5;
	while (p[len] && p[len] != '&')
		len++;
	return url_decode_copy(p, len, mode, modelen);
}

static int content_disposition_name(const char *headers, char *name, int namelen)
{
	const char *p;
	const char *start;
	const char *end;
	char tmp[128];
	int n;

	name[0] = 0;
	if (!headers)
		return -1;
	p = strstr(headers, "Content-Disposition:");
	if (!p)
		p = strstr(headers, "content-disposition:");
	if (!p)
		return -1;
	p = strstr(p, "filename");
	if (!p)
		return -1;
	p += 8;
	if (*p == '*')
		p++;
	while (*p == ' ' || *p == '=')
		p++;
	if (*p == '"') {
		p++;
		start = p;
		end = strchr(p, '"');
		if (!end)
			return -1;
	} else {
		start = p;
		end = start;
		while (*end && *end != ';' && *end != '\r' && *end != '\n')
			end++;
	}
	n = (int)(end - start);
	if (n <= 0 || n >= (int)sizeof(tmp))
		return -1;
	memcpy(tmp, start, (size_t)n);
	tmp[n] = 0;
	return url_decode_copy(tmp, n, name, namelen);
}

/* Returns 0 if fully sent; -1 on short send (caller must not claim 200). */
static int send_text(int fd, int code, const char *ctype, const char *body)
{
	char hdr[320];
	int blen = (int)strlen(body);
	int n = snprintf(hdr, sizeof(hdr),
			 "HTTP/1.0 %d %s\r\nContent-Type: %s\r\n"
			 "Content-Length: %d\r\nConnection: close\r\n"
			 "Access-Control-Allow-Origin: *\r\n"
			 "Cache-Control: no-store\r\n\r\n",
			 code,
			 code == 200 ? "OK" : (code == 400 ? "Bad Request" :
			 code == 409 ? "Conflict" : "Error"),
			 ctype, blen);
	if (send_all(fd, hdr, n) != 0)
		return -1;
	if (send_all(fd, body, blen) != 0)
		return -1;
	return 0;
}

static int recv_request(int fd, char *buf, int cap, int timeout_ms)
{
	int total = 0;
	uint64_t deadline=sock_now_ms()+timeout_ms;

	sock_set_nonblock(fd, 1);
	while (total < cap - 1 && sock_now_ms()<deadline && !sock_stopping()) {
		int n = (int)sceNetInetRecv(fd, buf + total, (cap - 1) - total, 0);
		if (n > 0) {
			total += n;
			buf[total] = 0;
			if (strstr(buf, "\r\n\r\n") || strstr(buf, "\n\n"))
				break;
			continue;
		}
		if (n==0) break;
		sceKernelDelayThread(20 * 1000);
	}
	buf[total] = 0;
	return total;
}

static const char *www_content_type(const char *path)
{
	const char *dot = strrchr(path, '.');

	if (!dot)
		return "application/octet-stream";
	if (ascii_eq_ci(dot, ".html") || ascii_eq_ci(dot, ".htm"))
		return "text/html; charset=utf-8";
	if (ascii_eq_ci(dot, ".js") || ascii_eq_ci(dot, ".mjs"))
		return "application/javascript; charset=utf-8";
	if (ascii_eq_ci(dot, ".css"))
		return "text/css; charset=utf-8";
	if (ascii_eq_ci(dot, ".wasm"))
		return "application/wasm";
	if (ascii_eq_ci(dot, ".json") || ascii_eq_ci(dot, ".map"))
		return "application/json";
	if (ascii_eq_ci(dot, ".svg"))
		return "image/svg+xml";
	if (ascii_eq_ci(dot, ".png"))
		return "image/png";
	if (ascii_eq_ci(dot, ".ico"))
		return "image/x-icon";
	if (ascii_eq_ci(dot, ".txt"))
		return "text/plain; charset=utf-8";
	return "application/octet-stream";
}

static int www_resolve_path(const char *url_path, char *out, int outlen)
{
	char rel[192];
	int i;
	const char *p;

	if (!url_path || url_path[0] != '/')
		return -1;
	p = url_path;
	if (strcmp(p, "/") == 0)
		p = "/index.html";
	{
		const char *q = strchr(p, '?');
		int n = q ? (int)(q - p) : (int)strlen(p);
		if (n <= 0 || n >= (int)sizeof(rel))
			return -1;
		memcpy(rel, p, (size_t)n);
		rel[n] = 0;
	}
	for (i = 0; rel[i]; i++) {
		char c = rel[i];
		if (c == '%' || c == '\\')
			return -1;
	}
	for (i = 0; rel[i]; i++) {
		char c = rel[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '.' || c == '_' ||
		      c == '-' || c == '/'))
			return -1;
	}
	for (i = 0; rel[i]; i++) {
		if (rel[i] == '.' && rel[i + 1] == '.')
			return -1;
	}
	p = rel[0] == '/' ? rel + 1 : rel;
	if (!p[0])
		p = "index.html";
	snprintf(out, outlen, "%s/%s", WWW_DIR, p);
	return 0;
}

static int send_www_file(int fd, const char *url_path)
{
	char path[220];
	char hdr[220];
	char chunk[4096];
	SceUID f;
	SceIoStat st;
	int hn, n;

	cfg_ensure_dirs();
	if (www_resolve_path(url_path, path, sizeof(path)) != 0)
		return -1;
	if (sceIoGetstat(path, &st) < 0 || st.st_size < 0)
		return -1;
	if (st.st_size > 12 * 1024 * 1024)
		return -1;

	f = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (f < 0)
		return -1;

	hn = snprintf(hdr, sizeof(hdr),
		      "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n"
		      "Content-Length: %d\r\nConnection: close\r\n"
		      "Access-Control-Allow-Origin: *\r\n\r\n",
		      www_content_type(path), (int)st.st_size);
	if (send_all(fd, hdr, hn) != 0) {
		sceIoClose(f);
		return -1;
	}
	while ((n = sceIoRead(f, chunk, sizeof(chunk))) > 0) {
		if (send_all(fd, chunk, n) != 0) {
			sceIoClose(f);
			return -1;
		}
	}
	sceIoClose(f);
	return 0;
}

static void send_www_index(int fd)
{
	if (send_www_file(fd, "/index.html") == 0) {
		httpd_set_event("GET / www");
		return;
	}
	/* Fallback is a complete HTTP response already. */
	if (send_all(fd, HTTP_FALLBACK, (int)strlen(HTTP_FALLBACK)) != 0)
		httpd_set_event("GET / fallback short");
	else
		httpd_set_event("GET / fallback");
}

static int is_flpv_name(const char *name)
{
	const char *dot;
	int n;

	if (!name || !name[0] || name[0] == '.')
		return 0;
	n = (int)strlen(name);
	if (n > 9 && ascii_eq_ci(name + n - 9, "_sup.flpv"))
		return 1;
	dot = strrchr(name, '.');
	if (!dot)
		return 0;
	return ascii_eq_ci(dot, ".flpv");
}

static int is_gcode_name(const char *name)
{
	const char *dot;

	if (!name || !name[0] || name[0] == '.')
		return 0;
	dot = strrchr(name, '.');
	if (!dot)
		return 0;
	return ascii_eq_ci(dot, ".gcode") || ascii_eq_ci(dot, ".gco") ||
	       ascii_eq_ci(dot, ".g");
}

static int is_inbox_servable(const char *name)
{
	return is_gcode_name(name) || is_flpv_name(name);
}

/* Keep /inbox.json shape the web UI expects: synthetic <slug>.gcode names. */
static void send_inbox_list_json(int fd)
{
	ModelStorageList list;
	char json[8192];
	int n;
	int i;

	if (model_storage_list(&list) != 0) {
		send_text(fd, 500, "application/json",
			  "{\"error\":\"models open fail\",\"files\":[]}\n");
		httpd_set_event("GET inbox.json fail");
		return;
	}

	n = snprintf(json, sizeof(json), "{\"files\":[");
	for (i = 0; i < list.count; i++) {
		char syn[MODEL_STORAGE_NAME_LEN];
		int w;

		snprintf(syn, sizeof(syn), "%s.gcode", list.items[i].slug);
		if (n >= (int)sizeof(json) - 160)
			break;
		w = snprintf(json + n, sizeof(json) - (size_t)n,
			     "%s{\"name\":\"%s\",\"size\":%d,\"flpv\":%d,\"flpv_sup\":%d}",
			     i ? "," : "", syn, list.items[i].gcode_size,
			     list.items[i].has_preview, list.items[i].has_preview_sup);
		if (w < 0)
			break;
		n += w;
	}
	if (n < (int)sizeof(json) - 4)
		n += snprintf(json + n, sizeof(json) - (size_t)n, "]}\n");
	else
		strcpy(json + sizeof(json) - 5, "]}\n");
	send_text(fd, 200, "application/json", json);
	httpd_set_event("GET inbox.json");
}

static int send_inbox_file(int fd,const char *name)
{
    char slug[MODEL_STORAGE_SLUG_LEN],hdr[256];
    ModelFileKind kind; char *data=NULL; int size=0,n;
    if (!is_inbox_servable(name) ||
        model_storage_resolve_legacy_name(name,slug,sizeof(slug),&kind)) {
        send_text(fd,400,"text/plain","bad name\n"); return -1;
    }
    if (model_storage_read(slug,kind,&data,&size)<0) {
        send_text(fd,404,"text/plain","not found\n"); return -1;
    }
    n=snprintf(hdr,sizeof(hdr),"HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",size);
    int rc=send_all(fd,hdr,n) || send_all(fd,data,size);
    free(data); return rc ? -1 : 0;
}

static void path_no_query(const char *path, char *out, int outlen)
{
	const char *q;
	int n;

	if (!path || outlen < 2) {
		if (outlen > 0)
			out[0] = 0;
		return;
	}
	q = strchr(path, '?');
	n = q ? (int)(q - path) : (int)strlen(path);
	if (n >= outlen)
		n = outlen - 1;
	memcpy(out, path, (size_t)n);
	out[n] = 0;
}

static void send_slicer_cfg(int fd)
{
	SceUID f;
	SceIoStat st;
	char buf[SLICER_CFG_MAX + 1];
	char hdr[160];
	int n, hn;
	const char *fallback = "{}";

	cfg_ensure_dirs();
	if (sceIoGetstat(SLICER_CFG, &st) < 0 || st.st_size <= 0 ||
	    st.st_size > SLICER_CFG_MAX) {
		send_text(fd, 200, "application/json", fallback);
		httpd_set_event("GET slicer default");
		return;
	}
	f = sceIoOpen(SLICER_CFG, PSP_O_RDONLY, 0);
	if (f < 0) {
		send_text(fd, 200, "application/json", fallback);
		httpd_set_event("GET slicer openfail");
		return;
	}
	n = sceIoRead(f, buf, (int)st.st_size);
	sceIoClose(f);
	if (n <= 0) {
		send_text(fd, 200, "application/json", fallback);
		return;
	}
	buf[n] = 0;
	hn = snprintf(hdr, sizeof(hdr),
		      "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
		      "Content-Length: %d\r\nConnection: close\r\n"
		      "Access-Control-Allow-Origin: *\r\n"
		      "Cache-Control: no-store\r\n\r\n", n);
	if (send_all(fd, hdr, hn) != 0 || send_all(fd, buf, n) != 0)
		httpd_set_event("GET slicer short");
	else
		httpd_set_event("GET slicer.json");
}

static int write_slicer_cfg(const void *data, int len)
{
	SceUID f;

	if (len < 2 || len > SLICER_CFG_MAX)
		return -1;
	cfg_ensure_dirs();
	f = sceIoOpen(SLICER_CFG, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
	if (f < 0)
		return -1;
	if (sceIoWrite(f, data, len) != len) {
		sceIoClose(f);
		return -1;
	}
	sceIoClose(f);
	return 0;
}

static void handle_client(int cfd)
{
	char req[2048];
	char method[16], path[256];
	char name_q[96], safe[96];
	int content_len = -1;
	int n;
    uint64_t deadline;
	char *body = NULL;
	char *hdr_end;
	char *p;
	int got;
	int already;
	int fd = cfd;
	int max_body;
	int is_preview;

	if (fd <= 0)
		return;

	method[0] = path[0] = 0;
	memset(req, 0, sizeof(req));
	n = recv_request(fd, req, sizeof(req), 4000);
	if (n <= 0) {
		sceNetInetClose(fd);
		return;
	}

	sscanf(req, "%15s %255s", method, path);

	for (p = req; p && *p; ) {
		char *nl = strchr(p, '\n');
		if (!nl)
			break;
		if (strncasecmp(p, "Content-Length:", 15) == 0)
			content_len = atoi(p + 15);
		if (nl[1] == '\n' || (nl[0] == '\r' && nl[1] == '\n' && nl[2] == '\r'))
			break;
		if ((nl > p && nl[-1] == '\r' && nl[1] == '\r' && nl[2] == '\n') ||
		    (nl[1] == '\n'))
			break;
		p = nl + 1;
	}

	hdr_end = strstr(req, "\r\n\r\n");
	if (hdr_end)
		hdr_end += 4;
	else {
		hdr_end = strstr(req, "\n\n");
		if (hdr_end)
			hdr_end += 2;
	}

	if (!hdr_end) { send_text(fd,400,"text/plain","incomplete headers\n"); sceNetInetClose(fd); return; }

	if (strcmp(method, "GET") == 0) {
		char pclean[256];

		path_no_query(path, pclean, sizeof(pclean));
		if (strcmp(pclean, "/logs") == 0 || strcmp(pclean, "/logs/") == 0) {
			if (!log_net_active()) {
				send_text(fd, 404, "text/plain", "logging disabled\n");
				httpd_set_event("GET /logs off");
			} else {
				send_text(fd, 200, "text/html; charset=utf-8", log_html_page());
				httpd_set_event("GET /logs");
			}
		} else if (strncmp(pclean, "/api/logs", 9) == 0 &&
			   (pclean[9] == 0 || pclean[9] == '/')) {
			unsigned since = 0;
			unsigned next = 0;
			static char json[12288];
			const char *q;
			int njson;

			if (!log_net_active()) {
				send_text(fd, 404, "application/json",
					  "{\"error\":\"logging disabled\"}\n");
				httpd_set_event("GET /api/logs off");
			} else {
				q = strchr(path, '?');
				if (q) {
					q++;
					while (*q) {
						if (strncmp(q, "since=", 6) == 0) {
							since = (unsigned)atoi(q + 6);
							break;
						}
						while (*q && *q != '&')
							q++;
						if (*q == '&')
							q++;
					}
				}
				njson = log_export_since(since, json, (int)sizeof(json), &next);
				if (njson < 0)
					send_text(fd, 500, "application/json",
						  "{\"error\":\"buffer\"}\n");
				else
					send_text(fd, 200, "application/json", json);
				httpd_set_event("GET /api/logs");
			}
		} else if (strcmp(pclean, "/slicer.json") == 0) {
			send_slicer_cfg(fd);
		} else if (strcmp(pclean, "/inbox.json") == 0 ||
			   strcmp(pclean, "/models.json") == 0) {
			send_inbox_list_json(fd);
		} else if (strncmp(pclean, "/inbox/", 7) == 0 && pclean[7]) {
			send_inbox_file(fd, pclean + 7);
		} else if (strncmp(pclean, "/models/", 8) == 0 && pclean[8]) {
			send_inbox_file(fd, pclean + 8);
		} else if (strcmp(pclean, "/") == 0 ||
			   strcmp(pclean, "/index.html") == 0) {
			send_www_index(fd);
		} else if (pclean[0] == '/') {
			if (send_www_file(fd, pclean) != 0) {
				send_text(fd, 404, "text/plain", "not found\n");
				httpd_set_event("GET 404");
			} else {
				httpd_set_event("GET asset");
			}
		} else {
			send_text(fd, 404, "text/plain", "not found\n");
		}
		sceNetInetClose(fd);
		return;
	}

	{
		char pclean[256];

		path_no_query(path, pclean, sizeof(pclean));
		if (strcmp(method, "POST") == 0 &&
		    strcmp(pclean, "/slicer.json") == 0) {
			if (content_len < 2 || content_len > SLICER_CFG_MAX) {
				send_text(fd, 400, "text/plain", "bad slicer.json size\n");
				sceNetInetClose(fd);
				return;
			}
			body = (char *)malloc((size_t)content_len + 1);
			if (!body) {
				send_text(fd, 500, "text/plain", "oom\n");
				sceNetInetClose(fd);
				return;
			}
			already = 0;
			if (hdr_end) {
				already = n - (int)(hdr_end - req);
				if (already < 0)
					already = 0;
				if (already > content_len)
					already = content_len;
				if (already > 0)
					memcpy(body, hdr_end, (size_t)already);
			}
			got = already;
			deadline=sock_now_ms()+10000;
			sock_set_nonblock(fd, 1);
			while (got < content_len && sock_now_ms()<deadline && !sock_stopping()) {
				int r = (int)sceNetInetRecv(fd, body + got,
							    content_len - got, 0);
				if (r > 0) {
					got += r;
					continue;
				}
				if (r==0) break;
			sceKernelDelayThread(20 * 1000);
			}
			if (got != content_len) {
				free(body);
				send_text(fd, 400, "text/plain", "short body\n");
				sceNetInetClose(fd);
				return;
			}
			if (body[0] != '{') {
				free(body);
				send_text(fd, 400, "text/plain", "expect JSON object\n");
				sceNetInetClose(fd);
				return;
			}
			if (write_slicer_cfg(body, got) != 0) {
				free(body);
				send_text(fd, 500, "text/plain", "write fail\n");
				sceNetInetClose(fd);
				return;
			}
			free(body);
			send_text(fd, 200, "text/plain", "ok saved slicer.json\n");
			httpd_set_event("POST slicer.json");
			sceNetInetClose(fd);
			return;
		}
	}

	is_preview = (strcmp(method, "POST") == 0 && strncmp(path, "/preview", 8) == 0);
	if (strcmp(method, "POST") == 0 &&
	    (is_preview || strncmp(path, "/upload", 7) == 0 || strcmp(path, "/") == 0)) {
		char slug[MODEL_STORAGE_SLUG_LEN];
		ModelFileKind kind;
		int wr;

		max_body = is_preview ? cfg_max_preview() : cfg_max_upload();
		if (content_len < 0 || content_len > max_body) {
			send_text(fd, 400, "text/plain",
				  is_preview ? "bad Content-Length (max preview)\n"
					     : "bad Content-Length (max 4MiB)\n");
			httpd_set_event("POST reject: size");
			sceNetInetClose(fd);
			return;
		}
		if (url_query_name(path, name_q, sizeof(name_q)) != 0) {
			if (content_disposition_name(req, name_q, sizeof(name_q)) != 0)
				strncpy(name_q, is_preview ? "preview.flpv" : "upload.bin",
					sizeof(name_q) - 1);
		}
		if (model_storage_sanitize_filename(name_q,safe,sizeof(safe))) {
            send_text(fd,400,"text/plain","unsupported filename\n");
            sceNetInetClose(fd); return;
        }
		if (is_preview && !is_flpv_name(safe)) {
			char base[80];
			const char *dot = strrchr(safe, '.');
			int bl = dot ? (int)(dot - safe) : (int)strlen(safe);
			if (bl <= 0)
				bl = 7;
			if (bl > 70)
				bl = 70;
			memcpy(base, safe, (size_t)bl);
			base[bl] = 0;
			snprintf(safe, sizeof(safe), "%s.flpv", base);
		}

		body = (char *)malloc((size_t)content_len + 1);
		if (!body) {
			send_text(fd, 500, "text/plain", "oom\n");
			sceNetInetClose(fd);
			return;
		}

		already = 0;
		if (hdr_end) {
			already = n - (int)(hdr_end - req);
			if (already < 0)
				already = 0;
			if (already > content_len)
				already = content_len;
			if (already > 0)
				memcpy(body, hdr_end, (size_t)already);
		}
		got = already;
		deadline=sock_now_ms()+180000;
		sock_set_nonblock(fd, 1);
		while (got < content_len && sock_now_ms()<deadline && !sock_stopping()) {
			int r = (int)sceNetInetRecv(fd, body + got, content_len - got, 0);
			if (r > 0) {
				got += r;
				continue;
			}
			if (r==0) break;
			sceKernelDelayThread(20 * 1000);
		}
		if (got != content_len) {
			free(body);
			send_text(fd, 400, "text/plain", "short body\n");
			httpd_set_event("POST short body");
			sceNetInetClose(fd);
			return;
		}

		if (is_preview) {
			if (got < 8 || body[0] != 'F' || body[1] != 'L' ||
			    body[2] != 'P' || body[3] != 'V') {
				free(body);
				send_text(fd, 400, "text/plain", "not FLPV\n");
				httpd_set_event("POST preview bad magic");
				sceNetInetClose(fd);
				return;
			}
		}

		if (model_storage_resolve_legacy_name(safe, slug, (int)sizeof(slug),
						      &kind) != 0) {
			free(body);
			send_text(fd, 400, "text/plain", "bad name\n");
			httpd_set_event("POST bad name");
			sceNetInetClose(fd);
			return;
		}
		{
			char mode[16];
			int overwrite = 0;

			if (url_query_mode(path, mode, (int)sizeof(mode)) != 0)
				mode[0] = 0;
			if (mode[0] && strcmp(mode, "overwrite") != 0 && strcmp(mode, "keep_both") != 0) {
				free(body);
				send_text(fd, 400, "text/plain", "bad mode\n");
				sceNetInetClose(fd);
				return;
			}
			if (strcmp(mode, "keep_both") == 0) {
				if (model_storage_unique_slug(slug, slug, (int)sizeof(slug)) != 0) {
					free(body);
					send_text(fd, 500, "text/plain", "no free slug\n");
					sceNetInetClose(fd);
					return;
				}
			} else if (model_storage_exists(slug)) {
				if (strcmp(mode, "overwrite") != 0) {
					free(body);
					send_text(fd, 409, "text/plain", "exists: choose overwrite or keep_both\n");
					httpd_set_event("POST conflict");
					sceNetInetClose(fd);
					return;
				}
				overwrite = 1;
			}
			wr = model_storage_write(slug, kind, body, got, overwrite);
		}
		free(body);
		if (wr != 0) {
			send_text(fd, wr == -5 ? 409 : (wr == -2 ? 400 : 500), "text/plain",
				  wr == -5 ? "exists\n" : (wr == -2 ? "bad or oversized file\n" : "cannot write model\n"));
			httpd_set_event("POST write fail");
			sceNetInetClose(fd);
			return;
		}

		{
			char resp[192];
			snprintf(resp, sizeof(resp), "ok saved slug=%s file=%s (%d bytes)\n", slug, safe, got);
			if (send_text(fd, 200, "text/plain", resp) != 0) {
				httpd_set_event("POST ok but short send");
			} else {
				char event[128];
                snprintf(event,sizeof(event),is_preview ? "PREV %s (%d B)" : "POST %s (%d B)",safe,got);
                httpd_set_event(event);
			}
		}
		sceNetInetClose(fd);
		return;
	}

	send_text(fd, 404, "text/plain", "not found\n");
	httpd_set_event("404");
	sceNetInetClose(fd);
}

int httpd_start(int port)
{
	struct sockaddr_in name;
	int fd;
	int yes = 1;

    if (!s_http_lock_ready) {
        if (sceKernelCreateLwMutex(&s_http_lock,"http_state",0,0,NULL)<0) return -1;
        s_http_lock_ready=1;
    }
    httpd_stop();
    g_http_port = port;

	fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		snprintf(g_httpd_status, sizeof(g_httpd_status), "socket fail");
		return -1;
	}
	sceNetInetSetsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	memset(&name, 0, sizeof(name));
	name.sin_len = sizeof(name);
	name.sin_family = AF_INET;
	name.sin_port = htons((unsigned short)port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);

	if (sceNetInetBind(fd, (struct sockaddr *)&name, sizeof(name)) < 0) {
		snprintf(g_httpd_status, sizeof(g_httpd_status), "bind :%d fail", port);
		sceNetInetClose(fd);
		return -1;
	}
	if (sceNetInetListen(fd, 4) < 0) {
		snprintf(g_httpd_status, sizeof(g_httpd_status), "listen fail");
		sceNetInetClose(fd);
		return -1;
	}

	sock_set_nonblock(fd, 1);
	http_lock();
	g_listen_fd = fd;
	snprintf(g_httpd_status, sizeof(g_httpd_status), "listen %s:%d",
		 net_local_ip(), port);
    http_unlock();
	httpd_set_event("listening");
	if (g_httpd_thread < 0) {
		g_httpd_thread = sceKernelCreateThread("httpd", httpd_thread_main,
						       0x18, 48 * 1024, 0, NULL);
		if (g_httpd_thread<0 || sceKernelStartThread(g_httpd_thread,0,NULL)<0) {
            if (g_httpd_thread>=0) sceKernelDeleteThread(g_httpd_thread);
            g_httpd_thread=-1;httpd_stop();return -1;
        }
	}
	return 0;
}

void httpd_stop(void)
{
    if (!s_http_lock_ready) return;
    http_lock();
	if (g_listen_fd >= 0) {
		sceNetInetClose(g_listen_fd);
		g_listen_fd = -1;
	}
	snprintf(g_httpd_status, sizeof(g_httpd_status), "HTTP off");
    http_unlock();
}

static void httpd_accept_one(void)
{
	struct sockaddr_in client;
	int alen;
	int cfd;

	http_lock();
	if (g_listen_fd < 0) { http_unlock(); return; }

	memset(&client, 0, sizeof(client));
	alen = (int)sizeof(client);
	cfd = sceNetInetAccept(g_listen_fd, (struct sockaddr *)&client, (socklen_t *)&alen);
    http_unlock();
	if (cfd <= 0)
		return;

	sock_set_nonblock(cfd, 1);
	handle_client(cfd);
}

static int httpd_thread_main(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	while (!sock_stopping()) {
		httpd_accept_one();
		sceKernelDelayThread(10 * 1000);
	}
	return 0;
}

void httpd_poll(void)
{
}

int httpd_listening(void)
{
	if (!s_http_lock_ready) return 0;
    http_lock(); int on=g_listen_fd>=0; http_unlock(); return on;
}

const char *httpd_status(void)
{
	static char copy[64];
    if (!s_http_lock_ready) return "HTTP off";
    http_lock(); memcpy(copy,g_httpd_status,sizeof(copy)); http_unlock();
    return copy;
}

const char *httpd_last_event(void)
{
	static char copy[128];
    if (!s_http_lock_ready) return "-";
    http_lock(); memcpy(copy,g_httpd_event,sizeof(copy)); http_unlock();
    return copy;
}

void httpd_note_local_ip(const char *ip)
{
	if (!ip || !ip[0] || !s_http_lock_ready) return;
    http_lock();
    if (g_listen_fd<0) {http_unlock();return;}
	snprintf(g_httpd_status, sizeof(g_httpd_status),
		 "listen %s:%d", ip, g_http_port);
    http_unlock();
}

void httpd_shutdown(void)
{
    httpd_stop();
    if (g_httpd_thread>=0) {
        sceKernelWaitThreadEnd(g_httpd_thread,NULL);
        sceKernelDeleteThread(g_httpd_thread);g_httpd_thread=-1;
    }
}
