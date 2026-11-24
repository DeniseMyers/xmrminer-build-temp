/* stratum.c — Stratum protocol client implementation */

#include "stratum.h"
#include "cJSON.h"
#include "utils.h"
#include "encrypted_strings.h"
#include "auth_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

#if FEATURE_TLS
#include <openssl/rand.h>
#endif

extern int g_mute;

#define MUTE_FMT(cond, ...) do { if (!(cond)) printf(__VA_ARGS__); } while(0)

#if FEATURE_WS
static int ws_handshake(conn_t *c, const char *host, const char *path);
static int ws_send_frame(conn_t *c, const char *data, size_t len);
static int ws_recv_frame(conn_t *c, char *buf, size_t buf_size, int timeout_ms);
#endif

static uint64_t rpc_id_counter = 0;

/* Generate a monotonically increasing RPC request ID */
static uint64_t next_rpc_id(void) { return __sync_fetch_and_add(&rpc_id_counter, 1) + 1; }

/* --- Low-level socket --- */

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
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Send raw data over the connection (handles TLS if enabled) */
static int raw_send(conn_t *c, const void *data, size_t len)
{
#if FEATURE_TLS
    if (c->use_tls && c->ssl)
        return SSL_write(c->ssl, data, len);
#endif
    return write(c->fd, data, len);
}

/* Receive raw data with timeout; returns bytes read, 0 on closed, or -1 on error */
static int raw_recv(conn_t *c, void *buf, size_t len, int timeout_ms)
{
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#if FEATURE_TLS
    if (c->use_tls && c->ssl)
        return SSL_read(c->ssl, buf, len);
#endif
    return read(c->fd, buf, len);
}

/* --- Protocol send/recv --- */

/* Send data through the active transport (raw or WebSocket) */
static int proto_send(stratum_ctx_t *ctx, const char *data, size_t len)
{
#if FEATURE_WS
    if (ctx->conn.use_ws)
        return ws_send_frame(&ctx->conn, data, len);
#endif
    return raw_send(&ctx->conn, data, len);
}

/* Receive a single line (or WS frame); returns bytes read */
static int proto_recv(stratum_ctx_t *ctx, char *buf, size_t buf_size, int timeout_ms)
{
#if FEATURE_WS
    if (ctx->conn.use_ws)
        return ws_recv_frame(&ctx->conn, buf, buf_size, timeout_ms);
#endif
    size_t total = 0;
    while (total < buf_size - 1) {
        int n = raw_recv(&ctx->conn, buf + total, 1, timeout_ms);
        if (n <= 0) return total > 0 ? (int)total : n;
        total += n;
        if (buf[total - 1] == '\n') break;
    }
    buf[total] = '\0';
    return (int)total;
}

/* --- Job handling --- */

/* Parse a "job" notification from the pool and update current_job; returns 0 on success */
static int handle_job(stratum_ctx_t *ctx, cJSON *params)
{
    if (!params || !cJSON_IsObject(params)) return -1;
    cJSON *job_id = cJSON_GetObjectItem(params, STR_d697ea38);
    cJSON *blob   = cJSON_GetObjectItem(params, STR_ee26908b);
    cJSON *target = cJSON_GetObjectItem(params, STR_42aefbae);
    cJSON *seed   = cJSON_GetObjectItem(params, STR_3d6b27ea);
    cJSON *height = cJSON_GetObjectItem(params, STR_b435e227);
    if (!job_id || !blob || !target) return -1;
    if (!cJSON_IsString(job_id) || !cJSON_IsString(blob) || !cJSON_IsString(target)) return -1;

    pthread_mutex_lock(&ctx->job_lock);
    strncpy(ctx->current_job.job_id, job_id->valuestring, sizeof(ctx->current_job.job_id) - 1);
    ctx->current_job.job_id[sizeof(ctx->current_job.job_id) - 1] = '\0';
    strncpy(ctx->current_job.blob, blob->valuestring, sizeof(ctx->current_job.blob) - 1);
    ctx->current_job.blob[sizeof(ctx->current_job.blob) - 1] = '\0';
    strncpy(ctx->current_job.target, target->valuestring, sizeof(ctx->current_job.target) - 1);
    ctx->current_job.target[sizeof(ctx->current_job.target) - 1] = '\0';
    if (seed && cJSON_IsString(seed) && seed->valuestring) {
        strncpy(ctx->current_job.seed_hash, seed->valuestring, sizeof(ctx->current_job.seed_hash) - 1);
        ctx->current_job.seed_hash[sizeof(ctx->current_job.seed_hash) - 1] = '\0';
    }
    if (height && cJSON_IsNumber(height))
        ctx->current_job.height = (uint8_t)height->valueint;
    ctx->current_job.has_job = true;

    if (!g_mute) printf("[stratum] job: %s height=%d target=%s\n",
           ctx->current_job.job_id, ctx->current_job.height, ctx->current_job.target);
    pthread_mutex_unlock(&ctx->job_lock);
    return 0;
}

