# libminer

RandomX mining engine — thread pool, dataset init, nonce search.

## API

```c
int  miner_pool_init(miner_pool_t *pool, stratum_ctx_t *stratum, int num_threads);
void miner_pool_start(miner_pool_t *pool);
void miner_pool_stop(miner_pool_t *pool);
void miner_pool_wait(miner_pool_t *pool);
uint64_t target_to_diff(const char *target_hex);
```

Configuration via `extern rx_config_t rx_config`:
- `rx_config.flags` — RandomX flags (RANDOMX_FLAG_JIT, RANDOMX_FLAG_HARD_AES, etc.)
- `rx_config.full_mem` — true = full dataset, false = light mode

CPU randomization modes: `CPU_RANDOMIZE_OFF`, `CPU_RANDOMIZE_SINE`, `CPU_RANDOMIZE_BROWN`, `CPU_RANDOMIZE_BURST`.

## Dependencies

- libstratum (job data types)
- libutils (hex conversion)
- RandomX (`lib/randomx/build/librandomx.a`)

## Build

```
make CC=gcc CFLAGS="-O1 -Wall -Wextra" \
  FEATURE_FULL_MEM=1 FEATURE_JIT=1 FEATURE_HARD_AES=1 FEATURE_LARGE_PAGES=0
```
