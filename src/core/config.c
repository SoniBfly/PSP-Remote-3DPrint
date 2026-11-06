/*
 * Runtime + build-time configuration.
 * Outside this file: getters/setters only. Dirty flag gates Memory Stick writes.
 */
#include "config.h"
#include "log.h"
#include "file_io.h"

#include <pspiofilemgr.h>
#include <pspkernel.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================== */
/* --- build-time fixed ---                                            */
/* Paths and limits that are compiled in; not read from flsun.ini.     */
/* ================================================================== */

static const char S_MS_ROOT[]     = "ms0:/PSP/GAME/FLSUNRemote";
static const char S_INI_PATH[]    = "ms0:/PSP/GAME/FLSUNRemote/flsun.ini";
static const char S_INBOX_DIR[]   = "ms0:/PSP/GAME/FLSUNRemote/inbox";
static const char S_MODELS_DIR[]  = "ms0:/PSP/GAME/FLSUNRemote/models";
static const char S_WWW_DIR[]     = "ms0:/PSP/GAME/FLSUNRemote/www";
static const char S_SLICER_JSON[] = "ms0:/PSP/GAME/FLSUNRemote/slicer.json";

/* Neutral placeholder — not a home LAN address. See flsun.ini.example. */
static const char S_DEFAULT_PRINTER_IP[] = "192.168.1.50";

/* ================================================================== */
/* --- from flsun.ini ---                                              */
/* Mutable fields with defaults; dirty tracks unsaved edits.           */
/* ================================================================== */

static struct {
	char printer_ip[32];
	char local_ip[32];
	int  mks_port;
	int  http_port;
	int  pla_hotend;
	int  pla_bed;
	int  speed_pct;
	int  flow_pct;
	int  log_mode;
	int  log_port;
} s_ini;

static int s_dirty;
static SceLwMutexWorkarea s_connection_lock;
static int s_connection_lock_ready;

int cfg_copy_printer_connection(struct cfg_printer_connection *out)
{
    if (!out || !s_connection_lock_ready) return -1;
    sceKernelLockLwMutex(&s_connection_lock,1,NULL);
    memcpy(out->ip,s_ini.printer_ip,sizeof(out->ip));
    out->port=s_ini.mks_port;
    sceKernelUnlockLwMutex(&s_connection_lock,1);
    return 0;
}

static void strip_cr(char *s)
{
	char *cr;

	if (!s)
		return;
	cr = strchr(s, '\r');
	if (cr)
		*cr = 0;
}

static void set_defaults(void)
{
	memset(&s_ini, 0, sizeof(s_ini));
	strncpy(s_ini.printer_ip, S_DEFAULT_PRINTER_IP, sizeof(s_ini.printer_ip) - 1);
	s_ini.local_ip[0] = 0;
	s_ini.mks_port = CFG_DEFAULT_MKS_PORT;
	s_ini.http_port = CFG_DEFAULT_HTTP_PORT;
	s_ini.pla_hotend = 200;
	s_ini.pla_bed = 60;
	s_ini.speed_pct = 100;
	s_ini.flow_pct = 100;
	s_ini.log_mode = LOG_MODE_OFF;
	s_ini.log_port = FLSUN_DEFAULT_LOG_PORT;
	s_dirty = 0;
}

void cfg_ensure_dirs(void)
{
	sceIoMkdir("ms0:/PSP", 0777);
	sceIoMkdir("ms0:/PSP/GAME", 0777);
	sceIoMkdir(S_MS_ROOT, 0777);
	sceIoMkdir(S_INBOX_DIR, 0777);
	sceIoMkdir(S_MODELS_DIR, 0777);
	sceIoMkdir(S_WWW_DIR, 0777);
}