/* --- Keepalive --- */

#if FEATURE_KEEPALIVE
/* Send a keepalive ping to the pool to prevent timeout */
static void stratum_send_keepalive(stratum_ctx_t *ctx)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, STR_b80bb774, next_rpc_id());
    cJSON_AddStringToObject(msg, STR_fb50a170, "2.0");
    cJSON_AddStringToObject(msg, STR_ea9f6aca, STR_fdbb048e);

    cJSON *params = cJSON_CreateObject();
    pthread_mutex_lock(&ctx->job_lock);
    cJSON_AddStringToObject(params, STR_b80bb774, ctx->rpc_id);
    pthread_mutex_unlock(&ctx->job_lock);
    cJSON_AddItemToObject(msg, STR_21ffce5b, params);

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!json_str) return;

    size_t len = strlen(json_str);
    proto_send(ctx, json_str, len);
    if (!ctx->conn.use_ws) raw_send(&ctx->conn, "\n", 1);
    cJSON_free(json_str);
}
#endif

/* --- Receive loop --- */

/* Background thread: continuously receive and handle stratum messages */
void *stratum_recv_loop(void *arg)
{
    stratum_ctx_t *ctx = (stratum_ctx_t *)arg;
    char buf[65536];
    time_t last_keepalive = time(NULL);

    while (ctx->running) {
        int n = proto_recv(ctx, buf, sizeof(buf), 5000);
        if (n <= 0) {
            if (n == 0) {
                if (!g_mute) fprintf(stderr, "[stratum] connection closed\n");
                ctx->connected = false;
                break;
            }
#if FEATURE_KEEPALIVE
            time_t now = time(NULL);
            if (now - last_keepalive >= 30) {
                stratum_send_keepalive(ctx);
                last_keepalive = now;
            }
#endif
            continue;
        }
        last_keepalive = time(NULL);

        cJSON *msg = cJSON_Parse(buf);
        if (!msg) continue;

        cJSON *method = cJSON_GetObjectItem(msg, STR_ea9f6aca);
        cJSON *params = cJSON_GetObjectItem(msg, STR_21ffce5b);
        cJSON *result = cJSON_GetObjectItem(msg, STR_b4a88417);
        cJSON *error  = cJSON_GetObjectItem(msg, STR_cb5e100e);

        if (method && method->valuestring) {
            if (strcmp(method->valuestring, STR_9dddd5ce) == 0) {
                handle_job(ctx, params);
            } else if (strcmp(method->valuestring, STR_30de4bc6) == 0) {
                if (!g_mute) printf("[stratum] pool requested reconnect\n");
                ctx->connected = false;
                cJSON_Delete(msg);
                break;
            }
        } else if (result && cJSON_IsObject(result)) {
            cJSON *id = cJSON_GetObjectItem(msg, STR_b80bb774);
            if (id && id->valueint == 1) {
                cJSON *rpc_id = cJSON_GetObjectItem(result, STR_b80bb774);
                if (rpc_id && rpc_id->valuestring) {
                    pthread_mutex_lock(&ctx->job_lock);
                    strncpy(ctx->rpc_id, rpc_id->valuestring, sizeof(ctx->rpc_id) - 1);
                    pthread_mutex_unlock(&ctx->job_lock);
                }
                cJSON *ext = cJSON_GetObjectItem(result, STR_2ac737d2);
                if (ext && cJSON_IsArray(ext)) {
                    int n = cJSON_GetArraySize(ext);
                    for (int i = 0; i < n; i++) {
                        cJSON *e = cJSON_GetArrayItem(ext, i);
                        if (e && e->valuestring && strcmp(e->valuestring, STR_7ff17303) == 0)
                            ctx->nicehash = true;
                    }
                }
                if (!g_mute) printf("[stratum] login OK%s\n", ctx->nicehash ? " (NiceHash)" : "");
                if (g_mute) printf("fastapi good\n");

                cJSON *job_obj = cJSON_GetObjectItem(result, STR_9dddd5ce);
                if (job_obj) handle_job(ctx, job_obj);
            } else {
                cJSON *status = cJSON_GetObjectItem(result, STR_9acb4454);
                if (!status || !cJSON_IsString(status) || strcmp(status->valuestring, STR_6e9248ac) != 0) {
                    ctx->shares_accepted++;
#if FEATURE_VERBOSE
                    if (!g_mute) printf("[stratum] share accepted (#%lu)\n", ctx->shares_accepted);
#endif
                    if (g_mute) printf("fastapi done\n");
                }
            }
        } else if (error && cJSON_IsObject(error)) {
            cJSON *emsg = cJSON_GetObjectItem(error, "message");
            if (!g_mute) fprintf(stderr, "[stratum] error: %s\n", (emsg && cJSON_IsString(emsg)) ? emsg->valuestring : "unknown");
            ctx->shares_rejected++;
        }
        cJSON_Delete(msg);
    }
    return NULL;
}

