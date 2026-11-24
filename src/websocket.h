/* websocket.h — Standalone WebSocket client: connect, send, recv, close */

#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <openssl/ssl.h>

/* WebSocket connection handle with optional TLS and read buffer */
typedef struct {
    int fd;
    SSL *ssl;
    SSL_CTX *ctx;
    int use_tls;
    char host[256];
    uint8_t read_buf[65536];
    size_t read_len;
} ws_conn_t;

/* Connect to a WebSocket server (ws:// or wss://) returns 0 on success */
int  ws_connect(ws_conn_t *ws, const char *url);
/* Send a text frame */
int  ws_send(ws_conn_t *ws, const char *data, size_t len);
/* Receive a text frame; returns payload size, 0 on close, -1 on error */
int  ws_recv(ws_conn_t *ws, char *buf, size_t buf_size, int timeout_ms);
/* Close the WebSocket connection and free resources */
void ws_close(ws_conn_t *ws);

#endif
