#include <sys/socket.h>
#include <poll.h>
#define SO_NONBLOCK 0x1009
#define SCE_NET_INET_POLLOUT POLLOUT
#define SCE_NET_INET_POLLIN POLLIN
typedef struct pollfd SceNetInetPollfd;
int sceNetInetSocket(int,int,int);int sceNetInetSetsockopt(int,int,int,const void *,socklen_t);
int sceNetInetConnect(int,const struct sockaddr *,socklen_t);int sceNetInetGetsockopt(int,int,int,void *,socklen_t *);
int sceNetInetPoll(SceNetInetPollfd *,unsigned,int);int sceNetInetGetErrno(void);
int sceNetInetSend(int,const void *,int,int);int sceNetInetRecv(int,void *,int,int);int sceNetInetClose(int);

int sceNetInetBind(int,const struct sockaddr *,socklen_t);int sceNetInetListen(int,int);int sceNetInetAccept(int,struct sockaddr *,socklen_t *);
