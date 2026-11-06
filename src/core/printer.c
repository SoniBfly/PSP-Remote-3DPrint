/*
 * Printer model and net/worker request execution.
 * Results go into struct printer only — no completion callbacks.
 */
#include "printer.h"
#include "model_storage.h"
#include "printer_mks.h"
#include "worker.h"
#include "log.h"
#include "socket.h"
#include <pspthreadman.h>

#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only the worker mutates s_printer. UI sees a once-per-frame snapshot. */
static struct printer s_printer, s_published, s_ui;
static SceLwMutexWorkarea s_state_lock;
static int s_state_ready;
#define RESULT_CAP 32
static struct printer_result s_results[RESULT_CAP];
static int s_result_head, s_result_count;
static struct printer_result s_overflow_error;
static int s_overflow_error_pending;

static void publish(void)
{
    sceKernelLockLwMutex(&s_state_lock,1,NULL);
    int busy = s_published.busy;
    unsigned requests=s_published.requests_version;
    s_published=s_printer;
    s_published.busy=busy;
    s_published.requests_version=requests;
    sceKernelUnlockLwMutex(&s_state_lock,1);
}
static printer_upload_progress_fn s_prog_fn;
static void *s_prog_user;

static int name_eq_ci(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = *a, cb = *b;

		if (ca >= 'A' && ca <= 'Z')
			ca += 32;
		if (cb >= 'A' && cb <= 'Z')
			cb += 32;
		if (ca != cb)
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

static int s_queued;

static int begin_request(void)
{
	s_printer.last_error[0] = 0;
	return 0;
}

static int end_request(int rc)
{
	s_printer.last_request = (rc == 0) ? PRINTER_OK : PRINTER_ERR;
	return s_printer.last_request;
}

void printer_queue_push(void)
{
    sceKernelLockLwMutex(&s_state_lock,1,NULL);
	s_queued++;
    s_published.requests_version++;
	s_published.busy = s_queued > 0;
    sceKernelUnlockLwMutex(&s_state_lock,1);
}

void printer_queue_done(void)
{
    sceKernelLockLwMutex(&s_state_lock,1,NULL);
	if (s_queued > 0)
		s_queued--;
	s_published.busy = s_queued > 0;
    sceKernelUnlockLwMutex(&s_state_lock,1);
}

static void set_err(const char *msg)
{
	snprintf(s_printer.last_error, sizeof(s_printer.last_error), "%s",
		 msg && msg[0] ? msg : "error");
}

static void apply_print_status(const MksPrintStatus *ps)
{
	if (!ps || !ps->ok) {
		s_printer.status = PRINTER_STATUS_UNKNOWN;
		s_printer.progress = -1.0f;
		return;
	}
	s_printer.progress = ps->progress;
	if (ps->printing)
		s_printer.status = PRINTER_STATUS_PRINTING;
	else
		s_printer.status = PRINTER_STATUS_IDLE;
}

static int merge_lists(const MksFileList *sd, const ModelStorageList *local)
{
	struct file_list *out = &s_printer.files;
	int i, n = 0;

	memset(out, 0, sizeof(*out));

	for (i = 0; i < sd->count && n < PRINTER_FILES_MAX; i++) {
		struct file_entry *e = &out->items[n];

		strncpy(e->name, sd->names[i], PRINTER_NAME_LEN - 1);
		e->name[PRINTER_NAME_LEN - 1] = 0;
		e->size = -1;
		e->flags = FILE_ON_SD;
		n++;
	}

	for (i = 0; i < local->count && n < PRINTER_FILES_MAX; i++) {
		const ModelStorageEntry *m = &local->items[i];
		char syn[PRINTER_NAME_LEN];
		int j, found = -1;
		unsigned flags = FILE_ON_PSP;

		snprintf(syn, sizeof(syn), "%.*s.gcode", MODEL_STORAGE_SLUG_MAX, m->slug);
		if (m->has_preview)
			flags |= FILE_HAS_PREVIEW;
		if (m->has_preview_sup)
			flags |= FILE_HAS_PREVIEW_SUP;

		for (j = 0; j < n; j++) {
			if (name_eq_ci(out->items[j].name, syn)) {
				found = j;
				break;
			}
		}
		if (found >= 0) {
			out->items[found].flags |= flags;
			if (m->gcode_size > 0)
				out->items[found].size = m->gcode_size;
		} else {
			struct file_entry *e = &out->items[n];

			snprintf(e->name,sizeof(e->name),"%s",syn);
			e->name[PRINTER_NAME_LEN - 1] = 0;
			e->size = m->gcode_size > 0 ? m->gcode_size : -1;
			e->flags = flags;
			n++;
		}
	}

	out->count = n;
    s_printer.files_version++;
	return n;
}

static void upload_prog_bridge(int sent, int total, void *user)
{
	(void)user;
	s_printer.upload_sent = sent;
	s_printer.upload_total = total;
    publish();
}

/*
 * Read local gcode by synthetic "<slug>.gcode" or bare slug, upload to SD,
 * wait for M20, optionally M23/M24 print.
 */
static int upload_local(const char *name, int and_print)
{
	char slug[MODEL_STORAGE_SLUG_LEN];
	ModelFileKind kind;
	char *data = NULL;
	int size = 0;
	char err[96];
	char sd_name[PRINTER_NAME_LEN];
	int rc;

	if (!name || !name[0]) {
		set_err("bad name");
		return -1;
	}

	if (model_storage_resolve_legacy_name(name, slug, (int)sizeof(slug), &kind) != 0 ||
	    kind != MODEL_FILE_GCODE) {
		/* Bare slug fallback. */
		if (model_storage_sanitize_slug(name, slug, (int)sizeof(slug)) != 0) {
			set_err("bad name");
			return -1;
		}
	}

	snprintf(sd_name, sizeof(sd_name), "%.*s.gcode", MODEL_STORAGE_SLUG_MAX, slug);
	rc = model_storage_read(slug, MODEL_FILE_GCODE, &data, &size);
	if (rc < 0 || !data) {
		set_err(rc == -2 ? "too big / empty (max 4M)" : "read FAIL");
		return -1;
	}

	if (mks_upload_ex(sd_name, data, size,
			  upload_prog_bridge, NULL, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "upload fail");
		free(data);
		return -1;
	}

	if (mks_wait_file_on_sd(sd_name, 6000, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "not in M20");
		free(data);
		return -1;
	}
	free(data);

	if (!and_print)
		return 0;

	if (mks_select_and_print(sd_name, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "print fail");
		return -1;
	}
	return 0;
}

void printer_init(void)
{
	memset(&s_printer, 0, sizeof(s_printer));
	s_printer.progress = -1.0f;
	s_printer.status = PRINTER_STATUS_UNKNOWN;
	s_prog_fn = NULL;
	s_prog_user = NULL;
    if (sceKernelCreateLwMutex(&s_state_lock,"printer_state",0,0,NULL)>=0) {
        s_state_ready=1;
        s_published=s_ui=s_printer;
    }
}

struct printer *printer_get(void)
{
	return &s_ui;
}

int printer_is_busy(void)
{
	int busy;
    if (!s_state_ready) return 1;
    sceKernelLockLwMutex(&s_state_lock,1,NULL);
    busy=s_published.busy;
    sceKernelUnlockLwMutex(&s_state_lock,1);
    return busy;
}

void printer_set_upload_progress(printer_upload_progress_fn fn, void *user)
{
	s_prog_fn = fn;
	s_prog_user = user;
}

const struct file_list *printer_files(void)
{
	return &s_ui.files;
}

static int worker_request_temps(void)
{
	MksTemps t;

	if (begin_request() != 0)
		return PRINTER_BUSY;

	memset(&t, 0, sizeof(t));
	if (mks_fetch_temps(&t) != 0) {
		s_printer.temps_ok = 0;
		set_err(t.err[0] ? t.err : "M105 FAIL");
		return end_request(-1);
	}
	s_printer.temps_ok = t.ok ? 1 : 0;
	s_printer.hotend = t.hotend;
	s_printer.hotend_target = t.hotend_target;
	s_printer.bed = t.bed;
	s_printer.bed_target = t.bed_target;
	return end_request(0);
}

static int worker_request_status(void)
{
	MksPrintStatus ps;

	if (begin_request() != 0)
		return PRINTER_BUSY;

	memset(&ps, 0, sizeof(ps));
	ps.progress = -1.0f;
	if (mks_print_status(&ps) != 0) {
		set_err(ps.err[0] ? ps.err : "M27 FAIL");
		apply_print_status(NULL);
		return end_request(-1);
	}
	apply_print_status(&ps);
	return end_request(0);
}

static int worker_request_files(void)
{
	MksFileList sd;
	ModelStorageList local;
	int sd_rc, local_rc;

	if (begin_request() != 0)
		return PRINTER_BUSY;

	memset(&sd, 0, sizeof(sd));
	memset(&local, 0, sizeof(local));

	sd_rc = mks_list_files(&sd);
	local_rc = model_storage_list(&local);

	if (sd_rc != 0 && local_rc != 0) {
		snprintf(s_printer.files.err, sizeof(s_printer.files.err), "%s",
			 sd.err[0] ? sd.err : (local.err[0] ? local.err : "list FAIL"));
		set_err(s_printer.files.err);
		s_printer.files.count = 0;
        s_printer.files_version++;
		return end_request(-1);
	}
	if (sd_rc != 0) {
		/* Keep local-only list; surface SD error in last_error. */
		set_err(sd.err[0] ? sd.err : "SD list FAIL");
		LOG_W("printer_request_files: SD list failed: %s", s_printer.last_error);
		memset(&sd, 0, sizeof(sd));
	}
	if (local_rc != 0)
		memset(&local, 0, sizeof(local));

	merge_lists(&sd, &local);
	/* Partial success (local only) still returns OK so the Models grid works offline. */
	s_printer.files.err[0] = 0;
	return end_request(0);
}

static void refresh_lists_and_status(void)
{
	MksFileList sd;
	ModelStorageList local;
	MksPrintStatus ps;

	memset(&sd, 0, sizeof(sd));
	memset(&local, 0, sizeof(local));
	mks_list_files(&sd);
	model_storage_list(&local);
	merge_lists(&sd, &local);

	memset(&ps, 0, sizeof(ps));
	ps.progress = -1.0f;
	if (mks_print_status(&ps) == 0)
		apply_print_status(&ps);
}

static int worker_request_print(const struct file_entry *entry)
{
	char err[96];

	if (!entry || !entry->name[0]) {
		s_printer.last_request = PRINTER_ERR;
		set_err("no entry");
		return PRINTER_ERR;
	}
	if (begin_request() != 0)
		return PRINTER_BUSY;

	if (entry->flags & FILE_ON_SD) {
		int need_upload = 0;

		/*
		 * The PSP copy of this gcode may be newer than the SD one.
		 * Sizes are cheap to compare (M23 Size:), mismatch -> push again.
		 */
		if ((entry->flags & FILE_ON_PSP) && entry->size > 0) {
			int sd_size = -1;

			if (mks_file_size(entry->name, &sd_size) == 0 && sd_size >= 0 &&
			    sd_size != entry->size)
				need_upload = 1;
		}

		if (need_upload) {
			if (upload_local(entry->name, 1) != 0)
				return end_request(-1);
			sceKernelDelayThread(400 * 1000);
			refresh_lists_and_status();
		} else {
			if (mks_select_and_print(entry->name, err, sizeof(err)) != 0) {
				set_err(err[0] ? err : "PRINT FAIL");
				return end_request(-1);
			}
			sceKernelDelayThread(400 * 1000);
			{
				MksPrintStatus ps;

				memset(&ps, 0, sizeof(ps));
				ps.progress = -1.0f;
				if (mks_print_status(&ps) == 0)
					apply_print_status(&ps);
			}
		}
		strncpy(s_printer.current_file, entry->name, PRINTER_NAME_LEN - 1);
		s_printer.current_file[PRINTER_NAME_LEN - 1] = 0;
		return end_request(0);
	}

	if (entry->flags & FILE_ON_PSP) {
		if (upload_local(entry->name, 1) != 0)
			return end_request(-1);
		sceKernelDelayThread(400 * 1000);
		refresh_lists_and_status();
		strncpy(s_printer.current_file, entry->name, PRINTER_NAME_LEN - 1);
		s_printer.current_file[PRINTER_NAME_LEN - 1] = 0;
		return end_request(0);
	}

	set_err("no source");
	return end_request(-1);
}

static int worker_request_upload(const char *name, int and_print)
{
	if (!name || !name[0]) {
		s_printer.last_request = PRINTER_ERR;
		set_err("no name");
		return PRINTER_ERR;
	}
	if (begin_request() != 0)
		return PRINTER_BUSY;

	if (upload_local(name, and_print) != 0)
		return end_request(-1);

	{
		MksFileList sd;
		ModelStorageList local;

		memset(&sd, 0, sizeof(sd));
		memset(&local, 0, sizeof(local));
		mks_list_files(&sd);
		model_storage_list(&local);
		merge_lists(&sd, &local);
	}
	if (and_print) {
		MksPrintStatus ps;
		memset(&ps, 0, sizeof(ps));
		ps.progress = -1.0f;
		if (mks_print_status(&ps) == 0)
			apply_print_status(&ps);
		strncpy(s_printer.current_file, name, PRINTER_NAME_LEN - 1);
		s_printer.current_file[PRINTER_NAME_LEN - 1] = 0;
	}
	return end_request(0);
}

static int worker_request_delete_sd(const char *name)
{
	char err[96];

	if (!name || !name[0]) {
		s_printer.last_request = PRINTER_ERR;
		set_err("no name");
		return PRINTER_ERR;
	}
	if (begin_request() != 0)
		return PRINTER_BUSY;

	if (mks_delete(name, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "del FAIL");
		return end_request(-1);
	}
	{
		MksFileList sd;
		ModelStorageList local;

		memset(&sd, 0, sizeof(sd));
		memset(&local, 0, sizeof(local));
		mks_list_files(&sd);
		model_storage_list(&local);
		merge_lists(&sd, &local);
	}
	return end_request(0);
}

static int worker_request_delete_psp(const char *name)
{
	char slug[MODEL_STORAGE_SLUG_LEN];
	ModelFileKind kind;
	char err[96];

	if (!name || !name[0]) {
		s_printer.last_request = PRINTER_ERR;
		set_err("no name");
		return PRINTER_ERR;
	}
	if (begin_request() != 0)
		return PRINTER_BUSY;

	if (model_storage_resolve_legacy_name(name, slug, (int)sizeof(slug), &kind) != 0) {
		if (model_storage_sanitize_slug(name, slug, (int)sizeof(slug)) != 0) {
			set_err("bad name");
			return end_request(-1);
		}
	}
	if (model_storage_delete(slug, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "del FAIL");
		return end_request(-1);
	}
	{
		MksFileList sd;
		ModelStorageList local;

		memset(&sd, 0, sizeof(sd));
		memset(&local, 0, sizeof(local));
		mks_list_files(&sd);
		model_storage_list(&local);
		merge_lists(&sd, &local);
	}
	return end_request(0);
}

static int worker_request_pause(void)
{
	char err[96];

	if (begin_request() != 0)
		return PRINTER_BUSY;
	if (mks_pause(err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "PAUSE FAIL");
		return end_request(-1);
	}
	sceKernelDelayThread(300 * 1000);
	{
		MksPrintStatus ps;
		memset(&ps, 0, sizeof(ps));
		ps.progress = -1.0f;
		if (mks_print_status(&ps) == 0)
			apply_print_status(&ps);
	}
	s_printer.status = PRINTER_STATUS_PAUSED;
	return end_request(0);
}

static int worker_request_resume(void)
{
	char err[96];

	if (begin_request() != 0)
		return PRINTER_BUSY;
	if (mks_resume(err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "RESUME FAIL");
		return end_request(-1);
	}
	sceKernelDelayThread(300 * 1000);
	{
		MksPrintStatus ps;
		memset(&ps, 0, sizeof(ps));
		ps.progress = -1.0f;
		if (mks_print_status(&ps) == 0)
			apply_print_status(&ps);
	}
	return end_request(0);
}

static int worker_request_home(void)
{
	char err[96];

	if (begin_request() != 0)
		return PRINTER_BUSY;
	if (mks_home(err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "G28 FAIL");
		return end_request(-1);
	}
	return end_request(0);
}

static int worker_request_set_hotend(int celsius)
{
	char err[96];

	if (begin_request() != 0)
		return PRINTER_BUSY;
	if (mks_set_hotend(celsius, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "M104 FAIL");
		return end_request(-1);
	}
	return end_request(0);
}

static int worker_request_set_bed(int celsius)
{
	char err[96];

	if (begin_request() != 0)
		return PRINTER_BUSY;
	if (mks_set_bed(celsius, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "M140 FAIL");
		return end_request(-1);
	}
	return end_request(0);
}

static int worker_request_set_speed(int percent)
{
	char err[96];

	if (begin_request() != 0)
		return PRINTER_BUSY;
	if (mks_set_speed(percent, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "M220 FAIL");
		return end_request(-1);
	}
	return end_request(0);
}

static int worker_request_set_flow(int percent)
{
	char err[96];

	if (begin_request() != 0)
		return PRINTER_BUSY;
	if (mks_set_flow(percent, err, sizeof(err)) != 0) {
		set_err(err[0] ? err : "M221 FAIL");
		return end_request(-1);
	}
	return end_request(0);
}


static int queue_task(int type, const char *name, const struct file_entry *entry,
		      int and_print, int value, uint32_t timeout_ms)
{
	if (!s_state_ready || net_worker_submit(type, name, entry, and_print, value, 0, 0, timeout_ms) != 0) {
		s_ui.last_request = PRINTER_BUSY;
        snprintf(s_ui.last_error,sizeof(s_ui.last_error),"queue full / worker unavailable");
		return PRINTER_BUSY;
	}
	return PRINTER_OK;
}

int printer_request_temps(void) { return queue_task(CMD_TEMPS, NULL, NULL, 0, 0, 10000); }
int printer_request_status(void) { return queue_task(CMD_STATUS, NULL, NULL, 0, 0, 10000); }
int printer_request_files(void) { return queue_task(CMD_FILES, NULL, NULL, 0, 0, 20000); }
int printer_request_print(const struct file_entry *entry)
{
	if (!entry)
		return PRINTER_ERR;
	return queue_task(CMD_PRINT, NULL, entry, 0, 0, 240000);
}
int printer_request_upload(const char *name, int and_print)
{
	return queue_task(CMD_UPLOAD, name, NULL, and_print, 0, 240000);
}
int printer_request_delete_sd(const char *name)
{
	return queue_task(CMD_DELETE_SD, name, NULL, 0, 0, 20000);
}
int printer_request_delete_psp(const char *name)
{
	return queue_task(CMD_DELETE_PSP, name, NULL, 0, 0, 20000);
}
int printer_request_pause(void) { return queue_task(CMD_PAUSE, NULL, NULL, 0, 0, 10000); }
int printer_request_resume(void) { return queue_task(CMD_RESUME, NULL, NULL, 0, 0, 10000); }
int printer_request_home(void) { return queue_task(CMD_HOME, NULL, NULL, 0, 0, 15000); }
int printer_request_set_hotend(int celsius)
{
	return queue_task(CMD_SET_HOTEND, NULL, NULL, 0, celsius, 10000);
}
int printer_request_set_bed(int celsius)
{
	return queue_task(CMD_SET_BED, NULL, NULL, 0, celsius, 10000);
}
int printer_request_set_speed(int percent)
{
	return queue_task(CMD_SET_SPEED, NULL, NULL, 0, percent, 10000);
}
int printer_request_set_flow(int percent)
{
	return queue_task(CMD_SET_FLOW, NULL, NULL, 0, percent, 10000);
}

static const char *task_label(int type)
{
    static const char *labels[] = { "unknown", "M105", "status", "files", "print", "upload",
        "delete SD", "delete PSP", "pause", "resume", "G28", "M104", "M140", "M220", "M221" };
    return type > 0 && type < (int)(sizeof(labels)/sizeof(labels[0])) ? labels[type] : "command";
}

void printer_worker_execute(const struct net_task *task)
{
    s_printer.last_request=PRINTER_ERR;
    s_printer.last_error[0]=0;
    if (task->type==CMD_PRINT || task->type==CMD_UPLOAD) {
        s_printer.upload_sent=s_printer.upload_total=0;
        publish();
    }
	switch (task->type) {
	case CMD_TEMPS: worker_request_temps(); break;
	case CMD_STATUS: worker_request_status(); break;
	case CMD_FILES: worker_request_files(); break;
	case CMD_PRINT: worker_request_print(&task->entry); break;
	case CMD_UPLOAD: worker_request_upload(task->name, task->and_print); break;
	case CMD_DELETE_SD: worker_request_delete_sd(task->name); break;
	case CMD_DELETE_PSP: worker_request_delete_psp(task->name); break;
	case CMD_PAUSE: worker_request_pause(); break;
	case CMD_RESUME: worker_request_resume(); break;
	case CMD_HOME: worker_request_home(); break;
	case CMD_SET_HOTEND: worker_request_set_hotend(task->value); break;
	case CMD_SET_BED: worker_request_set_bed(task->value); break;
	case CMD_SET_SPEED: worker_request_set_speed(task->value); break;
	case CMD_SET_FLOW: worker_request_set_flow(task->value); break;
	default: set_err("bad task"); break;
    }
    if (sock_task_expired()) printer_worker_timeout();
    sceKernelLockLwMutex(&s_state_lock,1,NULL);
    int busy=s_published.busy;
    unsigned requests=s_published.requests_version;
    s_published=s_printer;s_published.busy=busy;s_published.requests_version=requests;
    int slot=(s_result_head+s_result_count)%RESULT_CAP;
    if (s_result_count==RESULT_CAP) {
        if (!s_overflow_error_pending && s_results[s_result_head].rc!=PRINTER_OK) {
            s_overflow_error=s_results[s_result_head];s_overflow_error_pending=1;
        }
        s_result_head=(s_result_head+1)%RESULT_CAP;
        s_result_count--;
    }
    s_results[slot].type=task->type;
    s_results[slot].rc=s_printer.last_request;
    snprintf(s_results[slot].label,sizeof(s_results[slot].label),"%s",task_label(task->type));
    snprintf(s_results[slot].error,sizeof(s_results[slot].error),"%s",s_printer.last_error);
    s_result_count++;
    sceKernelUnlockLwMutex(&s_state_lock,1);
}

void printer_worker_timeout(void)
{
	s_printer.last_request = PRINTER_ERR;
	set_err("printer TIMEOUT");
}

int printer_poll(void)
{
    unsigned old=s_ui.files_version;
    if (!s_state_ready) return 0;
    sceKernelLockLwMutex(&s_state_lock,1,NULL);
    s_ui=s_published;
    sceKernelUnlockLwMutex(&s_state_lock,1);
    return old!=s_ui.files_version;
}
int printer_next_result(struct printer_result *out)
{
    int have=0;
    if (!s_state_ready) return 0;
    sceKernelLockLwMutex(&s_state_lock,1,NULL);
    if (s_result_count) {
        *out=s_results[s_result_head];
        s_result_head=(s_result_head+1)%RESULT_CAP;
        s_result_count--;have=1;
    } else if (s_overflow_error_pending) {
        *out=s_overflow_error;s_overflow_error_pending=0;have=1;
    }
    sceKernelUnlockLwMutex(&s_state_lock,1);
    return have;
}
