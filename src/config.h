/* config.h — compile-time feature flags and runtime RandomX config */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/*
 * XMRMiner Feature Flags
 * Toggle features at compile time:
 *   make FEATURE_TLS=1 FEATURE_WS=0
 *   make FEATURES="-DFEATURE_TLS -DFEATURE_FULL_MEM"
 */

/* --- Network Features --- */
#ifndef FEATURE_TLS
#define FEATURE_TLS         1   /* OpenSSL TLS/SSL support (stratum+ssl://, wss://) */
#endif

#ifndef FEATURE_WS
#define FEATURE_WS          1   /* WebSocket transport (ws://, wss://) */
#endif

/* --- RandomX Features --- */
#ifndef FEATURE_FULL_MEM
#define FEATURE_FULL_MEM    1   /* Full dataset mode (~2GB RAM, 2-3x faster) */
#endif

#ifndef FEATURE_JIT
#define FEATURE_JIT         1   /* JIT compilation (faster, ~10-20% boost) */
#endif

#ifndef FEATURE_HARD_AES
#define FEATURE_HARD_AES    1   /* Hardware AES-NI (CPU must support it) */
#endif

#ifndef FEATURE_LARGE_PAGES
#define FEATURE_LARGE_PAGES 0   /* Large pages (requires root/permissions) */
#endif

/* --- Output Features --- */
#ifndef FEATURE_VERBOSE
#define FEATURE_VERBOSE     1   /* Verbose pool messages */
#endif

#ifndef FEATURE_COLORS
#define FEATURE_COLORS      1   /* Colored terminal output */
#endif

/* --- Mining Features --- */
#ifndef FEATURE_KEEPALIVE
#define FEATURE_KEEPALIVE   1   /* Send keepalived pings to pool */
#endif

/* --- Runtime RandomX Config (override via CLI) --- */

/* Runtime RandomX configuration: holds flags bitmask and full_mem toggle */
typedef struct {
    int flags;       /* RandomX flags bitmask (RANDOMX_FLAG_*) */
    bool full_mem;   /* Use full dataset (~2GB) vs light mode */
} rx_config_t;

extern rx_config_t rx_config;

/* --- Build Info --- */
#define VERSION "1.0.0"
#define AGENT   "xmrminer/" VERSION

#endif
