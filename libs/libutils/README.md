# libutils

Hexadecimal conversion and CPU feature detection.

## API

```c
void bin2hex(char *out, const uint8_t *in, size_t len);
int  hex2bin(uint8_t *out, const char *in, size_t out_len);
int  hex_to_bytes(const char *hex, uint8_t *bytes, size_t len);
int  bytes_to_hex(char *hex, const uint8_t *bytes, size_t len);
int  has_hardware_aes(void);
```

## Dependencies

None (standalone).

## Build

```
make CC=gcc CFLAGS="-O2 -Wall -Wextra"
```
