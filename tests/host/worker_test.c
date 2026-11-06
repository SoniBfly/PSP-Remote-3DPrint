/* Real pthread timing checks for the production queue, not a PSP scheduler emulator. */
#include "worker.h"
#include "pspkernel.h"
#include <assert.h>
#include <errno.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static pthread_t thread;
static int (*entry_fn)(SceSize,void *);
static sem_t wake;
static atomic_int calls,busy;
static int types[64];static uint64_t times[64];static char copied[64];
int sceKernelCreateLwMutex(SceLwMutexWorkarea *m,const char *n,int a,int c,void *o){(void)n;(void)a;(void)c;(void)o;return -pthread_mutex_init(m,NULL);}
int sceKernelLockLwMutex(SceLwMutexWorkarea *m,int n,void *t){(void)n;(void)t;return -pthread_mutex_lock(m);}
int sceKernelUnlockLwMutex(SceLwMutexWorkarea *m,int n){(void)n;return -pthread_mutex_unlock(m);}
int sceKernelDeleteLwMutex(SceLwMutexWorkarea *m){return -pthread_mutex_destroy(m);}
int sceKernelCreateSema(const char *n,int a,int initial,int max,void *p){(void)n;(void)a;(void)max;(void)p;sem_init(&wake,0,initial);return 2;}
int sceKernelDeleteSema(int id){(void)id;return sem_destroy(&wake);}
int sceKernelSignalSema(int id,int n){(void)id;while(n--)sem_post(&wake);return 0;}
int sceKernelWaitSema(int id,int n,SceUInt *timeout){(void)id;(void)n;struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=(long)*timeout*1000;t.tv_sec+=t.tv_nsec/1000000000;t.tv_nsec%=1000000000;return sem_timedwait(&wake,&t);}
int sceKernelCreateThread(const char *name,int (*f)(SceSize,void *),int pri,int stack,int attr,void *o){(void)name;(void)pri;(void)stack;(void)attr;(void)o;entry_fn=f;return 3;}
static void *start(void *unused){(void)unused;entry_fn(0,NULL);return NULL;}
int sceKernelStartThread(int id,int n,void *p){(void)id;(void)n;(void)p;return -pthread_create(&thread,NULL,start,NULL);}
int sceKernelWaitThreadEnd(int id,void *p){(void)id;(void)p;return -pthread_join(thread,NULL);}
int sceKernelDeleteThread(int id){(void)id;return 0;}
unsigned long long sceKernelGetSystemTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000000+t.tv_nsec/1000;}
void sock_worker_register(int id){(void)id;}void sock_task_begin(uint32_t t){(void)t;}void sock_task_end(void){}
void printer_queue_push(void){atomic_fetch_add(&busy,1);}void printer_queue_done(void){atomic_fetch_sub(&busy,1);}
void printer_worker_execute(const struct net_task *task){int i=atomic_load(&calls);assert(i<64);types[i]=task->type;times[i]=sceKernelGetSystemTimeWide()/1000;if(task->name[0])snprintf(copied,sizeof(copied),"%s",task->name);atomic_fetch_add(&calls,1);}
int main(void)
{
    assert(!net_worker_start());char name[64]="original.gcode";
    uint64_t start_ms=sceKernelGetSystemTimeWide()/1000;
    assert(!net_worker_submit(CMD_STATUS,name,NULL,0,0,80,0,1000));strcpy(name,"changed.gcode");
    assert(!net_worker_submit(CMD_TEMPS,NULL,NULL,0,0,10,0,1000));
    while(atomic_load(&calls)<2)usleep(1000);
    assert(types[0]==CMD_TEMPS && types[1]==CMD_STATUS);assert(times[0]>=start_ms+9 && times[1]>=start_ms+79);assert(!strcmp(copied,"original.gcode"));
    while(atomic_load(&busy))usleep(1000);
    assert(!net_worker_submit(CMD_TEMPS,NULL,NULL,0,0,40,20,1000));assert(!atomic_load(&busy));
    while(atomic_load(&calls)<5)usleep(1000);
    assert(times[3]>=times[2]+19 && times[4]>=times[3]+19);
    net_worker_stop();assert(!atomic_load(&busy));
    puts("PASS worker: earliest due/delayed tasks/copied args/repeat interval/idle repeat not busy/stop");
}
