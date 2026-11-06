#ifndef FLSUN_SPLASH_H
#define FLSUN_SPLASH_H
void splash_show(const char *phase,unsigned files,unsigned total_files,
                 unsigned bytes,unsigned total_bytes,const char *file,int error);
void splash_progress(const char *,unsigned,unsigned,unsigned,unsigned,const char *,int,void *);
#endif