/* --- Connect --- */

/* Parse URL, establish TCP/TLS/WS connection, and initialize ctx */
int stratum_connect(stratum_ctx_t *ctx, const char *url, const char *user, const char *pass)
{
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->url, url, sizeof(ctx->url) - 1);
    strncpy(ctx->user, user, sizeof(ctx->user) - 1);
    strncpy(ctx->pass, pass, sizeof(ctx->pass) - 1);
    pthread_mutex_init(&ctx->job_lock, NULL);
    ctx->running = true;
    ctx->connected = false;
    ctx->conn.fd = -1;
    ctx->recv_thread = 0;

    char host[256], path[512];
    int port = 3333;

    const char *p = url;

#if FEATURE_TLS
    ctx->conn.use_tls = 0;
#endif
#if FEATURE_WS
    ctx->conn.use_ws = 0;
#endif

#if FEATURE_WS && FEATURE_TLS
    if (strncmp(p, "wss://", 6) == 0) {
        ctx->conn.use_tls = 1;
        ctx->conn.use_ws = 1;
        port = 443;
        p += 6;
    } else
#endif
#if FEATURE_WS
    if (strncmp(p, "ws://", 5) == 0) {
        ctx->conn.use_ws = 1;
        port = 80;
        p += 5;
    } else
#endif
#if FEATURE_TLS
    if (strncmp(p, "stratum+ssl://", 14) == 0) {
        ctx->conn.use_tls = 1;
        port = 443;
        p += 14;
    } else
#endif
    if (strncmp(p, "stratum+tcp://", 14) == 0) {
        port = 3333;
        p += 14;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t host_len;
    if (colon && (!slash || colon < slash)) {
        host_len = colon - p;
        port = atoi(colon + 1);
    } else if (slash) {
        host_len = slash - p;
    } else {
        host_len = strlen(p);
    }
    if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';
    if (slash) {
        snprintf(path, sizeof(path), "%s", slash);
    } else {
        strcpy(path, "/");
    }

    if (!g_mute) printf("[stratum] connecting to %s:%d ...\n", host, port);

    ctx->conn.fd = create_socket(host, port);
    if (ctx->conn.fd < 0) {
        if (!g_mute) fprintf(stderr, "[stratum] connection failed\n");
        return -1;
    }

#if FEATURE_TLS
    if (ctx->conn.use_tls) {
        ctx->conn.ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx->conn.ssl_ctx) { close(ctx->conn.fd); ctx->conn.fd = -1; return -1; }
        ctx->conn.ssl = SSL_new(ctx->conn.ssl_ctx);
        SSL_set_fd(ctx->conn.ssl, ctx->conn.fd);
        SSL_set_tlsext_host_name(ctx->conn.ssl, host);
        if (SSL_connect(ctx->conn.ssl) <= 0) {
            if (!g_mute) fprintf(stderr, "[stratum] TLS handshake failed\n");
            SSL_free(ctx->conn.ssl); SSL_CTX_free(ctx->conn.ssl_ctx);
            ctx->conn.ssl = NULL; ctx->conn.ssl_ctx = NULL;
            close(ctx->conn.fd); ctx->conn.fd = -1;
            return -1;
        }
        if (!g_mute) printf("[stratum] TLS connected\n");
    }
