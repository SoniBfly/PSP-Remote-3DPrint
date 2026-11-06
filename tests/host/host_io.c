/* POSIX stand-in only; these tests do not emulate PSP hardware. */
#include "pspiofilemgr.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static DIR *dirs[64];static char paths[64][1024];
int host_fail_write,host_no_replace;
int sceKernelCreateLwMutex(SceLwMutexWorkarea *m,const char *n,int a,int c,void *o)
{(void)n;(void)a;(void)c;(void)o;return -pthread_mutex_init(m,NULL);}
int sceKernelLockLwMutex(SceLwMutexWorkarea *m,int c,void *t){(void)c;(void)t;return -pthread_mutex_lock(m);}
int sceKernelUnlockLwMutex(SceLwMutexWorkarea *m,int c){(void)c;return -pthread_mutex_unlock(m);}
int sceKernelTryLockLwMutex(SceLwMutexWorkarea *m,int c){(void)c;return -pthread_mutex_trylock(m);}
unsigned long long sceKernelGetSystemTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceKernelGetThreadId(void){return 1;}
int sceKernelDelayThread(unsigned us){usleep(us);return 0;}
int sceIoMkdir(const char *p,int m){return mkdir(p,m);}
int sceIoOpen(const char *p,int f,int m){return open(p,f,m);}
int sceIoRead(int f,void *p,int n){return read(f,p,n);}
int sceIoWrite(int f,const void *p,int n){return host_fail_write ? -1 : (int)write(f,p,n);}
int sceIoClose(int f){return close(f);}
int sceIoRemove(const char *p){return unlink(p);}
int sceIoRmdir(const char *p){return rmdir(p);}
int sceIoRename(const char *a,const char *b){if(host_no_replace && access(b,F_OK)==0){errno=EEXIST;return -1;}return rename(a,b);}
int sceIoGetstat(const char *p,SceIoStat *o){struct stat s;if(stat(p,&s))return -1;o->st_size=s.st_size;o->st_mode=s.st_mode;o->st_attr=S_ISDIR(s.st_mode)?S_IFDIR:0;return 0;}
int sceIoDopen(const char *p){for(int i=0;i<64;i++)if(!dirs[i]){dirs[i]=opendir(p);snprintf(paths[i],sizeof(paths[i]),"%s",p);return dirs[i]?i:-1;}return -1;}
int sceIoDread(int f,SceIoDirent *o){struct dirent *e=readdir(dirs[f]);char p[1400];if(!e)return 0;snprintf(o->d_name,sizeof(o->d_name),"%s",e->d_name);snprintf(p,sizeof(p),"%s/%s",paths[f],e->d_name);sceIoGetstat(p,&o->d_stat);return 1;}
int sceIoDclose(int f){int rc=closedir(dirs[f]);dirs[f]=NULL;return rc;}
static char root[1024],models[1100],inbox[1100],www[1100];
void host_init(const char *p){snprintf(root,sizeof(root),"%s",p);snprintf(models,sizeof(models),"%s/models",p);snprintf(inbox,sizeof(inbox),"%s/inbox",p);snprintf(www,sizeof(www),"%s/www",p);}
#ifndef HOST_REAL_CONFIG
void cfg_ensure_dirs(void){mkdir(root,0700);mkdir(models,0700);mkdir(inbox,0700);mkdir(www,0700);}
const char *cfg_models_dir(void){return models;}const char *cfg_inbox_dir(void){return inbox;}const char *cfg_www_dir(void){return www;}
int cfg_max_upload(void){return 4*1024*1024;}int cfg_max_preview(void){return 1024*1024;}
#endif
#ifndef HOST_REAL_LOG
void log_write(char level,const char *fmt,...){(void)level;(void)fmt;}
#endif