static int write_ini(void)
{
	SceUID fd;
	char buf[448];
	int n;
	int wr;

	cfg_ensure_dirs();
	n = snprintf(buf, sizeof(buf),
		     "printer_ip=%s\nlocal_ip=%s\nhttp_port=%d\nmks_port=%d\n"
		     "pla_hotend=%d\npla_bed=%d\nspeed_pct=%d\nflow_pct=%d\n"
		     "log=%s\nlog_port=%d\n",
		     s_ini.printer_ip,
		     s_ini.local_ip[0] ? s_ini.local_ip : "",
		     s_ini.http_port, s_ini.mks_port,
		     s_ini.pla_hotend, s_ini.pla_bed,
		     s_ini.speed_pct, s_ini.flow_pct,
		     s_ini.log_mode == LOG_MODE_NET ? "net" : "off",
		     s_ini.log_port > 0 ? s_ini.log_port : FLSUN_DEFAULT_LOG_PORT);
	if (n <= 0 || n >= (int)sizeof(buf))
		return -1;

	char pending[200];
    snprintf(pending,sizeof(pending),"%s.pending",S_INI_PATH);
    fd = sceIoOpen(pending, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
	if (fd < 0)
		return -1;
	wr = sceIoWrite(fd, buf, n);
	sceIoClose(fd);
	if (wr != n) {sceIoRemove(pending);return -1;}
    if (file_replace(pending,S_INI_PATH)<0) {sceIoRemove(pending);return -1;}
	s_dirty = 0;
	return 0;
}

void cfg_load(void)
{
	SceUID fd;
	char buf[512];
	int n;
	char *line, *p;

    if (!s_connection_lock_ready &&
        sceKernelCreateLwMutex(&s_connection_lock,"config_connection",0,0,NULL)>=0)
        s_connection_lock_ready=1;
    /* Loading is a startup/UI operation, before any network threads run. */
	set_defaults();
	cfg_ensure_dirs();

	file_recover(S_INI_PATH);
    fd = sceIoOpen(S_INI_PATH, PSP_O_RDONLY, 0);
	if (fd < 0)
		return;

	n = sceIoRead(fd, buf, sizeof(buf) - 1);
	sceIoClose(fd);
	if (n <= 0)
		return;
	buf[n] = 0;

	line = buf;
	while (line && *line) {
		p = strchr(line, '\n');
		if (p)
			*p++ = 0;
		while (*line == ' ' || *line == '\t' || *line == '\r')
			line++;
		if (strncmp(line, "printer_ip=", 11) == 0) {
			strncpy(s_ini.printer_ip, line + 11, sizeof(s_ini.printer_ip) - 1);
			s_ini.printer_ip[sizeof(s_ini.printer_ip) - 1] = 0;
			strip_cr(s_ini.printer_ip);
		} else if (strncmp(line, "local_ip=", 9) == 0) {
			strncpy(s_ini.local_ip, line + 9, sizeof(s_ini.local_ip) - 1);
			s_ini.local_ip[sizeof(s_ini.local_ip) - 1] = 0;
			strip_cr(s_ini.local_ip);
		} else if (strncmp(line, "http_port=", 10) == 0) {
			int v = atoi(line + 10);
			if (v > 0 && v < 65536)
				s_ini.http_port = v;
		} else if (strncmp(line, "mks_port=", 9) == 0) {
			int v = atoi(line + 9);
			if (v > 0 && v < 65536)
				s_ini.mks_port = v;
		} else if (strncmp(line, "pla_hotend=", 11) == 0) {
			int v = atoi(line + 11);
			if (v >= 0 && v <= 300)
				s_ini.pla_hotend = v;
		} else if (strncmp(line, "pla_bed=", 8) == 0) {
			int v = atoi(line + 8);
			if (v >= 0 && v <= 120)
				s_ini.pla_bed = v;
		} else if (strncmp(line, "speed_pct=", 10) == 0) {
			int v = atoi(line + 10);
			if (v >= 10 && v <= 300)
				s_ini.speed_pct = v;
		} else if (strncmp(line, "flow_pct=", 9) == 0) {
			int v = atoi(line + 9);
			if (v >= 10 && v <= 300)
				s_ini.flow_pct = v;
		} else if (strncmp(line, "log_port=", 9) == 0) {
			int v = atoi(line + 9);
			if (v > 0 && v < 65536)
				s_ini.log_port = v;
		} else if (strncmp(line, "log=", 4) == 0) {
			char v[16];
			strncpy(v, line + 4, sizeof(v) - 1);
			v[sizeof(v) - 1] = 0;
			strip_cr(v);
			if (strcmp(v, "net") == 0)
				s_ini.log_mode = LOG_MODE_NET;
			else
				s_ini.log_mode = LOG_MODE_OFF;
		}
		line = p;
	}
	s_dirty = 0;
}

int cfg_save_if_dirty(void)
{
	if (!s_dirty)
		return 0;
	return write_ini();
}

int cfg_save_force(void)
{
	return write_ini();
}

void cfg_mark_dirty(void)
{
	s_dirty = 1;
}

int cfg_is_dirty(void)
{
	return s_dirty;
}

/* --- ini getters --- */

const char *cfg_printer_ip(void)
{
	return s_ini.printer_ip;
}

const char *cfg_local_ip(void)
{
	return s_ini.local_ip;
}

int cfg_mks_port(void)
{
	return s_ini.mks_port;
}

int cfg_http_port(void)
{
	return s_ini.http_port;
}

int cfg_pla_hotend(void)
{
	return s_ini.pla_hotend;
}

int cfg_pla_bed(void)
{
	return s_ini.pla_bed;
}

int cfg_speed_pct(void)
{
	return s_ini.speed_pct;
}

int cfg_flow_pct(void)
{
	return s_ini.flow_pct;
}

int cfg_log_mode(void)
{
	return s_ini.log_mode;
}

int cfg_log_port(void)
{
	return s_ini.log_port;
}

/* --- ini setters --- */

void cfg_set_printer_ip(const char *ip)
{
	char tmp[32] = {0};

	if (!ip)
		ip = "";
	strncpy(tmp, ip, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = 0;
    if (s_connection_lock_ready) sceKernelLockLwMutex(&s_connection_lock,1,NULL);
    if (strcmp(s_ini.printer_ip,tmp)!=0) {
        memcpy(s_ini.printer_ip,tmp,sizeof(s_ini.printer_ip));
        s_dirty=1;
    }
    if (s_connection_lock_ready) sceKernelUnlockLwMutex(&s_connection_lock,1);
}

void cfg_set_local_ip(const char *ip)
{
	char tmp[32] = {0};

	if (!ip)
		ip = "";
	strncpy(tmp, ip, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = 0;
	if (strcmp(s_ini.local_ip, tmp) == 0)
		return;
	memcpy(s_ini.local_ip, tmp, sizeof(s_ini.local_ip));
	s_dirty = 1;
}

void cfg_set_mks_port(int v)
{
    if (v<=0 || v>=65536) return;
    if (s_connection_lock_ready) sceKernelLockLwMutex(&s_connection_lock,1,NULL);
    if (v!=s_ini.mks_port) {s_ini.mks_port=v;s_dirty=1;}
    if (s_connection_lock_ready) sceKernelUnlockLwMutex(&s_connection_lock,1);
}

void cfg_set_http_port(int v)
{
	if (v <= 0 || v >= 65536 || v == s_ini.http_port)
		return;
	s_ini.http_port = v;
	s_dirty = 1;
}

void cfg_set_pla_hotend(int v)
{
	if (v < 0)
		v = 0;
	if (v > 300)
		v = 300;
	if (v == s_ini.pla_hotend)
		return;
	s_ini.pla_hotend = v;
	s_dirty = 1;
}

void cfg_set_pla_bed(int v)
{
	if (v < 0)
		v = 0;
	if (v > 120)
		v = 120;
	if (v == s_ini.pla_bed)
		return;
	s_ini.pla_bed = v;
	s_dirty = 1;
}

void cfg_set_speed_pct(int v)
{
	if (v < 10)
		v = 10;
	if (v > 300)
		v = 300;
	if (v == s_ini.speed_pct)
		return;
	s_ini.speed_pct = v;
	s_dirty = 1;
}

void cfg_set_flow_pct(int v)
{
	if (v < 10)
		v = 10;
	if (v > 300)
		v = 300;
	if (v == s_ini.flow_pct)
		return;
	s_ini.flow_pct = v;
	s_dirty = 1;
}

void cfg_set_log_mode(int mode)
{
	if (mode != LOG_MODE_NET)
		mode = LOG_MODE_OFF;
	if (mode == s_ini.log_mode)
		return;
	s_ini.log_mode = mode;
	s_dirty = 1;
}

void cfg_set_log_port(int port)
{
	if (port <= 0 || port >= 65536 || port == s_ini.log_port)
		return;
	s_ini.log_port = port;
	s_dirty = 1;
}

void cfg_bump_printer_ip_octet_at(int idx, int delta)
{
	unsigned o[4];
	char tmp[32] = {0};
	int i;

	if (idx < 0 || idx > 3)
		return;
	if (sscanf(s_ini.printer_ip, "%u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) != 4)
		return;
	for (i = 0; i < 4; i++)
		if (o[i] > 255)
			return;
	if (delta > 0) {
		if (o[idx] < 254)
			o[idx]++;
		else
			o[idx] = 1;
	} else {
		if (o[idx] > 1)
			o[idx]--;
		else
			o[idx] = 254;
	}
	snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
	cfg_set_printer_ip(tmp);
}

/* --- build-time getters --- */

const char *cfg_ms_root(void)
{
	return S_MS_ROOT;
}

const char *cfg_ini_path(void)
{
	return S_INI_PATH;
}

const char *cfg_inbox_dir(void)
{
	return S_INBOX_DIR;
}

const char *cfg_models_dir(void)
{
	return S_MODELS_DIR;
}

const char *cfg_www_dir(void)
{
	return S_WWW_DIR;
}

const char *cfg_slicer_json_path(void)
{
	return S_SLICER_JSON;
}

int cfg_screen_w(void)
{
	return CFG_SCREEN_W;
}

int cfg_screen_h(void)
{
	return CFG_SCREEN_H;
}

int cfg_max_upload(void)
{
	return CFG_MAX_UPLOAD;
}

int cfg_max_preview(void)
{
	return CFG_MAX_PREVIEW;
}

int cfg_mks_http_port(void)
{
	return CFG_MKS_HTTP_PORT;
}
