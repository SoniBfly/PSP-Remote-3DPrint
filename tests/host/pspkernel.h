#ifndef HOST_PSP_KERNEL_H
#define HOST_PSP_KERNEL_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
typedef int SceUID; typedef size_t SceSize; typedef unsigned SceUInt;
typedef pthread_mutex_t SceLwMutexWorkarea;
int sceKernelCreateLwMutex(SceLwMutexWorkarea *,const char *,int,int,void *);
int sceKernelLockLwMutex(SceLwMutexWorkarea *,int,void *);
int sceKernelUnlockLwMutex(SceLwMutexWorkarea *,int);
int sceKernelTryLockLwMutex(SceLwMutexWorkarea *,int);
unsigned long long sceKernelGetSystemTimeWide(void);
int sceKernelGetThreadId(void);
int sceKernelDelayThread(unsigned);
int sceKernelDeleteLwMutex(SceLwMutexWorkarea *);
int sceKernelCreateSema(const char *,int,int,int,void *);
int sceKernelDeleteSema(int);int sceKernelSignalSema(int,int);int sceKernelWaitSema(int,int,SceUInt *);
int sceKernelCreateThread(const char *,int (*)(SceSize,void *),int,int,int,void *);
int sceKernelStartThread(int,int,void *);int sceKernelWaitThreadEnd(int,void *);int sceKernelDeleteThread(int);
#endif
