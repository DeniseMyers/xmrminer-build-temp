#ifndef LIBSTRATUM_H
#define LIBSTRATUM_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "libws.h"
#include "libjson.h"

typedef struct {
    char job_id[128];
    char blob[512];
    char target[32];
    char seed_hash[128];
    uint8_t height;
    bool has_job;
} stratum_job_t;

typedef struct {
    ws_conn_t conn;
    char url[512];
    char user[256];
    char pass[256];
    char rpc_id[128];
    uint64_t login_rpc_id;
    stratum_job_t current_job;
    pthread_mutex_t job_lock;
    pthread_t recv_thread;
    bool connected;
    bool running;
    bool nicehash;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
} stratum_ctx_t;

int  stratum_connect(stratum_ctx_t *ctx, const char *url, const char *user, const char *pass);
int  stratum_login(stratum_ctx_t *ctx);
int  stratum_submit(stratum_ctx_t *ctx, const char *job_id, const char *nonce_hex, const char *result_hex);
void stratum_disconnect(stratum_ctx_t *ctx);
bool stratum_get_job(stratum_ctx_t *ctx, stratum_job_t *job);
void *stratum_recv_loop(void *arg);

#endif
