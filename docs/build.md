# Build System

## Overview

XMRMiner uses OLLVM-16 for code obfuscation, with a post-build patcher that encrypts all `.rodata` strings and injects a runtime decoder using a per-build rolling key.

## Requirements

- OLLVM-16: `/home/gamerwaka/ollvm/bin/clang-16`
- GCC 16.1.1 system includes (via `-isystem`)
- CMake (for RandomX)
- Python 3 (for patcher)
- Static OpenSSL 3.4.1 (pre-built in `static_lib/`)

## Build Commands

```bash
# Full build
make clean && make

# Build without obfuscation (for debugging)
make OBF_FLAGS=""

# Build with specific features
make FEATURE_TLS=0 FEATURE_WS=0
```

## Makefile Structure

```makefile
OLLVM   = /home/gamerwaka/ollvm/bin/clang-16
GCCINC  = -isystem /usr/lib/gcc/x86_64-pc-linux-gnu/16.1.1/include
OBF_FLAGS = -mllvm -sub -mllvm -split -mllvm -fla

CC      = $(OLLVM)
CFLAGS  = -O1 -march=native -Wall -Wextra -Isrc -pthread $(GCCINC) $(OBF_FLAGS)
LDFLAGS = -static -lpthread -lm -s
SRCS    = src/main.c src/stratum.c src/miner.c src/utils.c src/cJSON.c src/auth_key.c src/decoy.c
```

## Build Pipeline

```
Source (.c) → OLLVM Compiler → Object (.o) → Linker → Binary → Patcher → Final Binary
                                                  ↓
                                             Static libs
                                             (libssl.a, libcrypto.a, librandomx.a)
```

### Step 1: Compilation
Each `.c` file compiled with OLLVM flags:
- `-O1` — Optimization level 1
- `-march=native` — Target current CPU
- `-mllvm -sub` — Instruction substitution
- `-mllvm -split` — Basic block splitting
- `-mllvm -fla` — Control flow flattening

### Step 2: Linking
Static linking with:
- `librandomx.a` — RandomX algorithm
- `libssl.a` — OpenSSL TLS
- `libcrypto.a` — OpenSSL crypto
- `-lstdc++` — C++ standard library
- `-static` — No dynamic libraries
- `-s` — Symbol stripping

### Step 3: Patching
`patch_strings.py` runs automatically:
1. Reads ELF binary
2. Finds `.rodata` section
3. Encrypts every byte with a per-build rolling XOR scheme (seed, 4-byte key, index mixing)
4. Injects decoder shellcode at entry point
5. Renames RandomX RTTI symbols

## Feature Flags

| Flag | Default | Description |
|------|---------|-------------|
| `FEATURE_TLS` | 1 | TLS/SSL support |
| `FEATURE_WS` | 1 | WebSocket support |
| `FEATURE_FULL_MEM` | 1 | Full 2GB dataset |
| `FEATURE_JIT` | 1 | JIT compilation |
| `FEATURE_HARD_AES` | 1 | Hardware AES-NI |
| `FEATURE_LARGE_PAGES` | 0 | Large pages (needs root) |
| `FEATURE_VERBOSE` | 1 | Verbose output |
| `FEATURE_COLORS` | 1 | Colored output |
| `FEATURE_KEEPALIVE` | 1 | Connection keepalive |

## Static Libraries

### OpenSSL 3.4.1
Built on Ubuntu 20.04 RunPod:
```bash
./Configure no-shared no-dso no-tests
make -j$(nproc)
```

Files:
- `static_lib/libssl.a`
- `static_lib/libcrypto.a`

### RandomX
Cloned and built automatically:
```bash
git clone --depth 1 https://github.com/tevador/RandomX.git lib/randomx
cd lib/randomx/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
make -j$(nproc)
```

## Patcher (`patch_strings.py`)

### What it does

1. **String Encryption**: Encrypts all `.rodata` bytes with a per-build rolling XOR scheme (seed, 4-byte key, index mixing)
2. **Decoder Injection**: Adds shellcode at ELF entry point
3. **Symbol Renaming**: Mangles RandomX C++ RTTI names

### Decoder Shellcode

```asm
; Injected at ELF entry point
push rdi
push rsi
push rdx

; mprotect(.rodata, PROT_READ|PROT_WRITE)
mov rax, 10
syscall

; XOR loop
xor rcx, rcx
.loop:
    xor byte [rax+rcx], per-build rolling key
    inc rcx
    cmp rcx, rdx
    jl .loop

; mprotect(.rodata, PROT_READ)
mov rax, 10
syscall

pop rdx
pop rsi
pop rdi
jmp _start  ; rel32 for PIE
```

### Statistics

```
patched 769168 strings
decoder at 0x984830
remaining visible: ~25000 (junk strings)
```

## Binary Output

```
-rwxr-xr-x 1 gamerwaka gamerwaka 7.8M Jun 22 10:00 xmrminer
```

- Statically linked
- OLLVM-obfuscated
- All `.rodata` encrypted with per-build rolling key
- Entry point patched
- Symbols stripped

## Cross-Compilation

For other architectures, modify Makefile:
```makefile
OLLVM = /path/to/ollvm/bin/clang
CFLAGS += -march=armv8-a  # For ARM
```

## Debug Build

```bash
make OBF_FLAGS="" FEATURE_VERBOSE=1
./xmrminer --run
```

Disables OLLVM obfuscation for debugging.
