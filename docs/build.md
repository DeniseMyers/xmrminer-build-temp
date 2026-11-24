# Build

## Overview

Current build uses **system gcc/g++** (no OLLVM in the default path), static linking (`STATIC=1`), and a post-build `patch_strings.py` step that encrypts `.rodata` and injects a runtime decoder.

Optional hardening: **Tigress light automated hiding** on cold files only (no RandomX/miner hot path).

## Requirements

- GCC/G++ (Linux x86_64)
- CMake + make
- Python 3
- Static libs available in CI: `libssl-dev`, `zlib1g-dev`, `libzstd-dev`
- For Tigress: Tigress 4.2.x binary

## Local build

```bash
# normal static build
make clean && make STATIC=1 -j4

# dynamic (if static libs not preferred)
make clean && make -j4
```

## Tigress (light, cold files only)

Runs on: `src/main.c`, `src/stealth.c`, `libs/libcrypto/src/auth_key.c`, `libs/libcrypto/src/auth_key_data.c`, `libs/libcrypto/src/decoys.c`, `libs/libantidebug/src/antidebug.c`, `libs/libdecoy/src/decoy.c`, `libs/libutils/src/hex.c`, `libs/libutils/src/cpu.c`.

Skips: RandomX, miner hot path, stratum/ws/cJSON.

```bash
make tigress-heavy   # target name kept; currently light automated hiding
make clean && make STATIC=1 -j4
```

Transforms: `InitEntropy + InitOpaque + AddOpaque(call,count=1) + EncodeLiterals` (no CleanUp rename, no Virtualize/Flatten/EncodeArithmetic).

## GitHub Actions

Workflow: `.github/workflows/build.yml` (compile-only, no miner execution).

- Installs deps + ccache
- Downloads Tigress from official site and runs `make tigress-heavy`
- Builds `make STATIC=1 CC="ccache gcc" CXX="ccache g++" -j4`
- Verifies with `file`/`sha256sum` only
- Uploads `xmrminer` and `xmrminer_stealth` artifacts

## Notes

- `patch_strings.py` encrypts `.rodata` and injects decoder; it runs as part of normal build targets.
- `xmrminer_stealth` is the stripped/hardened target; `xmrminer` is the regular CLI target.
- Static CI artifacts are much larger than dynamic builds; this is expected.
