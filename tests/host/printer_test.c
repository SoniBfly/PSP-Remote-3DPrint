#include "printer.h"
#include "printer_mks.h"
#include "worker.h"
#include "model_storage.h"
#include "pspkernel.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void host_init(const char *);
static unsigned seq;static int fail_temps;
static atomic_int finished;
int mks_fetch_temps(MksTemps *o){memset(o,0,sizeof(*o));if(fail_temps){strcpy(o->err,"injected error");return -1;}unsigned n=++seq;o->ok=1;o->hotend=n;o->hotend_target=3*n;o->bed=2*n;o->bed_target=4*n;return 0;}
int mks_list_files(MksFileList *o){memset(o,0,sizeof(*o));o->count=(seq&1)?64:1;for(int i=0;i<o->count;i++)snprintf(o->names[i],64,"batch_%u_%d.gcode",seq,i);return 0;}
int mks_print_status(MksPrintStatus *o){memset(o,0,sizeof(*o));o->ok=1;return 0;}
int mks_select_and_print(const char *n,char *e,int c){(void)n;(void)e;(void)c;return 0;}
int mks_delete(const char *n,char *e,int c){(void)n;(void)e;(void)c;return 0;}
#define SIMPLE(name) int name(char *e,int c){(void)e;(void)c;return 0;}
SIMPLE(mks_pause) SIMPLE(mks_resume) SIMPLE(mks_home)
#define VALUE(name) int name(int v,char *e,int c){(void)v;(void)e;(void)c;return 0;}
VALUE(mks_set_hotend) VALUE(mks_set_bed) VALUE(mks_set_speed) VALUE(mks_set_flow)
int mks_upload_ex(const char *n,const void *b,int s,FlsunUploadProgress f,void *u,char *e,int c){(void)n;(void)b;(void)e;(void)c;if(f)f(s,s,u);return 0;}
int mks_wait_file_on_sd(const char *n,int t,char *e,int c){(void)n;(void)t;(void)e;(void)c;return 0;}
int sock_task_expired(void){return 0;}
int net_worker_submit(int type,const char *name,const struct file_entry *e,int ap,int v,uint32_t delay,uint32_t interval,uint32_t timeout){(void)type;(void)name;(void)e;(void)ap;(void)v;(void)delay;(void)interval;(void)timeout;return 0;}
static void execute(int type){struct net_task t={0};t.type=type;printer_queue_push();printer_worker_execute(&t);printer_queue_done();}
static void *writer(void *unused){(void)unused;for(int i=0;i<1000;i++){execute(CMD_TEMPS);execute(CMD_FILES);}atomic_store(&finished,1);return NULL;}
int main(int argc,char **argv)
{
    assert(argc==2);host_init(argv[1]);model_storage_init();printer_init();
    pthread_t t;assert(!pthread_create(&t,NULL,writer,NULL));
    do {
        printer_poll();const struct printer *pr=printer_get();
        if(pr->temps_ok){assert(pr->bed==2*pr->hotend);assert(pr->hotend_target==3*pr->hotend);assert(pr->bed_target==4*pr->hotend);}
        int generation=-1;
        for(int i=0;i<pr->files.count;i++){int n=-1,k=-1;assert(sscanf(pr->files.items[i].name,"batch_%d_%d.gcode",&n,&k)==2);assert(k==i);if(generation<0)generation=n;else assert(n==generation);}
        struct printer_result r;while(printer_next_result(&r))assert(r.rc==PRINTER_OK);
    } while(!atomic_load(&finished));
    pthread_join(t,NULL);struct printer_result r;while(printer_next_result(&r)){}
    fail_temps=1;execute(CMD_TEMPS);execute(CMD_STATUS);
    assert(printer_next_result(&r) && r.rc==PRINTER_ERR && !strcmp(r.error,"injected error"));
    assert(printer_next_result(&r) && r.rc==PRINTER_OK);
    printer_poll();assert(!printer_get()->busy);
    puts("PASS printer: 1000 concurrent temp/list snapshot pairs and error result survives next success");
}