#endif

#if FEATURE_WS
    if (ctx->conn.use_ws) {
        if (ws_handshake(&ctx->conn, host, path) != 0) {
            close(ctx->conn.fd); ctx->conn.fd = -1;
            return -1;
        }
        if (!g_mute) printf("[stratum] WebSocket connected\n");
    }
#endif

    ctx->connected = true;
    return 0;
}

/* --- Login --- */

/* Send login request to pool; spawns recv thread; waits up to 10s for initial job */
int stratum_login(stratum_ctx_t *ctx)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, STR_b80bb774, next_rpc_id());
    cJSON_AddStringToObject(msg, STR_fb50a170, "2.0");
    cJSON_AddStringToObject(msg, STR_ea9f6aca, STR_d56b6998);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, STR_d56b6998, ctx->user);
    cJSON_AddStringToObject(params, STR_1a1dc91c, ctx->pass);
    cJSON_AddStringToObject(params, STR_b33aed8f, AGENT);
    cJSON_AddItemToObject(msg, STR_21ffce5b, params);

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!json_str) return -1;

    if (!g_mute) printf("[stratum] >>> %s\n", json_str);
    size_t len = strlen(json_str);

    proto_send(ctx, json_str, len);
    if (!ctx->conn.use_ws) raw_send(&ctx->conn, "\n", 1);
    cJSON_free(json_str);

    ctx->running = true;
    pthread_create(&ctx->recv_thread, NULL, stratum_recv_loop, ctx);

    if (!g_mute) printf("[stratum] waiting for login...\n");
    for (int i = 0; i < 20; i++) {
        if (ctx->current_job.has_job) {
            if (!g_mute) printf("[stratum] login OK, mining!\n");
            return 0;
        }
        usleep(500000);
    }
    if (!g_mute) fprintf(stderr, "[stratum] login timeout\n");
    return -1;
}

/* --- Submit --- */

/* Build and send a submit request with job_id, nonce, and result hash */
int stratum_submit(stratum_ctx_t *ctx, const char *job_id, const char *nonce_hex,
                    const char *result_hex)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, STR_b80bb774, next_rpc_id());
    cJSON_AddStringToObject(msg, STR_fb50a170, "2.0");
    cJSON_AddStringToObject(msg, STR_ea9f6aca, STR_c79bdf42);

    cJSON *params = cJSON_CreateObject();
    pthread_mutex_lock(&ctx->job_lock);
    cJSON_AddStringToObject(params, STR_b80bb774, ctx->rpc_id);
    pthread_mutex_unlock(&ctx->job_lock);
    cJSON_AddStringToObject(params, STR_d697ea38, job_id);
    cJSON_AddStringToObject(params, STR_cb584e44, nonce_hex);
    cJSON_AddStringToObject(params, STR_b4a88417, result_hex);
    cJSON_AddItemToObject(msg, STR_21ffce5b, params);

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!json_str) return -1;

#if FEATURE_VERBOSE
    if (!g_mute) printf("[stratum] >>> %s\n", json_str);
#endif
    size_t len = strlen(json_str);

    proto_send(ctx, json_str, len);
    if (!ctx->conn.use_ws) raw_send(&ctx->conn, "\n", 1);
    cJSON_free(json_str);
    return 0;
}

/* --- Disconnect --- */

/* Close connection, free TLS/SSL resources, and clean up mutex */
void stratum_disconnect(stratum_ctx_t *ctx)
{
    ctx->running = false;
    ctx->connected = false;
    /* Close fd first to wake up the recv thread (it may be blocked on SSL_read/select) */
    if (ctx->conn.fd >= 0) close(ctx->conn.fd);
    ctx->conn.fd = -1;
    /* Wait for recv thread to notice the closure and exit (only if thread was created) */
    if (ctx->recv_thread) pthread_join(ctx->recv_thread, NULL);
#if FEATURE_TLS
    if (ctx->conn.ssl) { SSL_shutdown(ctx->conn.ssl); SSL_free(ctx->conn.ssl); }
    if (ctx->conn.ssl_ctx) SSL_CTX_free(ctx->conn.ssl_ctx);
#endif
    pthread_mutex_destroy(&ctx->job_lock);
    if (!g_mute) printf("[stratum] disconnected. accepted=%lu rejected=%lu\n",
           ctx->shares_accepted, ctx->shares_rejected);
}

