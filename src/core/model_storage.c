/*
 * Model warehouse: models/<slug>/… plus one-shot migration from flat inbox/.
 */
#include "model_storage.h"
#include "config.h"
#include "log.h"
#include "file_io.h"

#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspthreadman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODELS_DIR cfg_models_dir()
#define INBOX_DIR  cfg_inbox_dir()

static int s_migrated;

static SceLwMutexWorkarea s_storage_lock;
static int s_lock_ready;
static void storage_init_impl(void);
static int storage_list_impl(ModelStorageList *out);
static int storage_exists_impl(const char *slug);
static int storage_unique_slug_impl(const char *slug, char *out, int outlen);
static int storage_read_impl(const char *, ModelFileKind, char **, int *);
static int storage_write_impl(const char *, ModelFileKind, const void *, int, int);
static int storage_delete_impl(const char *, char *, int);


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

static int is_gcode_ext(const char *dot)
{
	return ascii_eq_ci(dot, ".gcode") || ascii_eq_ci(dot, ".gco") ||
	       ascii_eq_ci(dot, ".g");
}

int model_storage_sanitize_filename(const char *in, char *out, int outlen)
{
    int j=0;
    if (!out || outlen<2) return -1;
    out[0]=0;
    if (!in || !*in || strchr(in,'/') || strchr(in,'\\') || strchr(in,':') ||
        strstr(in,"..") || !strcmp(in,".")) return -1;
    while (*in) {
        unsigned char c=(unsigned char)*in++;
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='.' || c=='_' || c=='-') {
            if (j+1>=outlen) goto fail;
            out[j++]=c;
        } else if (c==' ') {
            if (j+1>=outlen) goto fail;
            out[j++]='_';
        } else if (c>=0x80) {
            static const char hex[]="0123456789abcdef";
            if (j+3>=outlen) goto fail;
            out[j++]='_';out[j++]=hex[c>>4];out[j++]=hex[c&15];
        } else goto fail;
    }
    out[j]=0;
    return j ? 0 : -1;
fail:
    out[0]=0; return -1;
}

/* A slug is already extension-free. Never strip it a second time. */
int model_storage_sanitize_slug(const char *in, char *out, int outlen)
{
    char safe[MODEL_STORAGE_NAME_LEN];
    if (!out || outlen<2) return -1;
    out[0]=0;
    if (model_storage_sanitize_filename(in,safe,sizeof(safe)) ||
        safe[0]=='.' || strlen(safe)>MODEL_STORAGE_SLUG_MAX ||
        strlen(safe)>=(size_t)outlen) return -1;
    strcpy(out,safe); return 0;
}

static const char *kind_filename(ModelFileKind kind)
{
	switch (kind) {
	case MODEL_FILE_GCODE:       return "model.gcode";
	case MODEL_FILE_STL:         return "model.stl";
	case MODEL_FILE_PREVIEW:     return "preview.flpv";
	case MODEL_FILE_PREVIEW_SUP: return "preview_sup.flpv";
	case MODEL_FILE_THUMB:       return "thumb.png";
	case MODEL_FILE_META:        return "meta.ini";
	default:                     return NULL;
	}
}

int model_storage_path(const char *slug, ModelFileKind kind,
		       char *out, int outlen)
{
	char safe[MODEL_STORAGE_SLUG_LEN];
	const char *fname;

	if (!out || outlen < 8)
		return -1;
	out[0] = 0;
	if (!slug || !slug[0])
		return -1;
	if (model_storage_sanitize_slug(slug, safe, (int)sizeof(safe)) != 0)
        return -1;
	if (!safe[0] || strchr(safe, '/') || strchr(safe, '\\'))
		return -1;
	fname = kind_filename(kind);
	if (!fname)
		return -1;
	snprintf(out, outlen, "%s/%s/%s", MODELS_DIR, safe, fname);
	return 0;
}

static int path_exists(const char *path)
{
	SceIoStat st;

	if (!path || !path[0])
		return 0;
	memset(&st, 0, sizeof(st));
	file_recover(path);
	return sceIoGetstat(path, &st) >= 0;
}

