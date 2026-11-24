/* websocket.c — Standalone WebSocket client implementation */

#include "websocket.h"
#include "auth_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

/* Create and connect a TCP socket to host:port; returns fd or -1 on failure */
static int create_socket(const char *host, int port)
{
    struct addrinfo hints, *res;
    char port_str[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return -1;

    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Base64 encode raw bytes into a null-terminated string */
static void base64_encode(char *out, const uint8_t *in, size_t len)
{
    const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)in[i] << 16;
        if (i + 1 < len) val |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) val |= (uint32_t)in[i + 2];
        out[j++] = table[(val >> 18) & 0x3F];
        out[j++] = table[(val >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? table[(val >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? table[val & 0x3F] : '=';
    }
    out[j] = '\0';
}

/* Parse a ws:// or wss:// URL into host, port, path, and TLS flag */
static int parse_url(const char *url, char *host, int *port, char *path, int *use_tls)
{
    const char *p = url;
    *use_tls = 0;

    if (strncmp(p, "wss://", 6) == 0) {
        *use_tls = 1;
        *port = 443;
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        *port = 80;
        p += 5;
    } else {
        *port = 80;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t host_len;

    if (colon && (!slash || colon < slash)) {
        host_len = colon - p;
        *port = atoi(colon + 1);
    } else if (slash) {
        host_len = slash - p;
    } else {
        host_len = strlen(p);
    }

    if (host_len >= 256) host_len = 255;
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    if (slash) {
        snprintf(path, 512, "%s", slash);
    } else {
        strcpy(path, "/");
    }

    return 0;
}

/* Connect to a WebSocket server: TCP connect, optional TLS, HTTP upgrade handshake */
int ws_connect(ws_conn_t *ws, const char *url)
{
    char host[256], path[512];
    int port, use_tls;

    memset(ws, 0, sizeof(*ws));
    parse_url(url, host, &port, path, &use_tls);
    strncpy(ws->host, host, sizeof(ws->host) - 1);
    ws->use_tls = use_tls;

    ws->fd = create_socket(host, port);
    if (ws->fd < 0) {
        fprintf(stderr, "[ws] failed to connect to %s:%d\n", host, port);
        return -1;
    }

    if (use_tls) {
        ws->ctx = SSL_CTX_new(TLS_client_method());
        if (!ws->ctx) {
            close(ws->fd);
            return -1;
        }
        ws->ssl = SSL_new(ws->ctx);
        SSL_set_fd(ws->ssl, ws->fd);
        SSL_set_tlsext_host_name(ws->ssl, host);
        if (SSL_connect(ws->ssl) <= 0) {
            fprintf(stderr, "[ws] TLS handshake failed\n");
            SSL_free(ws->ssl);
            SSL_CTX_free(ws->ctx);
            close(ws->fd);
            return -1;
        }
    }

    uint8_t key[16];
    RAND_bytes(key, sizeof(key));
    char key_b64[256];
    base64_encode(key_b64, key, sizeof(key));

    char request[2048];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "X-Auth-Key: %s\r\n"
        "\r\n",
        path, host, key_b64, AUTH_KEY);

    if (use_tls) {
        SSL_write(ws->ssl, request, strlen(request));
    } else {
        write(ws->fd, request, strlen(request));
    }

    char resp[4096] = {0};
    size_t total = 0;
    int n;
    do {
        if (use_tls)
            n = SSL_read(ws->ssl, resp + total, sizeof(resp) - total - 1);
        else
            n = read(ws->fd, resp + total, sizeof(resp) - total - 1);
        if (n > 0) total += n;
    } while (total < 4 && n > 0);

    if (strstr(resp, "101") == NULL) {
        fprintf(stderr, "[ws] handshake failed: %s\n", resp);
        ws_close(ws);
        return -1;
    }

    printf("[ws] connected to %s:%d (%s)\n", host, port, use_tls ? "TLS" : "plain");
    return 0;
}

/* Send a WebSocket frame with the given opcode and payload */
static int send_frame(ws_conn_t *ws, uint8_t opcode, const uint8_t *payload, size_t len)
{
    uint8_t header[14];
    int hlen = 0;

    header[0] = 0x80 | opcode;

    if (len < 126) {
        header[1] = (uint8_t)len;
        hlen = 2;
    } else if (len < 65536) {
        header[1] = 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        hlen = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++)
            header[2 + i] = (len >> (56 - i * 8)) & 0xFF;
        hlen = 10;
    }

    if (ws->use_tls) {
        SSL_write(ws->ssl, header, hlen);
        if (len > 0)
            SSL_write(ws->ssl, payload, len);
    } else {
        write(ws->fd, header, hlen);
        if (len > 0)
            write(ws->fd, payload, len);
    }
    return 0;
}

/* Send a text frame (opcode 0x1) containing data */
int ws_send(ws_conn_t *ws, const char *data, size_t len)
{
    return send_frame(ws, 0x01, (const uint8_t *)data, len);
}

/* Read raw bytes from the socket with a timeout (handles TLS) */
static int ws_read_raw(ws_conn_t *ws, uint8_t *buf, size_t size, int timeout_ms)
{
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    setsockopt(ws->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (ws->use_tls)
        return SSL_read(ws->ssl, buf, size);
    else
        return read(ws->fd, buf, size);
}

/* Receive a WebSocket frame: read header, parse length, read payload */
int ws_recv(ws_conn_t *ws, char *buf, size_t buf_size, int timeout_ms)
{
    uint8_t header[14];
    int n = ws_read_raw(ws, header, 2, timeout_ms);
    if (n <= 0) return n;

    size_t payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        n = ws_read_raw(ws, header + 2, 2, timeout_ms);
        if (n <= 0) return -1;
        payload_len = ((size_t)header[2] << 8) | header[3];
    } else if (payload_len == 127) {
        n = ws_read_raw(ws, header + 2, 8, timeout_ms);
        if (n <= 0) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | header[2 + i];
    }

    if (payload_len >= buf_size)
        return -1;

    size_t total = 0;
    while (total < payload_len) {
        n = ws_read_raw(ws, (uint8_t *)buf + total, payload_len - total, timeout_ms);
        if (n <= 0) break;
        total += n;
    }

    buf[total] = '\0';
    return (int)total;
}

/* Close WebSocket connection: TLS shutdown, free SSL, close socket */
void ws_close(ws_conn_t *ws)
{
    if (ws->ssl) {
        SSL_shutdown(ws->ssl);
        SSL_free(ws->ssl);
    }
    if (ws->ctx)
        SSL_CTX_free(ws->ctx);
    if (ws->fd >= 0)
        close(ws->fd);
    ws->fd = -1;
    ws->ssl = NULL;
    ws->ctx = NULL;
}