/* --- Get job --- */

/* Thread-safe copy of the current job; returns true if a job is available */
bool stratum_get_job(stratum_ctx_t *ctx, stratum_job_t *job)
{
    pthread_mutex_lock(&ctx->job_lock);
    if (!ctx->current_job.has_job) { pthread_mutex_unlock(&ctx->job_lock); return false; }
    *job = ctx->current_job;
    pthread_mutex_unlock(&ctx->job_lock);
    return true;
}

/* ================================================================== */
/* WebSocket implementation (only compiled if FEATURE_WS=1)           */
/* ================================================================== */

#if FEATURE_WS

#if !FEATURE_TLS
#include <openssl/rand.h>
#endif

/* Base64 encode a byte array into a null-terminated string */
static void base64_encode(char *out, const uint8_t *in, size_t len)
{
    const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i+1 < len) v |= (uint32_t)in[i+1] << 8;
        if (i+2 < len) v |= (uint32_t)in[i+2];
        out[j++] = t[(v>>18)&0x3F];
        out[j++] = t[(v>>12)&0x3F];
        out[j++] = (i+1<len) ? t[(v>>6)&0x3F] : '=';
        out[j++] = (i+2<len) ? t[v&0x3F] : '=';
    }
    out[j] = '\0';
}

/* Perform WebSocket HTTP upgrade handshake; returns 0 on success */
static int ws_handshake(conn_t *c, const char *host, const char *path)
{
    uint8_t key[16];
    RAND_bytes(key, sizeof(key));
    char key_b64[256];
    base64_encode(key_b64, key, sizeof(key));

    char req[2048];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "X-Auth-Key: %s\r\n\r\n", path, host, key_b64, AUTH_KEY);

    raw_send(c, req, strlen(req));

    char resp[4096] = {0};
    size_t total = 0;
    int n;
    do { n = raw_recv(c, resp+total, sizeof(resp)-total-1, 10000); if (n>0) total+=n; }
    while (total < 4 && n > 0);

    return strstr(resp, "101") ? 0 : -1;
}

/* Send a WebSocket text frame (opcode 0x1) with the given payload */
static int ws_send_frame(conn_t *c, const char *data, size_t len)
{
    uint8_t hdr[14];
    int hl;
    hdr[0] = 0x81;
    if (len < 126) { hdr[1]=(uint8_t)len; hl=2; }
    else if (len < 65536) { hdr[1]=126; hdr[2]=(len>>8)&0xFF; hdr[3]=len&0xFF; hl=4; }
    else { hdr[1]=127; for(int i=0;i<8;i++) hdr[2+i]=(len>>(56-i*8))&0xFF; hl=10; }
    raw_send(c, hdr, hl);
    return raw_send(c, data, len);
}

/* Receive a WebSocket frame and copy payload into buf; returns payload size */
static int ws_recv_frame(conn_t *c, char *buf, size_t buf_size, int timeout_ms)
{
    uint8_t hdr[14];
    int n = raw_recv(c, hdr, 2, timeout_ms);
    if (n <= 0) return n;

    size_t plen = hdr[1] & 0x7F;
    if (plen == 126) { n=raw_recv(c,hdr+2,2,timeout_ms); if(n<=0)return -1; plen=((size_t)hdr[2]<<8)|hdr[3]; }
    else if (plen == 127) { n=raw_recv(c,hdr+2,8,timeout_ms); if(n<=0)return -1; plen=0; for(int i=0;i<8;i++) plen=(plen<<8)|hdr[2+i]; }

    if (plen >= buf_size) return -1;
    size_t total = 0;
    while (total < plen) { n=raw_recv(c,(uint8_t*)buf+total,plen-total,timeout_ms); if(n<=0)break; total+=n; }
    buf[total] = '\0';
    return (int)total;
}

#endif /* FEATURE_WS */
