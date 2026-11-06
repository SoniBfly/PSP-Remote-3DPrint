/*
 * Local model warehouse on the Memory Stick:
 *   models/<slug>/{model.gcode, model.stl, preview.flpv, preview_sup.flpv, thumb.png, meta.ini}
 *
 * No sockets / MKS. Screens do not include this header — use core/printer merge.
 */
#ifndef FLSUN_MODEL_STORAGE_H
#define FLSUN_MODEL_STORAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODEL_STORAGE_MAX_MODELS 32
#define MODEL_STORAGE_SLUG_MAX 57 /* Leaves room for .gcode in a printer entry. */
#define MODEL_STORAGE_SLUG_LEN    64
#define MODEL_STORAGE_NAME_LEN    64

/* Which artifact inside models/<slug>/. */
typedef enum {
	MODEL_FILE_GCODE = 0,
	MODEL_FILE_STL,
	MODEL_FILE_PREVIEW,      /* preview.flpv */
	MODEL_FILE_PREVIEW_SUP,  /* preview_sup.flpv (web dual-preview) */
	MODEL_FILE_THUMB,
	MODEL_FILE_META
} ModelFileKind;

typedef struct {
	char slug[MODEL_STORAGE_SLUG_LEN];
	int  gcode_size;   /* bytes of model.gcode, or 0 if missing */
	int  has_preview;
	int  has_preview_sup;
	int  has_stl;
	int  has_thumb;
} ModelStorageEntry;

typedef struct {
	ModelStorageEntry items[MODEL_STORAGE_MAX_MODELS];
	int count;
	char err[96];
} ModelStorageList;

/* Ensure models/ exists; migrate flat inbox/ gcode (+ flpv) once. */
void model_storage_init(void);

/*
 * Normalize a canonical slug idempotently (no paths or dot segments).
 * Use resolve_legacy_name to strip an incoming filename extension exactly once.
 */
int model_storage_sanitize_slug(const char *in, char *out, int outlen);

/* Sanitize basename keeping a safe filename (letters/digits/._-). */
int model_storage_sanitize_filename(const char *in, char *out, int outlen);

/* List model folders under models/. */
int model_storage_list(ModelStorageList *out);

int model_storage_exists(const char *slug);
int model_storage_unique_slug(const char *slug, char *out, int outlen);

/* Build absolute path for an artifact. 0 = ok. */
int model_storage_path(const char *slug, ModelFileKind kind,
		       char *out, int outlen);

/*
 * Read artifact into malloc'd buffer (caller frees).
 * Returns byte count, or negative on error (-2 too big/empty, -3 oom, …).
 */
int model_storage_read(const char *slug, ModelFileKind kind,
		       char **data_out, int *size_out);

/*
 * Write artifact. Creates models/<slug>/ as needed.
 * overwrite=0: refuse if the target file already exists (returns -5).
 * overwrite=1: O_TRUNC replace.
 * Returns 0 on success.
 */
int model_storage_read_try(const char *, ModelFileKind, char **, int *);
int model_storage_artifact_exists_try(const char *, ModelFileKind);

int model_storage_write(const char *slug, ModelFileKind kind,
			const void *data, int len, int overwrite);

/* Delete the whole models/<slug>/ folder (best-effort file remove + rmdir). */
int model_storage_delete(const char *slug, char *err, int errlen);

/*
 * Resolve a legacy / HTTP name (.gcode / .flpv / _sup.flpv) to slug + kind.
 * Returns 0 on success.
 */
int model_storage_resolve_legacy_name(const char *filename,
				      char *slug_out, int slug_len,
				      ModelFileKind *kind_out);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_MODEL_STORAGE_H */
