# libcrypto

Compile-time string encryption macros, authentication key, and XOR decoy stubs.

## API

```c
const char *auth_key_get(void);   // decrypted auth key
void fake_xor_decoys(void);       // XOR padding to complicate static analysis
```

String macros (`STR_*`): compile-time XOR-encrypted string literals via `_DEC_STR`.

## Dependencies

None (standalone).

## Build

```
make CC=gcc CFLAGS="-O2 -Wall -Wextra"
```
