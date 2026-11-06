/*
 * Single access point for runtime (flsun.ini) and build-time settings.
 * Callers use getters/setters; they do not care whether a value came from
 * the Memory Stick or was compiled in.
 */
#ifndef FLSUN_CONFIG_H
#define FLSUN_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- build-time fixed (also usable as macros from net/httpd) --- */
#define CFG_MAX_UPLOAD        (4 * 1024 * 1024) /* sliced gcode / upload body */
#define CFG_MAX_PREVIEW       (200 * 1024)      /* browser FLPV RGB565 */
#define CFG_SLICER_JSON_MAX   4096
#define CFG_DEFAULT_MKS_PORT  8080
#define CFG_DEFAULT_HTTP_PORT 8081
#define CFG_MKS_HTTP_PORT     80                /* printer HTTP /upload */
#define CFG_SCREEN_W          480
#define CFG_SCREEN_H          272

/* Compatibility aliases for call sites that still use the old names. */
#define FLSUN_MAX_UPLOAD        CFG_MAX_UPLOAD
#define FLSUN_MAX_PREVIEW       CFG_MAX_PREVIEW
#define FLSUN_MKS_HTTP_PORT      CFG_MKS_HTTP_PORT
#define FLSUN_DEFAULT_MKS_PORT   CFG_DEFAULT_MKS_PORT
#define FLSUN_DEFAULT_HTTP_PORT  CFG_DEFAULT_HTTP_PORT

/* Load ini (or defaults). Creates ms0 game dirs as needed. */
void cfg_load(void);

/* Write ini only when dirty. 0 = ok (or nothing to write), -1 = I/O error. */
int cfg_save_if_dirty(void);

/* Always write ini (Dash Square). Clears dirty on success. */
int cfg_save_force(void);

void cfg_mark_dirty(void);
int  cfg_is_dirty(void);

/* Ensure ms0:/PSP/GAME/FLSUNRemote/{,inbox,models,www} exist. */
void cfg_ensure_dirs(void);

/* Worker-safe value copy; pointer getters below remain UI-thread APIs. */
struct cfg_printer_connection { char ip[32]; int port; };
int cfg_copy_printer_connection(struct cfg_printer_connection *out);

/* --- getters: flsun.ini fields --- */
const char *cfg_printer_ip(void);
const char *cfg_local_ip(void);
int         cfg_mks_port(void);
int         cfg_http_port(void);
int         cfg_pla_hotend(void);
int         cfg_pla_bed(void);
int         cfg_speed_pct(void);
int         cfg_flow_pct(void);
int         cfg_log_mode(void);
int         cfg_log_port(void);

/* --- setters: mark dirty when the value actually changes --- */
void cfg_set_printer_ip(const char *ip);
void cfg_set_local_ip(const char *ip);
void cfg_set_mks_port(int v);
void cfg_set_http_port(int v);
void cfg_set_pla_hotend(int v);
void cfg_set_pla_bed(int v);
void cfg_set_speed_pct(int v);
void cfg_set_flow_pct(int v);
void cfg_set_log_mode(int mode);
void cfg_set_log_port(int port);

/* Dash editing: bump IPv4 octet idx 0..3 (wraps 1..254). Marks dirty. */
void cfg_bump_printer_ip_octet_at(int idx, int delta);

/* --- getters: build-time paths / limits --- */
const char *cfg_ms_root(void);
const char *cfg_ini_path(void);
const char *cfg_inbox_dir(void);
const char *cfg_models_dir(void);
const char *cfg_www_dir(void);
const char *cfg_slicer_json_path(void);
int         cfg_screen_w(void);
int         cfg_screen_h(void);
int         cfg_max_upload(void);
int         cfg_max_preview(void);
int         cfg_mks_http_port(void);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_CONFIG_H */
