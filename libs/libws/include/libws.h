#ifndef LIBWS_H
#define LIBWS_H

#include <stdint.h>
#include <stddef.h>
#include <openssl/ssl.h>

typedef struct {
    int fd;
    SSL *ssl;
    SSL_CTX *ssl_ctx;
    int use_tls;
    int use_ws;
    char host[256];
} ws_conn_t;

int ws_tcp_connect(ws_conn_t *c, const char *host, int port, int use_tls);
int ws_tcp_send(ws_conn_t *c, const void *data, size_t len);
int ws_tcp_recv(ws_conn_t *c, void *buf, size_t len, int timeout_ms);

int ws_connect(ws_conn_t *c, const char *host, int port, int use_tls, const char *auth_key);
int ws_send_frame(ws_conn_t *c, const void *data, size_t len);
int ws_recv_frame(ws_conn_t *c, char *buf, size_t buf_size, int timeout_ms);
void ws_close(ws_conn_t *c);

#endif
