#include "socket.h"
#include "pspkernel.h"
#include "pspnet_inet.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int sceKernelCreateLwMutex(SceLwMutexWorkarea *m,const char *n,int a,int c,void *o){(void)m;(void)n;(void)a;(void)c;(void)o;return 0;}
int sceKernelLockLwMutex(SceLwMutexWorkarea *m,int c,void *t){(void)m;(void)c;(void)t;return 0;}
int sceKernelUnlockLwMutex(SceLwMutexWorkarea *m,int c){(void)m;(void)c;return 0;}
static unsigned long long clock_us;static int mode,blocking;
unsigned long long sceKernelGetSystemTimeWide(void){return clock_us;}
int sceKernelGetThreadId(void){return 1;}
int sceKernelDelayThread(unsigned us){clock_us+=us;return 0;}
int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;return 3;}
int sceNetInetSetsockopt(int f,int l,int k,const void *v,socklen_t n){(void)f;(void)l;(void)n;if(k==SO_NONBLOCK)blocking=!*(int *)v;return 0;}
int sceNetInetConnect(int f,const struct sockaddr *a,socklen_t n){(void)f;(void)a;(void)n;return 0;}
int sceNetInetGetsockopt(int f,int l,int o,void *v,socklen_t *n){(void)f;(void)l;(void)o;(void)n;*(int *)v=0;return 0;}
int sceNetInetPoll(SceNetInetPollfd *p,unsigned n,int t){(void)p;(void)n;clock_us+=(unsigned)t*1000;return 0;}
int sceNetInetGetErrno(void){return EAGAIN;}
int sceNetInetSend(int f,const void *b,int n,int flags){(void)f;(void)b;(void)n;(void)flags;assert(!blocking);if(mode==1){clock_us+=10000;return 1;}return -1;}
int sceNetInetRecv(int f,void *b,int n,int flags){(void)f;(void)n;(void)flags;assert(!blocking);if(mode==2){clock_us+=10000;*(char *)b='x';return 1;}return -1;}
int sceNetInetClose(int f){(void)f;return 0;}
int main(void)
{
    unsigned char ip[4];char data[10000]={0};
    assert(sock_parse_ipv4("1.2.3.4junk",ip)<0);assert(!sock_parse_ipv4("1.2.3.4",ip));
    assert(sock_connect_timeout("1.2.3.4",8080,500)==3 && !blocking);
    clock_us=0;mode=0;assert(send_all(3,data,100)<0);assert(clock_us==30000000);
    clock_us=0;mode=1;assert(send_all(3,data,sizeof(data))<0);assert(clock_us==30000000);
    clock_us=0;sock_worker_register(1);sock_task_begin(100);assert(send_all(3,data,100)<0);assert(clock_us==100000 && sock_task_expired());sock_task_end();
    clock_us=0;mode=2;int n=sock_recv_deadline(3,data,sizeof(data),100,NULL,500);assert(n==10 && clock_us==100000);
    clock_us=0;sock_task_begin(1000);uint64_t old=sock_limit_begin(50);mode=0;assert(send_all(3,data,100)<0);assert(clock_us==50000);sock_limit_end(old);sock_task_end();
    puts("PASS socket fake clock: stalled peer/continuous partial sends/continuous recv/task and nested deadlines");
}
