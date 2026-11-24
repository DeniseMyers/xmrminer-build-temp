# libstratum

Stratum protocol client (JSON-RPC 2.0 over TCP/TLS/WebSocket).

## API

```c
int  stratum_connect(stratum_ctx_t *ctx, const char *url, const char *user, const char *pass);
int  stratum_login(stratum_ctx_t *ctx);
int  stratum_submit(stratum_ctx_t *ctx, const char *job_id, const char *nonce_hex, const char *result_hex);
void stratum_disconnect(stratum_ctx_t *ctx);
bool stratum_get_job(stratum_ctx_t *ctx, stratum_job_t *job);
void *stratum_recv_loop(void *arg);   // runs in a background thread
```

Supports pool URLs: `stratum+tcp://`, `stratum+ssl://`, `ws://`, `wss://`.

## Dependencies

- libws (sockets/WebSocket)
- libjson (JSON parsing)
- libcrypto (encrypted strings, auth key)
- libutils (hex conversion)

## Build

```
make CC=gcc CFLAGS="-O2 -Wall -Wextra" FEATURE_TLS=1
```
