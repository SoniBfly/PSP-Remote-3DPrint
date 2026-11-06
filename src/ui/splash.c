/* Startup artwork uses the existing software framebuffer, not GU. */
#include "splash.h"
#include "splash_art.h"
#include "ui.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>

void splash_show(const char *phase,unsigned files,unsigned total_files,
                 unsigned bytes,unsigned total_bytes,const char *file,int error)
{
    char text[64]; unsigned done=files,total=total_files;
    if (!strcmp(phase,"Exporting web files")) { done=bytes; total=total_bytes; }
    ui_begin();
    for (int y=0;y<272;y++)
        for (int x=0;x<480;x++) ui_pixel(x,y,splash_palette[splash_pixels[y*480+x]]);
    ui_fill(0,236,480,36,0xff140802u);
    ui_text(12,239,1,error ? UI_COL_DANGER : UI_COL_TEXT,phase);
    if (total) {
        unsigned fill=(unsigned)(((unsigned long long)done*452)/total);
        if (fill>452) fill=452;
        ui_rect(12,250,456,6,UI_COL_BORDER);
        ui_fill(14,252,fill,2,error ? UI_COL_DANGER : UI_COL_ACCENT);
    }
    snprintf(text,sizeof(text),"%u/%u files  %u/%u KiB",files,total_files,bytes/1024,total_bytes/1024);
    ui_text(12,261,1,UI_COL_DIM,error ? "X: continue   START: exit" : (total_files ? text : file));
    ui_end();
    /* Present before the next blocking filesystem operation. No timed fake progress. */
    sceDisplayWaitVblankStart();
}
void splash_progress(const char *phase,unsigned files,unsigned total_files,
    unsigned bytes,unsigned total_bytes,const char *file,int error,void *user)
{
    static unsigned long long last;
    static char previous[32];
    unsigned long long now=sceKernelGetSystemTimeWide();
    (void)user;
    if (!error && files!=total_files && !strcmp(previous,phase) && now-last<33000) return;
    snprintf(previous,sizeof(previous),"%s",phase);last=now;
    splash_show(phase,files,total_files,bytes,total_bytes,file,error);
}
