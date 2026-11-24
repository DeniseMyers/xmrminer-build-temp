/* stratum.h — Stratum protocol client: connection, login, job/submit, WebSocket support */

#ifndef STRATUM_H
#define STRATUM_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "config.h"

#if FEATURE_TLS
#include <openssl/ssl.h>
#endif

/* A mining job received from the pool: blob, target, seed hash etc. */
typedef struct {
    char job_id[128];   /* Job identifier from pool */
    char blob[512];     /* Hex-encoded block template blob */
    char target[32];    /* Hex-encoded target (share difficulty) */
    char seed_hash[128];/* Hex-encoded RandomX seed hash */
    uint8_t height;     /* Block height */
    bool has_job;       /* Set to true when a valid job is available */
} stratum_job_t;

/* Low-level connection handle (raw socket, optional TLS/WebSocket state) */
typedef struct {
    int fd;
#if FEATURE_TLS
    SSL *ssl;
    SSL_CTX *ssl_ctx;
    int use_tls;
#endif
#if FEATURE_WS
    int use_ws;
#endif
} conn_t;

/* Full stratum context: connection, credentials, current job, receive thread */
typedef struct {
    conn_t conn;
    char url[512];
    char user[256];
    char pass[256];
    char rpc_id[128];          /* Session RPC ID returned by pool on login */
    stratum_job_t current_job;
    pthread_mutex_t job_lock;  /* Protects current_job and rpc_id */
    pthread_t recv_thread;     /* Background message receive thread */
    bool connected;
    bool running;
    bool nicehash;             /* Pool supports NiceHash nonce layout */
    uint64_t shares_accepted;
    uint64_t shares_rejected;
} stratum_ctx_t;

/* Connect to a stratum pool (supports tcp, ssl, ws, wss) */
int  stratum_connect(stratum_ctx_t *ctx, const char *url, const char *user, const char *pass);
/* Send login request and wait for initial job */
int  stratum_login(stratum_ctx_t *ctx);
/* Submit a share (nonce + result hash for a given job) */
int  stratum_submit(stratum_ctx_t *ctx, const char *job_id, const char *nonce_hex,
                    const char *result_hex);
/* Disconnect and clean up resources */
void stratum_disconnect(stratum_ctx_t *ctx);
/* Copy the current job out (thread-safe); returns true if a job is available */
bool stratum_get_job(stratum_ctx_t *ctx, stratum_job_t *job);
/* Background thread: receive messages, handle jobs/results/errors */
void *stratum_recv_loop(void *arg);

#endif
