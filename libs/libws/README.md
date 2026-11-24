# libws

TCP/TLS socket helpers and WebSocket (RFC 6455) client implementation.

## API

```c
int  ws_tcp_connect(ws_conn_t *c, const char *host, int port, int use_tls);
int  ws_tcp_send(ws_conn_t *c, const void *data, size_t len);
int  ws_tcp_recv(ws_conn_t *c, void *buf, size_t len, int timeout_ms);

int  ws_connect(ws_conn_t *c, const char *host, int port, int use_tls, const char *auth_key);
int  ws_send_frame(ws_conn_t *c, const void *data, size_t len);
int  ws_recv_frame(ws_conn_t *c, char *buf, size_t buf_size, int timeout_ms);
void ws_close(ws_conn_t *c);
```

## Dependencies

- OpenSSL (`libssl.a`, `libcrypto.a`)

## Build

```
make CC=gcc CFLAGS="-O2 -Wall -Wextra" FEATURE_TLS=1
```
