#include "log.h"
#include "pspnet_inet.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;return 3;}
int sceNetInetSetsockopt(int a,int b,int c,const void *d,socklen_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
int sceNetInetBind(int a,const struct sockaddr *b,socklen_t c){(void)a;(void)b;(void)c;return 0;}
int sceNetInetListen(int a,int b){(void)a;(void)b;return 0;}
int sceNetInetClose(int a){(void)a;return 0;}
int sceNetInetAccept(int a,struct sockaddr *b,socklen_t *c){(void)a;(void)b;(void)c;return -1;}
int sceNetInetRecv(int a,void *b,int c,int d){(void)a;(void)b;(void)c;(void)d;return -1;}
int sceNetInetSend(int a,const void *b,int c,int d){(void)a;(void)b;(void)d;return c;}
static void *writer(void *u){(void)u;for(int i=0;i<1000;i++)LOG_I("thread line %d",i);return NULL;}
int main(void)
{
    char json[12288];unsigned next=0;
    log_init();log_apply_config(LOG_MODE_NET,9999);LOG_I("first");
    assert(log_export_since(0,json,sizeof(json),&next)>0 && next==1 && strstr(json,"first"));
    LOG_I("second");assert(log_export_since(next,json,sizeof(json),&next)>0 && next==2 && strstr(json,"second"));
    pthread_t a,b;pthread_create(&a,NULL,writer,NULL);pthread_create(&b,NULL,writer,NULL);
    pthread_join(a,NULL);pthread_join(b,NULL);assert(log_seq()==2003);
    log_apply_config(LOG_MODE_OFF,9999);unsigned before=log_seq();LOG_I("discard");assert(log_seq()==before);assert(!log_net_active());log_term();
    puts("PASS logging: cursor/no skipped next line/2000 concurrent writes/off mode");
}
