#ifndef HOST_PSP_IO_H
#define HOST_PSP_IO_H
#include "pspkernel.h"
#include <fcntl.h>
#include <sys/stat.h>
typedef struct { long long st_size; unsigned st_mode,st_attr; } SceIoStat;
typedef struct { char d_name[256]; SceIoStat d_stat; } SceIoDirent;
#define PSP_O_RDONLY O_RDONLY
#define PSP_O_WRONLY O_WRONLY
#define PSP_O_CREAT O_CREAT
#define PSP_O_TRUNC O_TRUNC
#define PSP_O_APPEND O_APPEND
#define FIO_SO_IFDIR S_IFDIR
#define FIO_S_ISDIR S_ISDIR
int sceIoMkdir(const char *,int);int sceIoOpen(const char *,int,int);
int sceIoRead(int,void *,int);int sceIoWrite(int,const void *,int);int sceIoClose(int);
int sceIoRemove(const char *);int sceIoRmdir(const char *);int sceIoRename(const char *,const char *);
int sceIoGetstat(const char *,SceIoStat *);int sceIoDopen(const char *);int sceIoDread(int,SceIoDirent *);int sceIoDclose(int);
extern int host_fail_write,host_no_replace;
#endif
