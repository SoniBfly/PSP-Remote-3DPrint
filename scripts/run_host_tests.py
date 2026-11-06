#!/usr/bin/env python3
# Compile actual C modules against POSIX/fake-clock adapters; not PSP runtime.
from pathlib import Path
import re,struct,subprocess,tempfile,os
ROOT=Path(__file__).resolve().parents[1]
HOST=ROOT/'tests/host'
def run(*args): subprocess.run([str(a) for a in args],check=True)
with tempfile.TemporaryDirectory(prefix='fdm-host-') as tmp:
    work=Path(tmp)
    flags=['cc','-std=gnu11','-g','-Wall','-Wextra','-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(HOST),'-I'+str(ROOT/'src/core'),'-I'+str(ROOT/'src/net')]
    if os.environ.get('HOST_SANITIZE'):
        flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    def compile_run(name,sources,extra=()):
        exe=work/name
        run(*flags,*extra,*sources,'-o',exe)
        data=work/(name+'-data');data.mkdir()
        run(exe,data)
    compile_run('config',[HOST/'host_io.c',ROOT/'src/core/config.c',HOST/'config_test.c'],['-DHOST_REAL_CONFIG'])
    compile_run('storage',[HOST/'host_io.c',ROOT/'src/core/model_storage.c',HOST/'storage_test.c'])
    compile_run('printer',[HOST/'host_io.c',ROOT/'src/core/model_storage.c',ROOT/'src/core/printer.c',HOST/'printer_test.c'])
    version=int(re.search(r'WWW_BUNDLE_VERSION\s+0x([0-9a-fA-F]+)',(ROOT/'src/net/www_bundle_meta.h').read_text()).group(1),16)
    entries=[('index.html',b'web-ready'),('assets/test.bin',b'x'*100000)]
    pak=b'FLWW'+struct.pack('<III',version,len(entries),sum(len(b) for _,b in entries))
    for name,body in entries:
        raw=name.encode();pak+=struct.pack('<H',len(raw))+raw+struct.pack('<I',len(body))+body
    (work/'fixture.pak').write_bytes(pak)
    asm=work/'fixture.s';asm.write_text('.section .rodata\n.global _binary_www_pak_start\n.global _binary_www_pak_end\n_binary_www_pak_start:\n.incbin "'+str(work/'fixture.pak')+'"\n_binary_www_pak_end:\n.section .note.GNU-stack,"",@progbits\n')
    compile_run('www',[HOST/'host_io.c',ROOT/'src/net/www_bundle.c',HOST/'www_test.c',asm])
    compile_run('splash',[ROOT/'src/ui/splash.c',ROOT/'src/ui/base/text.c',HOST/'splash_test.c'],['-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/ui'),'-I'+str(ROOT/'src/ui/base')])
    if os.environ.get('HOST_PREVIEW_OUT'):
        Path(os.environ['HOST_PREVIEW_OUT']).write_bytes((work/'splash-data/startup_preview.ppm').read_bytes())
    compile_run('worker',[ROOT/'src/net/worker.c',HOST/'worker_test.c'])
    compile_run('socket',[ROOT/'src/net/socket.c',HOST/'socket_test.c'],['-Dsin_len=sin_zero[0]'])
    compile_run('log',[HOST/'host_io.c',ROOT/'src/core/log.c',HOST/'log_test.c'],['-DHOST_REAL_LOG','-Dsin_len=sin_zero[0]'])
    src=(ROOT/'src/net/printer_mks.c').read_text();start=src.index('static int mks_reply_ok(');end=src.index('\n}',start)+2
    c=work/'ack.c';c.write_text('#include <assert.h>\n#include <string.h>\n#include <stdio.h>\n'+src[start:end]+r'''
int main(void){assert(mks_reply_ok("ok\r\n"));assert(mks_reply_ok("OK T:20\n"));assert(mks_reply_ok("File selected\n"));assert(!mks_reply_ok("echo:M23 book.gcode\n"));assert(!mks_reply_ok("not ok\n"));assert(!mks_reply_ok("ok\nError: failed\n"));assert(!mks_reply_ok(""));assert(!mks_reply_ok("echo:ok\n"));assert(mks_reply_ok("echo:M23 error.gcode\nFile opened: failure.gcode Size:10\nok\n"));assert(!mks_reply_ok("echo:Unknown command: M24\nok\n"));assert(!mks_reply_ok("File open failed.\nok\n"));puts("PASS ACK tokens and explicit errors");}
''')
    run('cc',c,'-o',work/'ack');run(work/'ack')
print('All host tests passed. PSP/PPSSPP/hardware behaviour still requires runtime checks.')
