/* miner.h — RandomX miner thread pool: per-thread context, pool lifecycle */

#ifndef MINER_H
#define MINER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "stratum.h"

/* CPU randomization modes */
#define CPU_RANDOMIZE_OFF   0
#define CPU_RANDOMIZE_SINE  1
#define CPU_RANDOMIZE_BROWN 2
#define CPU_RANDOMIZE_BURST 3

/* Per-thread mining context: nonce range, stats */
typedef struct {
    stratum_ctx_t *stratum;
    int thread_id;
    uint32_t nonce_start;       /* Starting nonce for this thread */
    uint64_t hashes_total;      /* Total hashes computed */
    uint32_t hash_rate;         /* Recent hash rate (H/s) */
    uint64_t shares_found;      /* Shares meeting network target */
    uint64_t shares_submitted;  /* Shares actually submitted to pool */
    uint64_t seed_gen;          /* Counter incremented when seed changes */
    int cpu_rand_mode;          /* CPU_RANDOMIZE_OFF/SINE/BROWN/BURST */
    bool rand_mem;              /* Randomize memory allocation timing */
    double cpu_phase;           /* Per-thread phase offset for sine wave */
    bool running;
} miner_ctx_t;

/* Pool of miner threads sharing a single stratum connection */
typedef struct {
    stratum_ctx_t *stratum;
    int num_threads;
    int cpu_rand_mode;           /* CPU_RANDOMIZE_OFF/SINE/BROWN/BURST */
    bool rand_mem;               /* Randomize memory allocation timing */
    miner_ctx_t *threads;        /* Array of per-thread contexts */
    pthread_t *thread_handles;   /* Array of thread handles */
    bool running;
} miner_pool_t;

/* Initialize the thread pool with N threads sharing one stratum context */
int  miner_pool_init(miner_pool_t *pool, stratum_ctx_t *stratum, int num_threads);
/* Convert pool target hex to human-readable difficulty */
uint64_t target_to_diff(const char *target_hex);
/* Start all miner threads */
void miner_pool_start(miner_pool_t *pool);
/* Signal all threads to stop */
void miner_pool_stop(miner_pool_t *pool);
/* Wait for all threads to finish, then clean up */
void miner_pool_wait(miner_pool_t *pool);

#endif
