#include "libws.h"
#include "cert_pin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

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
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void base64_encode(char *out, const uint8_t *in, size_t len)
{
    const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)in[i] << 16;
        if (i+1 < len) val |= (uint32_t)in[i+1] << 8;
        if (i+2 < len) val |= (uint32_t)in[i+2];
        out[j++] = table[(val>>18)&0x3F];
        out[j++] = table[(val>>12)&0x3F];
        out[j++] = (i+1<len) ? table[(val>>6)&0x3F] : '=';
        out[j++] = (i+2<len) ? table[val&0x3F] : '=';
    }
    out[j] = '\0';
}

int ws_tcp_connect(ws_conn_t *c, const char *host, int port, int use_tls)
{
    memset(c, 0, sizeof(*c));
    strncpy(c->host, host, sizeof(c->host) - 1);
    c->use_tls = use_tls;

    c->fd = create_socket(host, port);
    if (c->fd < 0) return -1;

    if (use_tls) {
        c->ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!c->ssl_ctx) { close(c->fd); c->fd = -1; return -1; }
        c->ssl = SSL_new(c->ssl_ctx);
        SSL_set_fd(c->ssl, c->fd);
        SSL_set_tlsext_host_name(c->ssl, host);
        if (SSL_connect(c->ssl) <= 0) {
            SSL_free(c->ssl); SSL_CTX_free(c->ssl_ctx);
            c->ssl = NULL; c->ssl_ctx = NULL;
            close(c->fd); c->fd = -1;
            return -1;
        }
        X509 *server_cert = SSL_get_peer_certificate(c->ssl);
        if (!server_cert) {
            SSL_free(c->ssl); SSL_CTX_free(c->ssl_ctx);
            c->ssl = NULL; c->ssl_ctx = NULL;
            close(c->fd); c->fd = -1;
            return -1;
        }
        unsigned char *der = NULL;
        int der_len = i2d_X509(server_cert, &der);
        int ok = (der_len > 0 && (size_t)der_len == XMRPROXY_CERT_LEN
                  && memcmp(der, XMRPROXY_CERT_DER, der_len) == 0);
        OPENSSL_free(der);
        X509_free(server_cert);
        if (!ok) {
            SSL_free(c->ssl); SSL_CTX_free(c->ssl_ctx);
            c->ssl = NULL; c->ssl_ctx = NULL;
            close(c->fd); c->fd = -1;
            return -1;
        }
    }
    return 0;
}

int ws_tcp_send(ws_conn_t *c, const void *data, size_t len)
{
    if (c->use_tls && c->ssl)
        return SSL_write(c->ssl, data, len);
    return write(c->fd, data, len);
}

int ws_tcp_recv(ws_conn_t *c, void *buf, size_t len, int timeout_ms)
{
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (c->use_tls && c->ssl)
        return SSL_read(c->ssl, buf, len);
    return read(c->fd, buf, len);
}

int ws_connect(ws_conn_t *c, const char *host, int port, int use_tls, const char *auth_key)
{
    if (ws_tcp_connect(c, host, port, use_tls) != 0)
        return -1;
    c->use_ws = 1;

    uint8_t key[16];
    RAND_bytes(key, sizeof(key));
    char key_b64[256];
    base64_encode(key_b64, key, sizeof(key));

    char req[2048];
    snprintf(req, sizeof(req),
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s%s"
        "\r\n",
        host, key_b64,
        auth_key ? "X-Auth-Key: " : "",
        auth_key ? auth_key : "");
    if (auth_key) {
        size_t hdr_len = strlen(req);
        snprintf(req + hdr_len, sizeof(req) - hdr_len, "\r\n");
    }

    ws_tcp_send(c, req, strlen(req));

    char resp[4096];
    size_t total = 0;
    int m;
    do {
        m = ws_tcp_recv(c, resp + total, sizeof(resp) - total - 1, 10000);
        if (m > 0) total += m;
    } while (total < 4 && m > 0);
    resp[total] = '\0';

    if (strstr(resp, "101") == NULL) {
        ws_close(c);
        return -1;
    }
    return 0;
}

int ws_send_frame(ws_conn_t *c, const void *data, size_t len)
{
    uint8_t hdr[14];
    int hl;
    hdr[0] = 0x81;
    if (len < 126) { hdr[1] = (uint8_t)len; hl = 2; }
    else if (len < 65536) { hdr[1] = 126; hdr[2] = (len >> 8) & 0xFF; hdr[3] = len & 0xFF; hl = 4; }
    else { hdr[1] = 127; for (int i = 0; i < 8; i++) hdr[2 + i] = (len >> (56 - i * 8)) & 0xFF; hl = 10; }
    ws_tcp_send(c, hdr, hl);
    return (int)ws_tcp_send(c, data, len);
}

int ws_recv_frame(ws_conn_t *c, char *buf, size_t buf_size, int timeout_ms)
{
    uint8_t hdr[14];
    int n = ws_tcp_recv(c, hdr, 2, timeout_ms);
    if (n <= 0) return n;

    size_t plen = hdr[1] & 0x7F;
    if (plen == 126) {
        n = ws_tcp_recv(c, hdr + 2, 2, timeout_ms);
        if (n <= 0) return -1;
        plen = ((size_t)hdr[2] << 8) | hdr[3];
    } else if (plen == 127) {
        n = ws_tcp_recv(c, hdr + 2, 8, timeout_ms);
        if (n <= 0) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | hdr[2 + i];
    }

    if (plen >= buf_size) return -1;
    size_t total = 0;
    while (total < plen) {
        n = ws_tcp_recv(c, (uint8_t *)buf + total, plen - total, timeout_ms);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    return (int)total;
}

void ws_close(ws_conn_t *c)
{
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    if (c->ssl_ctx) SSL_CTX_free(c->ssl_ctx);
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->ssl = NULL;
    c->ssl_ctx = NULL;
}
