/* Actual config implementation; synthetic ini only, isolated test directory. */
#include "config.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
extern int host_fail_write,host_no_replace;
static void *writer(void *p) {
    (void)p;
    for(int i=0;i<5000;i++) {
        cfg_set_printer_ip(i&1 ? "10.1.2.3" : "172.16.20.254");
        cfg_set_mks_port(i&1 ? 8080 : 8888);
    }
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==2 && chdir(argv[1])==0);assert(mkdir("ms0:",0700)==0);
    cfg_load();cfg_set_printer_ip("10.1.2.3");
    pthread_t t;assert(pthread_create(&t,NULL,writer,NULL)==0);
    for(int i=0;i<5000;i++) {
        struct cfg_printer_connection c;assert(cfg_copy_printer_connection(&c)==0);
        assert(!strcmp(c.ip,"10.1.2.3") || !strcmp(c.ip,"172.16.20.254"));
        assert(c.port==8080 || c.port==8888);
    }
    pthread_join(t,NULL);
    cfg_set_printer_ip("10.1.2.3");assert(cfg_save_force()==0 && !cfg_is_dirty());
    cfg_set_printer_ip("172.16.20.254");host_fail_write=1;
    assert(cfg_save_force()<0 && cfg_is_dirty());host_fail_write=0;
    cfg_load();assert(!strcmp(cfg_printer_ip(),"10.1.2.3"));
    cfg_set_printer_ip("172.16.20.254");host_no_replace=1;
    assert(cfg_save_force()==0);host_no_replace=0;
    cfg_load();assert(!strcmp(cfg_printer_ip(),"172.16.20.254"));
    puts("PASS config: 5000 concurrent connection snapshots/save failure retains old ini/rename fallback");
    return 0;
}
