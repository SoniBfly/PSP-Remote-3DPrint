/* Validate every installed file, then export only changed files with real counters. */
#include "www_bundle.h"
#include "www_bundle_meta.h"
#include "config.h"
#include "file_io.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
extern const unsigned char _binary_www_pak_start[],_binary_www_pak_end[];
struct bundle_file { char rel[192]; const unsigned char *data; unsigned size; int changed; };
static unsigned u32le(const unsigned char *p)
{ return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static unsigned u16le(const unsigned char *p) { return p[0]|(unsigned)p[1]<<8; }
static int full_path(const char *rel,char *out,int cap)
{
    if (!rel[0] || rel[0]=='/' || strstr(rel,"..") || strchr(rel,'\\') || strchr(rel,':')) return -1;
    int n=snprintf(out,cap,"%s/%s",cfg_www_dir(),rel);
    return n<0 || n>=cap ? -1 : 0;
}
static void ensure_dirs(char *path)
{
    int start=strlen(cfg_www_dir())+1;
    for(int i=start;path[i];i++) if(path[i]=='/') {
        path[i]=0;sceIoMkdir(path,0777);path[i]='/';
    }
}
int www_bundle_ensure(www_progress_fn progress,void *user)
{
    const unsigned char *pak=_binary_www_pak_start,*end=_binary_www_pak_end,*at;
    struct bundle_file *files=NULL;
    unsigned count=0,total=0,checked=0,export_total=0,export_count=0,done=0,exported=0;
    unsigned version=0;
    char path[256],temp[272]; const char *error="bundle corrupt";
#define REPORT(phase,n,b,err) do { if(progress) progress(phase,n,count,b,total,path,err,user); } while(0)
    path[0]=0;
    REPORT("Checking web files",0,0,0);
    if (end-pak<16 || memcmp(pak,"FLWW",4)) goto fail;
    version=u32le(pak+4);count=u32le(pak+8);total=u32le(pak+12);
    if(version!=WWW_BUNDLE_VERSION || !count || count>1024 || total>32*1024*1024) goto fail;
    files=calloc(count,sizeof(*files)); if(!files) { error="bundle memory error";goto fail; }
    at=pak+16; unsigned sum=0;
    /* Validate the complete bundle before changing the installed site. */
    for(unsigned i=0;i<count;i++) {
        unsigned len,size;
        if ((size_t)(end-at)<2) goto fail;
        len=u16le(at);at+=2;
        if (!len || len>=sizeof(files[i].rel) || (size_t)(end-at)<len+4) goto fail;
        memcpy(files[i].rel,at,len);files[i].rel[len]=0;at+=len;
        if (memchr(files[i].rel,0,len) || full_path(files[i].rel,path,sizeof(path))) goto fail;
        size=u32le(at);at+=4;
        if((size_t)(end-at)<size || size>total || sum>total-size) goto fail;
        files[i].size=size;files[i].data=at;at+=size;sum+=size;
    }
    if (at!=end || sum!=total) goto fail;
    cfg_ensure_dirs();
    REPORT("Checking web files",0,0,0);
    for(unsigned i=0;i<count;i++) {
        SceIoStat st;unsigned off=0;int changed=0;
        SceUID fd;char buf[16384];
        full_path(files[i].rel,path,sizeof(path));
        file_recover(path);
        if(sceIoGetstat(path,&st)<0 || st.st_size!=files[i].size) changed=1;
        fd=changed ? -1 : sceIoOpen(path,PSP_O_RDONLY,0);
        if(fd<0) changed=1;
        while(!changed && off<files[i].size) {
            unsigned chunk=files[i].size-off;if(chunk>sizeof(buf))chunk=sizeof(buf);
            int n=sceIoRead(fd,buf,chunk);
            if(n>0) checked+=(unsigned)n;
            if(n!=(int)chunk || memcmp(buf,files[i].data+off,chunk)) changed=1;
            else off+=chunk;
            REPORT("Checking web files",i,checked,0);
        }
        if(fd>=0)sceIoClose(fd);
        files[i].changed=changed;
        if(changed) { export_total+=files[i].size;export_count++; }
        REPORT("Checking web files",i+1,checked,0);
    }
    /* Checking progress counts completed file checks, not imaginary bytes read. */
    if(export_count) {
        if(progress)progress("Exporting web files",0,export_count,0,export_total,"",0,user);
        for(unsigned i=0;i<count;i++) if(files[i].changed) {
            unsigned off=0;SceUID fd;
            full_path(files[i].rel,path,sizeof(path));ensure_dirs(path);
            snprintf(temp,sizeof(temp),"%s.pending",path);
            fd=sceIoOpen(temp,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
            if(fd<0) {error="web open failed";goto fail;}
            while(off<files[i].size) {
                unsigned chunk=files[i].size-off;if(chunk>32768)chunk=32768;
                int n=sceIoWrite(fd,files[i].data+off,chunk);
                if(n!=(int)chunk) {sceIoClose(fd);error="web write failed";goto fail;}
                off+=n;done+=n;
                if(progress)progress("Exporting web files",exported,export_count,done,export_total,files[i].rel,0,user);
            }
            sceIoClose(fd);
            if(file_replace(temp,path)<0) {error="web replace failed";goto fail;}
            exported++;
            if(progress)progress("Exporting web files",exported,export_count,done,export_total,files[i].rel,0,user);
        }
    }
    snprintf(path,sizeof(path),"%s/.flsun_www_ver",cfg_www_dir());
    char installed[16]={0};
    SceUID current=sceIoOpen(path,PSP_O_RDONLY,0);
    unsigned have=0;
    if(current>=0) {int n=sceIoRead(current,installed,sizeof(installed)-1);sceIoClose(current);if(n>0)sscanf(installed,"%x",&have);}
    if(!export_count && have==version) {free(files);return 0;}
    SceUID marker=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
    char text[16];int len=snprintf(text,sizeof(text),"%08X\n",version);
    if(marker<0) {error="web marker open failed";goto fail;}
    int written=sceIoWrite(marker,text,len);sceIoClose(marker);
    if(written!=len) {error="web marker write failed";goto fail;}
    free(files);return 0;
fail:
    if(progress)progress(error,exported,export_count ? export_count : count,done,
                         export_total ? export_total : total,path,1,user);
    free(files);return -1;
#undef REPORT
}
