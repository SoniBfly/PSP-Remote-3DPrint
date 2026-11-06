#ifndef FLSUN_WWW_BUNDLE_H
#define FLSUN_WWW_BUNDLE_H
typedef void (*www_progress_fn)(const char *phase,unsigned files,unsigned total_files,
    unsigned bytes,unsigned total_bytes,const char *file,int error,void *user);
int www_bundle_ensure(www_progress_fn progress,void *user);
#endif
