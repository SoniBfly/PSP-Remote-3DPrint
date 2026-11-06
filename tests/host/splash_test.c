#include "splash.h"
#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned pixels[272][480];
void ui_begin(void){}
void ui_end(void){}
void ui_pixel(int x,int y,unsigned c){if(x>=0&&x<480&&y>=0&&y<272)pixels[y][x]=c;}
void ui_fill(int x,int y,int w,int h,unsigned c){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)ui_pixel(xx,yy,c);}
void ui_rect(int x,int y,int w,int h,unsigned c){ui_fill(x,y,w,1,c);ui_fill(x,y+h-1,w,1,c);ui_fill(x,y,1,h,c);ui_fill(x+w-1,y,1,h,c);}
int sceDisplayWaitVblankStart(void){return 0;}
unsigned long long sceKernelGetSystemTimeWide(void){return 1000000;}
int main(int argc,char **argv)
{
    assert(argc==2);
    splash_show("Exporting web files",6,13,50000,100000,"assets/test.bin",0);
    assert(pixels[252][14]==UI_COL_ACCENT);assert(pixels[252][239]==UI_COL_ACCENT);assert(pixels[252][240]!=UI_COL_ACCENT);
    char path[2000];snprintf(path,sizeof(path),"%s/startup_preview.ppm",argv[1]);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n480 272\n255\n");
    for(int y=0;y<272;y++)for(int x=0;x<480;x++){unsigned c=pixels[y][x];unsigned char rgb[3]={c,c>>8,c>>16};fwrite(rgb,1,3,f);}
    fclose(f);
    puts("PASS splash: actual renderer artwork and byte-based 50% progress geometry");
}
