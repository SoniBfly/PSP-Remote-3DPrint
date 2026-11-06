#ifndef FLSUN_FILE_IO_H
#define FLSUN_FILE_IO_H
#include <pspiofilemgr.h>
#include <stdio.h>
/* Recover an interrupted replacement before opening or checking a target. */
static inline void file_recover(const char *target)
{
    char backup[300]; SceIoStat st;
    if (snprintf(backup,sizeof(backup),"%s.replace_backup",target)>=(int)sizeof(backup)) return;
    if (sceIoGetstat(target,&st)<0 && sceIoGetstat(backup,&st)>=0) sceIoRename(backup,target);
}
static inline int file_replace(const char *pending,const char *target)
{
    char backup[300]; SceIoStat st;
    file_recover(target);
    if (sceIoRename(pending,target)>=0) return 0;
    if (snprintf(backup,sizeof(backup),"%s.replace_backup",target)>=(int)sizeof(backup) ||
        sceIoGetstat(target,&st)<0 || sceIoGetstat(backup,&st)>=0) return -1;
    if (sceIoRename(target,backup)<0) return -1;
    if (sceIoRename(pending,target)<0) { sceIoRename(backup,target); return -1; }
    sceIoRemove(backup);
    return 0;
}
#endif
