#include "model_storage.h"
#include "pspiofilemgr.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void host_init(const char *);
static void *writer(void *unused)
{
    (void)unused;char body[4096];
    for(int i=0;i<500;i++){memset(body,i&1 ? 'a':'b',sizeof(body));assert(!model_storage_write("stress",MODEL_FILE_GCODE,body,sizeof(body),1));}
    return NULL;
}
int main(int argc,char **argv)
{
    assert(argc==2);host_init(argv[1]);model_storage_init();
    char slug[64],again[64],path[220];ModelFileKind kind;
    const char *bad[]={"...gcode","..gcode","../cube.gcode","a/b.gcode","a\\b.gcode","a:b.gcode",".gcode"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(model_storage_resolve_legacy_name(bad[i],slug,sizeof(slug),&kind)<0);
    assert(!model_storage_resolve_legacy_name("part.stl.gcode",slug,sizeof(slug),&kind));
    assert(!strcmp(slug,"part.stl"));assert(!model_storage_sanitize_slug(slug,again,sizeof(again)));assert(!strcmp(slug,again));
    assert(!model_storage_path(slug,kind,path,sizeof(path)));assert(strstr(path,"/part.stl/model.gcode"));
    assert(!model_storage_write(slug,kind,"original",8,0));assert(model_storage_write(slug,kind,"new",3,0)==-5);
    host_no_replace=1;
    assert(!model_storage_write(slug,kind,"original",8,1));
    host_fail_write=1;assert(model_storage_write(slug,kind,"broken",6,1)<0);host_fail_write=0;
    char *data;int size;assert(model_storage_read(slug,kind,&data,&size)==8);assert(!memcmp(data,"original",8));free(data);
    char body[4096];memset(body,'a',sizeof(body));assert(!model_storage_write("stress",MODEL_FILE_GCODE,body,sizeof(body),0));
    pthread_t t;assert(!pthread_create(&t,NULL,writer,NULL));
    for(int i=0;i<500;i++){assert(model_storage_read("stress",MODEL_FILE_GCODE,&data,&size)==4096);for(int j=1;j<size;j++)assert(data[j]==data[0]);free(data);}
    pthread_join(t,NULL);
    ModelStorageList list;assert(!model_storage_list(&list));assert(list.count==2);
    puts("PASS storage: traversal/idempotency/conflict/failed overwrite/500 concurrent snapshots");
}