static int file_size_of(const char *path)
{
	SceIoStat st;

	memset(&st, 0, sizeof(st));
	file_recover(path);
	if (sceIoGetstat(path, &st) < 0)
		return 0;
	return (int)st.st_size;
}

static void ensure_slug_dir(const char *slug)
{
	char dir[180];

	cfg_ensure_dirs();
	snprintf(dir, sizeof(dir), "%s/%s", MODELS_DIR, slug);
	sceIoMkdir(dir, 0777);
}

static int copy_file(const char *src, const char *dst)
{
	SceUID in, out;
	SceIoStat st;
	char buf[4096];
	int n, wr;
	int total = 0;

	memset(&st, 0, sizeof(st));
	if (sceIoGetstat(src, &st) < 0 || st.st_size <= 0)
		return -1;
	in = sceIoOpen(src, PSP_O_RDONLY, 0);
	if (in < 0)
		return -1;
	out = sceIoOpen(dst, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
	if (out < 0) {
		sceIoClose(in);
		return -1;
	}
	while ((n = sceIoRead(in, buf, sizeof(buf))) > 0) {
		wr = sceIoWrite(out, buf, n);
		if (wr != n) {
			sceIoClose(in);
			sceIoClose(out);
			return -1;
		}
		total += wr;
	}
	sceIoClose(in);
	sceIoClose(out);
	return n == 0 && total == (int)st.st_size ? 0 : -1;
}

/* Migrate flat inbox/ gcode (+ paired flpv / _sup.flpv) into models/<slug>/.
 * On success the source files are removed. Existing destination → skip that file. */
static void migrate_from_inbox(void)
{
	SceUID dd;
	SceIoDirent de;
	char src[200], dst[220], slug[MODEL_STORAGE_SLUG_LEN];
	char base[MODEL_STORAGE_NAME_LEN];
	char flpv[200], flpv_sup[200];
	char *dot;
	int n;

	cfg_ensure_dirs();
	dd = sceIoDopen(INBOX_DIR);
	if (dd < 0)
		return;

	memset(&de, 0, sizeof(de));
	while (sceIoDread(dd, &de) > 0) {
		if (de.d_stat.st_attr & FIO_SO_IFDIR)
			goto next;
#ifdef FIO_S_ISDIR
		if (FIO_S_ISDIR(de.d_stat.st_mode))
			goto next;
#endif
		dot = strrchr(de.d_name, '.');
		if (!dot || !is_gcode_ext(dot))
			goto next;

		ModelFileKind migrated_kind;
        if (model_storage_resolve_legacy_name(de.d_name, slug, sizeof(slug), &migrated_kind))
            goto next;
		if (!slug[0])
			goto next;

		ensure_slug_dir(slug);
		if (model_storage_path(slug, MODEL_FILE_GCODE, dst, (int)sizeof(dst)) != 0)
			goto next;

		if (snprintf(src, sizeof(src), "%s/%s", INBOX_DIR, de.d_name)>=(int)sizeof(src))
            goto next;
		if (!path_exists(dst)) {
			if (copy_file(src, dst) == 0)
				sceIoRemove(src);
		} else {
			/* Destination already present — leave inbox file (no silent clobber). */
			LOG_W("migrate skip gcode %s (exists)", slug);
		}

		/* Companion previews next to the old flat gcode. */
		n = (int)strlen(de.d_name);
		if (n >= (int)sizeof(base))
			n = (int)sizeof(base) - 1;
		memcpy(base, de.d_name, (size_t)n);
		base[n] = 0;
		dot = strrchr(base, '.');
		if (dot)
			*dot = 0;

		snprintf(flpv, sizeof(flpv), "%s/%s.flpv", INBOX_DIR, base);
		snprintf(flpv_sup, sizeof(flpv_sup), "%s/%s_sup.flpv", INBOX_DIR, base);

		if (path_exists(flpv) &&
		    model_storage_path(slug, MODEL_FILE_PREVIEW, dst, (int)sizeof(dst)) == 0) {
			if (!path_exists(dst) && copy_file(flpv, dst) == 0)
				sceIoRemove(flpv);
		}
		if (path_exists(flpv_sup) &&
		    model_storage_path(slug, MODEL_FILE_PREVIEW_SUP, dst, (int)sizeof(dst)) == 0) {
			if (!path_exists(dst) && copy_file(flpv_sup, dst) == 0)
				sceIoRemove(flpv_sup);
		}
next:
		memset(&de, 0, sizeof(de));
	}
	sceIoDclose(dd);
}

static void storage_init_impl(void)
{
	cfg_ensure_dirs();
	if (s_migrated)
		return;
	migrate_from_inbox();
	s_migrated = 1;
}

static int storage_list_impl(ModelStorageList *out)
{
	SceUID dd;
	SceIoDirent de;
	char path[200];

	memset(out, 0, sizeof(*out));
	storage_init_impl();

	dd = sceIoDopen(MODELS_DIR);
	if (dd < 0) {
		snprintf(out->err, sizeof(out->err), "models open fail");
		return -1;
	}

	memset(&de, 0, sizeof(de));
	while (sceIoDread(dd, &de) > 0) {
		int is_dir = 0;
		ModelStorageEntry *e;

		if (de.d_name[0] == '.')
			goto next;
		if (de.d_stat.st_attr & FIO_SO_IFDIR)
			is_dir = 1;
#ifdef FIO_S_ISDIR
		if (FIO_S_ISDIR(de.d_stat.st_mode))
			is_dir = 1;
#endif
		if (!is_dir)
			goto next;
		if (out->count >= MODEL_STORAGE_MAX_MODELS)
			break;

		e = &out->items[out->count];
		memset(e, 0, sizeof(*e));
		if (model_storage_sanitize_slug(de.d_name, e->slug, sizeof(e->slug)) ||
            strcmp(e->slug, de.d_name)) {
            LOG_W("unsupported model directory name");
            goto next;
        }

		if (model_storage_path(e->slug, MODEL_FILE_GCODE, path, (int)sizeof(path)) == 0)
			e->gcode_size = file_size_of(path);
		if (model_storage_path(e->slug, MODEL_FILE_PREVIEW, path, (int)sizeof(path)) == 0)
			e->has_preview = path_exists(path) ? 1 : 0;
		if (model_storage_path(e->slug, MODEL_FILE_PREVIEW_SUP, path, (int)sizeof(path)) == 0)
			e->has_preview_sup = path_exists(path) ? 1 : 0;
		if (model_storage_path(e->slug, MODEL_FILE_STL, path, (int)sizeof(path)) == 0)
			e->has_stl = path_exists(path) ? 1 : 0;
		if (model_storage_path(e->slug, MODEL_FILE_THUMB, path, (int)sizeof(path)) == 0)
			e->has_thumb = path_exists(path) ? 1 : 0;

		/* Only expose folders that at least have gcode (printable / listable). */
		if (e->gcode_size > 0)
			out->count++;
next:
		memset(&de, 0, sizeof(de));
	}
	sceIoDclose(dd);
	return 0;
}

static int storage_exists_impl(const char *slug)
{
	char safe[MODEL_STORAGE_SLUG_LEN];
	char dir[180];

	if (!slug || model_storage_sanitize_slug(slug, safe, (int)sizeof(safe)) != 0)
		return 0;
	snprintf(dir, sizeof(dir), "%s/%s", MODELS_DIR, safe);
	return path_exists(dir);
}

static int storage_unique_slug_impl(const char *slug, char *out, int outlen)
{
	char safe[MODEL_STORAGE_SLUG_LEN];
	int i;

	if (!slug || !out || model_storage_sanitize_slug(slug, safe, (int)sizeof(safe)) != 0)
		return -1;
	if (!storage_exists_impl(safe)) {
		snprintf(out, outlen, "%s", safe);
		return 0;
	}
	for (i = 1; i < 1000; i++) {
		int n = snprintf(out, outlen, "%.*s_%d", MODEL_STORAGE_SLUG_MAX - 4, safe, i);
        if (n < 0 || n >= outlen) return -1;
		if (!storage_exists_impl(out))
			return 0;
	}
	return -1;
}

static int storage_read_impl(const char *slug, ModelFileKind kind,
		       char **data_out, int *size_out)
{
	char path[220];
	SceUID fd;
	SceIoStat st;
	int n, max_sz;
	char *buf;

	if (data_out)
		*data_out = NULL;
	if (size_out)
		*size_out = 0;
	if (!slug || !slug[0] || !data_out)
		return -1;
	if (model_storage_path(slug, kind, path, (int)sizeof(path)) != 0)
		return -1;

	memset(&st, 0, sizeof(st));
	file_recover(path);
	if (sceIoGetstat(path, &st) < 0)
		return -1;

	max_sz = (kind == MODEL_FILE_PREVIEW || kind == MODEL_FILE_PREVIEW_SUP ||
		  kind == MODEL_FILE_THUMB)
			 ? cfg_max_preview()
			 : cfg_max_upload();
	if (kind == MODEL_FILE_META)
		max_sz = 4096;
	if ((int)st.st_size <= 0 || (int)st.st_size > max_sz)
		return -2;

	buf = (char *)malloc((size_t)st.st_size + 1);
	if (!buf)
		return -3;

	fd = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (fd < 0) {
		free(buf);
		return -1;
	}
	n = sceIoRead(fd, buf, (int)st.st_size);
	sceIoClose(fd);
	if (n != (int)st.st_size) {
		free(buf);
		return -4;
	}
	buf[n] = 0;
	*data_out = buf;
	if (size_out)
		*size_out = n;
	return n;
}

static int storage_write_impl(const char *slug, ModelFileKind kind,
			const void *data, int len, int overwrite)
{
	char safe[MODEL_STORAGE_SLUG_LEN];
	char path[220], temp[232];
	SceUID fd;
	int wr, max_sz;

	max_sz = (kind == MODEL_FILE_PREVIEW || kind == MODEL_FILE_PREVIEW_SUP ||
		  kind == MODEL_FILE_THUMB) ? cfg_max_preview() : cfg_max_upload();
	if (!slug || !slug[0] || !data || len <= 0 || len > max_sz)
		return -2;
	if (model_storage_sanitize_slug(slug, safe, (int)sizeof(safe)) != 0)
        return -1;
	if (!safe[0])
		return -1;
	if (model_storage_path(safe, kind, path, (int)sizeof(path)) != 0)
		return -1;

	ensure_slug_dir(safe);
	if (!overwrite && path_exists(path))
		return -5; /* conflict — HTTP reports 409 */

	snprintf(temp, sizeof(temp), "%s.pending", path);
    fd = sceIoOpen(temp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd < 0) return -1;
    wr = sceIoWrite(fd, data, len);
    sceIoClose(fd);
    if (wr != len) { sceIoRemove(temp); return -4; }
    if (file_replace(temp, path) < 0) {
        /* Keep the old target intact if this filesystem cannot replace atomically. */
        sceIoRemove(temp); return -4;
    }
    return 0;
}

static int storage_delete_impl(const char *slug, char *err, int errlen)
{
	char safe[MODEL_STORAGE_SLUG_LEN];
	char path[220];
	char dir[180];
	int k;
	int any = 0, failed = 0;

	if (err && errlen > 0)
		err[0] = 0;
	if (!slug || !slug[0]) {
		if (err && errlen > 0)
			snprintf(err, errlen, "no file");
		return -1;
	}
	if (model_storage_sanitize_slug(slug, safe, (int)sizeof(safe)) != 0)
        return -1;
	if (!safe[0]) {
		if (err && errlen > 0)
			snprintf(err, errlen, "bad name");
		return -1;
	}

	for (k = 0; k <= (int)MODEL_FILE_META; k++) {
		if (model_storage_path(safe, (ModelFileKind)k, path, (int)sizeof(path)) != 0)
			continue;
		if (path_exists(path)) {
			if (sceIoRemove(path) < 0) failed = 1;
            else any = 1;
		}
	}
	snprintf(dir, sizeof(dir), "%s/%s", MODELS_DIR, safe);
	if (path_exists(dir) && sceIoRmdir(dir) < 0) failed = 1;
    if (failed || (!any && path_exists(dir))) {
		/* Also accept "already gone" as ok if dir absent. */
		if (err && errlen > 0)
			snprintf(err, errlen, "remove fail");
		return -1;
	}
	return 0;
}

int model_storage_resolve_legacy_name(const char *filename,
    char *slug_out, int slug_len, ModelFileKind *kind_out)
{
    char safe[MODEL_STORAGE_NAME_LEN];
    int n; char *dot;
    if (!slug_out || slug_len<2 || !kind_out) return -1;
    slug_out[0]=0;
    if (model_storage_sanitize_filename(filename,safe,sizeof(safe))) return -1;
    n=strlen(safe); dot=strrchr(safe,'.');
    if (n>9 && ascii_eq_ci(safe+n-9,"_sup.flpv")) {
        *kind_out=MODEL_FILE_PREVIEW_SUP; safe[n-9]=0;
    } else {
        if (!dot) return -1;
        if (is_gcode_ext(dot)) *kind_out=MODEL_FILE_GCODE;
        else if (ascii_eq_ci(dot,".flpv")) *kind_out=MODEL_FILE_PREVIEW;
        else if (ascii_eq_ci(dot,".stl")) *kind_out=MODEL_FILE_STL;
        else if (ascii_eq_ci(dot,".png")) *kind_out=MODEL_FILE_THUMB;
        else return -1;
        *dot=0;
    }
    return model_storage_sanitize_slug(safe,slug_out,slug_len);
}

/* Called once on the main thread before network threads start. */
void model_storage_init(void)
{
    if (!s_lock_ready) {
        if (sceKernelCreateLwMutex(&s_storage_lock,"model_storage",0,0,NULL)<0) return;
        s_lock_ready=1;
    }
    sceKernelLockLwMutex(&s_storage_lock,1,NULL);
    storage_init_impl();
    sceKernelUnlockLwMutex(&s_storage_lock,1);
}
static int storage_lock(void)
{
    return s_lock_ready ? sceKernelLockLwMutex(&s_storage_lock,1,NULL) : -1;
}
static void storage_unlock(void) { sceKernelUnlockLwMutex(&s_storage_lock,1); }
int model_storage_list(ModelStorageList *out)
{
    int rc; if (storage_lock()<0) { memset(out,0,sizeof(*out)); return -1; }
    rc=storage_list_impl(out); storage_unlock(); return rc;
}
int model_storage_exists(const char *slug)
{
    int rc; if (storage_lock()<0) return 0;
    rc=storage_exists_impl(slug); storage_unlock(); return rc;
}
int model_storage_unique_slug(const char *slug,char *out,int cap)
{
    int rc; if (storage_lock()<0) return -1;
    rc=storage_unique_slug_impl(slug,out,cap); storage_unlock(); return rc;
}
int model_storage_read(const char *slug,ModelFileKind kind,char **data,int *size)
{
    int rc; if (storage_lock()<0) return -1;
    rc=storage_read_impl(slug,kind,data,size); storage_unlock(); return rc;
}
int model_storage_read_try(const char *slug,ModelFileKind kind,char **data,int *size)
{
    int rc;
    *data=NULL; if (size) *size=0;
    if (!s_lock_ready || sceKernelTryLockLwMutex(&s_storage_lock,1)<0) return -6;
    rc=storage_read_impl(slug,kind,data,size); storage_unlock(); return rc;
}
int model_storage_write(const char *slug,ModelFileKind kind,const void *data,int size,int overwrite)
{
    int rc; if (storage_lock()<0) return -1;
    rc=storage_write_impl(slug,kind,data,size,overwrite); storage_unlock(); return rc;
}
int model_storage_delete(const char *slug,char *err,int cap)
{
    int rc; if (storage_lock()<0) return -1;
    rc=storage_delete_impl(slug,err,cap); storage_unlock(); return rc;
}
int model_storage_artifact_exists_try(const char *slug,ModelFileKind kind)
{
    char path[220]; int exists=0;
    if (!s_lock_ready || sceKernelTryLockLwMutex(&s_storage_lock,1)<0) return 0;
    if (!model_storage_path(slug,kind,path,sizeof(path))) exists=path_exists(path);
    storage_unlock(); return exists;
}
