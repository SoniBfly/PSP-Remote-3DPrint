/* Non-blocking sockets with monotonic wall-clock deadlines. */
#include "socket.h"
#include <pspkernel.h>
#include <pspthreadman.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

static SceLwMutexWorkarea s_shutdown_lock;
static int s_shutdown_ready, s_shutdown;
int sock_runtime_init(void)
{
    if (!s_shutdown_ready) {
        if (sceKernelCreateLwMutex(&s_shutdown_lock,"network_shutdown",0,0,NULL)<0) return -1;
        s_shutdown_ready=1;
    }
    sceKernelLockLwMutex(&s_shutdown_lock,1,NULL);s_shutdown=0;
    sceKernelUnlockLwMutex(&s_shutdown_lock,1);return 0;
}
void sock_runtime_shutdown(void)
{
    if (!s_shutdown_ready) return;
    sceKernelLockLwMutex(&s_shutdown_lock,1,NULL);s_shutdown=1;
    sceKernelUnlockLwMutex(&s_shutdown_lock,1);
}
int sock_stopping(void)
{
    if (!s_shutdown_ready) return 0;
    sceKernelLockLwMutex(&s_shutdown_lock,1,NULL);int stop=s_shutdown;
    sceKernelUnlockLwMutex(&s_shutdown_lock,1);return stop;
}
static SceUID s_task_thread = -1;
static uint64_t s_task_deadline, s_limit_deadline;

uint64_t sock_now_ms(void) { return sceKernelGetSystemTimeWide() / 1000; }
void sock_worker_register(int thread_id) { s_task_thread=thread_id; }
void sock_task_begin(uint32_t timeout_ms)
{
    s_task_deadline = timeout_ms ? sock_now_ms() + timeout_ms : 0;
}
uint64_t sock_limit_begin(int timeout_ms)
{
    uint64_t old=s_limit_deadline;
    uint64_t next=sock_now_ms()+sock_remaining_ms(timeout_ms);
    s_limit_deadline=next;
    return old;
}
void sock_limit_end(uint64_t previous) { s_limit_deadline=previous; }
void sock_task_end(void) { s_task_deadline = 0; }
int sock_task_expired(void)
{
    return sock_stopping() || (sceKernelGetThreadId() == s_task_thread && s_task_deadline &&
           sock_now_ms() >= s_task_deadline);
}
int sock_remaining_ms(int limit_ms)
{
    uint64_t now = sock_now_ms();
    if (sceKernelGetThreadId() == s_task_thread) {
        uint64_t deadline=s_task_deadline;
        if (s_limit_deadline && (!deadline || s_limit_deadline<deadline)) deadline=s_limit_deadline;
        if (deadline) {
            uint64_t left=deadline>now ? deadline-now : 0;
            if (left<(uint64_t)limit_ms) return (int)left;
        }
    }
    return limit_ms;
}
void sock_set_nonblock(int fd, int on)
{
    int flag = on ? 1 : 0;
    sceNetInetSetsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &flag, sizeof(flag));
}
static int retryable(void)
{
    int e = sceNetInetGetErrno();
    return e == EAGAIN || e == EWOULDBLOCK || e == EINTR ||
           e == EINPROGRESS || e == EALREADY;
}
static int poll_one(int fd, short events, uint64_t deadline)
{
    SceNetInetPollfd pfd;
    uint64_t now = sock_now_ms();
    int left = deadline > now ? (int)(deadline - now) : 0;
    left = sock_remaining_ms(left);
    if (left <= 0 || sock_stopping()) return -1;
    pfd.fd = fd; pfd.events = events; pfd.revents = 0;
    return sceNetInetPoll(&pfd, 1, left < 40 ? left : 40);
}
int sock_parse_ipv4(const char *s, unsigned char out[4])
{
    unsigned a,b,c,d; char extra;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &a,&b,&c,&d,&extra) != 4 ||
        a>255 || b>255 || c>255 || d>255) return -1;
    out[0]=a; out[1]=b; out[2]=c; out[3]=d; return 0;
}
int sock_connect_timeout(const char *ip, int port, int timeout_ms)
{
    int fd, ret, soerr;
    socklen_t len;
    unsigned char oct[4]; struct sockaddr_in addr;
    uint64_t deadline = sock_now_ms() + sock_remaining_ms(timeout_ms);
    if (sock_parse_ipv4(ip,oct) || port <= 0 || port > 65535 || sock_task_expired()) return -1;
    fd=sceNetInetSocket(AF_INET,SOCK_STREAM,0);
    if (fd<0) return -1;
    sock_set_nonblock(fd,1);
    memset(&addr,0,sizeof(addr)); addr.sin_len=sizeof(addr); addr.sin_family=AF_INET;
    addr.sin_port=htons((unsigned short)port); memcpy(&addr.sin_addr.s_addr,oct,4);
    ret=sceNetInetConnect(fd,(struct sockaddr *)&addr,sizeof(addr));
    if (ret==0) return fd;
    if (!retryable()) { sceNetInetClose(fd); return -3; }
    while (sock_now_ms()<deadline && !sock_task_expired()) {
        ret=poll_one(fd,SCE_NET_INET_POLLOUT,deadline);
        if (ret<0) break;
        if (!ret) continue;
        soerr=0; len=sizeof(soerr);
        if (sceNetInetGetsockopt(fd,SOL_SOCKET,SO_ERROR,&soerr,&len)<0) break;
        if (!soerr) return fd;
        if (soerr!=EINPROGRESS && soerr!=EALREADY) break;
    }
    sceNetInetClose(fd); return -2;
}
int sock_recv_deadline(int fd,char *buf,int cap,int timeout_ms,const char *stop,int quiet_ms)
{
    int total=0;
    uint64_t now=sock_now_ms(), last=now;
    uint64_t deadline=now+sock_remaining_ms(timeout_ms);
    if (!buf || cap<2) return 0;
    buf[0]=0; sock_set_nonblock(fd,1);
    if (quiet_ms<=0) quiet_ms=400;
    while (total<cap-1 && sock_now_ms()<deadline && !sock_task_expired()) {
        int n=sceNetInetRecv(fd,buf+total,cap-1-total,0);
        if (n>0) { total+=n; buf[total]=0; last=sock_now_ms();
            if (stop && strstr(buf,stop)) break;
            continue;
        }
        if (!n) break;
        if (!retryable()) break;
        if (total && sock_now_ms()-last>=(unsigned)quiet_ms) break;
        if (poll_one(fd,SCE_NET_INET_POLLIN,deadline)<0) break;
    }
    buf[total]=0; return total;
}
int send_all(int fd,const void *buf,int len)
{
    int sent=0; const char *data=buf;
    uint64_t deadline=sock_now_ms()+sock_remaining_ms(30000);
    if (!data || len<0) return -1;
    sock_set_nonblock(fd,1);
    while (sent<len) {
        int n;
        if (sock_now_ms()>=deadline || sock_task_expired()) return -1;
        n=sceNetInetSend(fd,data+sent,len-sent,0);
        if (n>0) { sent+=n; continue; }
        if (!n || !retryable()) return -1;
        if (poll_one(fd,SCE_NET_INET_POLLOUT,deadline)<0) return -1;
    }
    return 0;
}
