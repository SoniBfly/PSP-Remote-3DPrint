#include "www_bundle.h"
#include "pspiofilemgr.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern void host_init(const char *);
static unsigned exports,check_files,check_bytes,export_bytes,errors;
static void progress(const char *phase,unsigned f,unsigned ft,unsigned b,unsigned bt,const char *path,int error,void *user)
{
    (void)path;(void)user;
    if(error){errors++;return;}
    assert(f<=ft && b<=bt);
    if(!strcmp(phase,"Checking web files")){assert(f>=check_files);check_files=f;check_bytes=b;}
    if(!strcmp(phase,"Exporting web files")){assert(b>=export_bytes);exports++;export_bytes=b;}
}
static void reset(void){exports=check_files=check_bytes=export_bytes=errors=0;}
int main(int argc,char **argv)
{
    assert(argc==2);host_init(argv[1]);
    assert(!www_bundle_ensure(progress,NULL));assert(exports && check_files==2 && export_bytes==100009 && !errors);
    reset();assert(!www_bundle_ensure(progress,NULL));assert(!exports && check_files==2 && check_bytes==100009);
    char path[1200];snprintf(path,sizeof(path),"%s/index.html",cfg_www_dir());int fd=sceIoOpen(path,PSP_O_WRONLY,0);assert(fd>=0);assert(sceIoWrite(fd,"changed!!",9)==9);sceIoClose(fd);
    host_no_replace=1;reset();assert(!www_bundle_ensure(progress,NULL));assert(export_bytes==9 && check_files==2);
    fd=sceIoOpen(path,PSP_O_WRONLY,0);assert(fd>=0);assert(sceIoWrite(fd,"changed!!",9)==9);sceIoClose(fd);
    host_fail_write=1;reset();assert(www_bundle_ensure(progress,NULL)<0 && errors);host_fail_write=0;
    puts("PASS www: first export/current check/same-size corruption/selective export/write error");
}
