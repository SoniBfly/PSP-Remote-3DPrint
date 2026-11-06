/*
 * MKS Wi-Fi printer protocol: TCP :8080 G-code, HTTP :80 /upload.
 * Address/port from cfg_printer_ip() / cfg_mks_port().
 */
#ifndef FLSUN_PRINTER_MKS_H
#define FLSUN_PRINTER_MKS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MKS_MAX_FILES 64
#define MKS_NAME_LEN  64

typedef struct {
	int   ok;
	float hotend;
	float hotend_target;
	float bed;
	float bed_target;
	char  raw[256];
	char  err[96];
} MksTemps;

typedef struct {
	char names[MKS_MAX_FILES][MKS_NAME_LEN];
	int  count;
	char err[96];
} MksFileList;

typedef struct {
	int   ok;
	int   printing;   /* 1 = SD printing / active */
	float progress;   /* 0..100 or -1 if unknown */
	char  raw[128];
	char  err[96];
} MksPrintStatus;

int  mks_fetch_temps(MksTemps *out);
int  mks_list_files(MksFileList *out);
/* M23 only → Size (bytes). Does not start print. -1 on fail. */
int  mks_file_size(const char *filename, int *size_out);
int  mks_print_status(MksPrintStatus *out);
int  mks_select_and_print(const char *filename, char *err, int errlen);
int  mks_pause(char *err, int errlen);
int  mks_resume(char *err, int errlen);
/* M30: 0 = deleted, -1 = failed (Deletion failed / connect). */
int  mks_delete(const char *filename, char *err, int errlen);

int  mks_set_hotend(int celsius, char *err, int errlen);
int  mks_set_bed(int celsius, char *err, int errlen);
int  mks_set_speed(int percent, char *err, int errlen);
int  mks_set_flow(int percent, char *err, int errlen);
/* G28 — moves axes; caller must confirm in UI first. */
int  mks_home(char *err, int errlen);

typedef void (*FlsunUploadProgress)(int sent, int total, void *user);
int  mks_upload_ex(const char *filename,
		   const void *data, int size,
		   FlsunUploadProgress prog, void *prog_user,
		   char *err, int errlen);
int  mks_upload(const char *filename,
		const void *data, int size, char *err, int errlen);
/* After upload: poll M20 until name appears (or timeout). 0 = seen. */
int  mks_wait_file_on_sd(const char *filename,
			 int timeout_ms, char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_PRINTER_MKS_H */
