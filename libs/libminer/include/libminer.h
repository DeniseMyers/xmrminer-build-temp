#ifndef LIBMINER_H
#define LIBMINER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <libstratum.h>
#include <randomx.h>

typedef struct {
    int flags;
    bool full_mem;
} rx_config_t;

extern rx_config_t rx_config;

#define CPU_RANDOMIZE_OFF   0
#define CPU_RANDOMIZE_SINE  1
#define CPU_RANDOMIZE_BROWN 2
#define CPU_RANDOMIZE_BURST 3

typedef struct {
    stratum_ctx_t *stratum;
    int thread_id;
    uint32_t nonce_start;
    uint64_t hashes_total;
    uint32_t hash_rate;
    uint64_t shares_found;
    uint64_t shares_submitted;
    uint64_t seed_gen;
    int cpu_rand_mode;
    bool rand_mem;
    double cpu_phase;
    bool running;
} miner_ctx_t;

typedef struct {
    stratum_ctx_t *stratum;
    int num_threads;
    int cpu_rand_mode;
    bool rand_mem;
    miner_ctx_t *threads;
    pthread_t *thread_handles;
    bool running;
} miner_pool_t;

int  miner_pool_init(miner_pool_t *pool, stratum_ctx_t *stratum, int num_threads);
uint64_t target_to_diff(const char *target_hex);
void miner_pool_start(miner_pool_t *pool);
void miner_pool_stop(miner_pool_t *pool);
void miner_pool_wait(miner_pool_t *pool);
void miner_pool_cleanup_rx(void);

#endif
