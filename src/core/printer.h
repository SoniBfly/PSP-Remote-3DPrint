/*
 * Neutral printer model + async-shaped request API.
 * Stage 7: requests are queued to net/worker; screens keep the same signatures.
 *
 * Screens read fields every frame. No completion callbacks.
 */
#ifndef FLSUN_PRINTER_H
#define FLSUN_PRINTER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRINTER_NAME_LEN  64
#define PRINTER_FILES_MAX 96  /* MKS_MAX_FILES + MODEL_STORAGE_MAX_MODELS */

/* Source / artifact flags for a merged list entry. */
#define FILE_ON_SD          (1u << 0)
#define FILE_ON_PSP         (1u << 1)
#define FILE_HAS_PREVIEW    (1u << 2)
#define FILE_HAS_PREVIEW_SUP (1u << 3)

struct file_entry {
	char name[PRINTER_NAME_LEN]; /* SD filename or synthetic "<slug>.gcode" */
	int size;                    /* gcode bytes, or -1 if unknown */
	unsigned flags;
};

struct file_list {
	struct file_entry items[PRINTER_FILES_MAX];
	int count;
	char err[96];
};

typedef enum {
	PRINTER_STATUS_UNKNOWN = 0,
	PRINTER_STATUS_IDLE,
	PRINTER_STATUS_PRINTING,
	PRINTER_STATUS_PAUSED
} printer_status_t;

/* Return codes for printer_request_*: 0 accepted/ok, -1 failed, -2 busy. */
#define PRINTER_OK     0
#define PRINTER_ERR   -1
#define PRINTER_BUSY  -2

typedef void (*printer_upload_progress_fn)(int sent, int total, void *user);

struct printer {
	int busy;

	int temps_ok;
	float hotend;
	float hotend_target;
	float bed;
	float bed_target;

	printer_status_t status;
	float progress; /* 0..100 or -1 */
	char current_file[PRINTER_NAME_LEN];

	struct file_list files;
    unsigned files_version, requests_version;

	char last_error[96];
	int last_request; /* PRINTER_OK / ERR / BUSY of the last request_* */
	int upload_sent;
	int upload_total;
};

struct printer_result {
    int type, rc;
    char label[24], error[96];
};
void printer_init(void);
int printer_poll(void);
int printer_next_result(struct printer_result *out);
struct printer *printer_get(void);
int printer_is_busy(void);

/* Optional legacy progress sink; worker updates upload_sent/upload_total instead. */
void printer_set_upload_progress(printer_upload_progress_fn fn, void *user);

/*
 * Requests queue work and return immediately. Results land in struct printer.
 * PRINTER_BUSY means the fixed-size queue is full.
 */
int printer_request_temps(void);
int printer_request_status(void);
int printer_request_files(void); /* M20 + model_storage merge → files */
int printer_request_print(const struct file_entry *entry);
int printer_request_upload(const char *name, int and_print);
int printer_request_delete_sd(const char *name);
int printer_request_delete_psp(const char *name);
int printer_request_pause(void);
int printer_request_resume(void);
int printer_request_home(void);
int printer_request_set_hotend(int celsius);
int printer_request_set_bed(int celsius);
int printer_request_set_speed(int percent);
int printer_request_set_flow(int percent);

/* Read helpers (same as reading printer_get()->field). */
const struct file_list *printer_files(void);

struct net_task;
void printer_queue_push(void);
void printer_queue_done(void);
void printer_worker_execute(const struct net_task *task);
void printer_worker_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_PRINTER_H */
