#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <time.h>
#include "libutils.h"
#include "libstratum.h"
#include "libminer.h"
#include "randomx.h"

static volatile int g_running = 1;
int g_mute = 0;

static void signal_handler(int sig)
{
    (void)sig;
    printf("\n[main] shutting down...\n");
    g_running = 0;
}

static struct termios g_orig_termios;
static int g_raw_mode = 0;

static void enable_raw_mode(void)
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_raw_mode = 1;
}

static void disable_raw_mode(void)
{
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_raw_mode = 0;
    }
}

static int read_key(void)
{
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return c;
    return -1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -o, --url URL        Pool URL (stratum+tcp://, stratum+ssl://, ws://, wss://)\n");
    printf("  -u, --user USER      Wallet address or pool login\n");
    printf("  -p, --pass PASS      Worker password (default: x)\n");
    printf("  -w, --worker NAME    Worker name\n");
    printf("  -t, --threads N      Number of mining threads (default: 1)\n");
    printf("  --jit / --no-jit     RandomX JIT compilation (default: on)\n");
    printf("  --aes / --no-aes     Hardware AES-NI (default: on)\n");
    printf("  --full-mem / --light RandomX full dataset mode (default: on)\n");
    printf("  --large-pages       RandomX large pages (default: off)\n");
    printf("  --randomize-cpu      Randomize CPU load (asymmetric sine + noise)\n");
    printf("  -b, --benchmark      Benchmark mode (no pool, dummy job)\n");
    printf("  -h, --help           Show this help\n");
    printf("\nInteractive commands:\n");
    printf("  h  - Show hashrate\n");
    printf("  r  - Show accepted/rejected shares\n");
    printf("  d  - Show share report\n");
    printf("  c  - Show config (jit, aes, full-mem, large-pages)\n");
    printf("  q  - Quit\n");
    printf("\nExamples:\n");
    printf("  %s -o stratum+tcp://127.0.0.1:3333 -u WALLET -p x\n", prog);
    printf("  %s -o stratum+tcp://pool:port -u WALLET -t 4 -w rig1\n", prog);
    printf("\n");
}

int main(int argc, char *argv[])
{
    char url[512] = {0};
    char user[256] = {0};
    char pass[256] = "x";
    char worker[256] = {0};
    int num_threads = 1;
    int cpu_rand_mode = CPU_RANDOMIZE_OFF;
    bool benchmark = false;

    rx_config.flags = 0;
    rx_config.full_mem = false;
    rx_config.flags |= RANDOMX_FLAG_HARD_AES;
    rx_config.flags |= RANDOMX_FLAG_JIT;
    rx_config.flags |= RANDOMX_FLAG_FULL_MEM;
    rx_config.full_mem = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--url") == 0) {
            if (i + 1 < argc) strncpy(url, argv[++i], sizeof(url) - 1);
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--user") == 0) {
            if (i + 1 < argc) strncpy(user, argv[++i], sizeof(user) - 1);
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--pass") == 0) {
            if (i + 1 < argc) strncpy(pass, argv[++i], sizeof(pass) - 1);
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--worker") == 0) {
            if (i + 1 < argc) strncpy(worker, argv[++i], sizeof(worker) - 1);
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 < argc) num_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--jit") == 0) {
            rx_config.flags |= RANDOMX_FLAG_JIT;
        } else if (strcmp(argv[i], "--no-jit") == 0) {
            rx_config.flags &= ~RANDOMX_FLAG_JIT;
        } else if (strcmp(argv[i], "--aes") == 0) {
            rx_config.flags |= RANDOMX_FLAG_HARD_AES;
        } else if (strcmp(argv[i], "--no-aes") == 0) {
            rx_config.flags &= ~RANDOMX_FLAG_HARD_AES;
        } else if (strcmp(argv[i], "--full-mem") == 0) {
            rx_config.flags |= RANDOMX_FLAG_FULL_MEM;
            rx_config.full_mem = true;
        } else if (strcmp(argv[i], "--light") == 0) {
            rx_config.flags &= ~RANDOMX_FLAG_FULL_MEM;
            rx_config.full_mem = false;
        } else if (strcmp(argv[i], "--large-pages") == 0) {
            rx_config.flags |= RANDOMX_FLAG_LARGE_PAGES;
        } else if (strcmp(argv[i], "--randomize-cpu") == 0) {
            cpu_rand_mode = CPU_RANDOMIZE_SINE;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--benchmark") == 0) {
            benchmark = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!benchmark && (!url[0] || !user[0])) {
        fprintf(stderr, "Error: pool URL and user/wallet are required.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (num_threads < 1) num_threads = 1;
    if (num_threads > 64) num_threads = 64;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    atexit(disable_raw_mode);

    if (benchmark) {
        stratum_ctx_t stratum;
        miner_pool_t miner;

        if ((rx_config.flags & RANDOMX_FLAG_HARD_AES) && !has_hardware_aes()) {
            printf("[main] AES-NI not detected, using software AES\n");
            rx_config.flags &= ~RANDOMX_FLAG_HARD_AES;
        }

        memset(&stratum, 0, sizeof(stratum));
        pthread_mutex_init(&stratum.job_lock, NULL);
        pthread_mutex_lock(&stratum.job_lock);
        strncpy(stratum.current_job.job_id, "benchmark", sizeof(stratum.current_job.job_id) - 1);
        strncpy(stratum.current_job.blob,
            "1010a8cbf9d0069f47179be0e5250a5a0b8a3d1b23327b09a1e4f01e12304567"
            "0000000000000000000000000000000000000000000000000000000000000000",
            sizeof(stratum.current_job.blob) - 1);
        strncpy(stratum.current_job.target, "ffffffffffffffff", sizeof(stratum.current_job.target) - 1);
        strncpy(stratum.current_job.seed_hash,
            "0000000000000000000000000000000000000000000000000000000000000000",
            sizeof(stratum.current_job.seed_hash) - 1);
        stratum.current_job.height = 0;
        stratum.current_job.has_job = true;
        pthread_mutex_unlock(&stratum.job_lock);
        stratum.connected = true;
        stratum.running = true;

        printf("=== XMRMiner v1.0.0 benchmark (%d thread%s, %s) ===\n",
               num_threads, num_threads > 1 ? "s" : "",
               rx_config.full_mem ? "full-mem" : "light");
        printf("[benchmark] use 'h' for hashrate, 'q' to quit\n");

        if (miner_pool_init(&miner, &stratum, num_threads) != 0) {
            fprintf(stderr, "[main] failed to init miner\n");
            return 1;
        }
        miner.cpu_rand_mode = cpu_rand_mode;
        miner_pool_start(&miner);
        enable_raw_mode();

        while (g_running && stratum.connected) {
            int key = read_key();
            if (key == 'h' || key == 'H') {
                uint64_t total = 0;
                for (int i = 0; i < miner.num_threads; i++)
                    total += miner.threads[i].hashes_total;
                printf("\n[main] hashrate report:\n");
                for (int i = 0; i < miner.num_threads; i++)
                    printf("  thread %d: %u H/s (total: %lu)\n", i,
                           miner.threads[i].hash_rate, miner.threads[i].hashes_total);
                printf("  total: %lu hashes\n\n", total);
            } else if (key == 'r' || key == 'R') {
                printf("\n[main] share results: accepted=%lu rejected=%lu\n\n",
                       stratum.shares_accepted, stratum.shares_rejected);
            } else if (key == 'q' || key == 'Q') {
                printf("\n[main] quitting...\n");
                g_running = 0;
            }
            usleep(100000);
        }

        disable_raw_mode();
        miner_pool_stop(&miner);
        miner_pool_wait(&miner);
        pthread_mutex_destroy(&stratum.job_lock);
        miner_pool_cleanup_rx();
        printf("[main] benchmark complete.\n");
        return 0;
    }

    printf("=== XMRMiner v1.0.0 (RandomX, %d thread%s) ===\n", num_threads, num_threads > 1 ? "s" : "");
    printf("pool: %s\n", url);
    printf("user: %s\n", user);
    printf("commands: [h]ashrate [r]esults [d]iffbar [c]onfig [q]uit\n\n");

    while (g_running) {
        if ((rx_config.flags & RANDOMX_FLAG_HARD_AES) && !has_hardware_aes()) {
            printf("[main] AES-NI not detected, using software AES\n");
            rx_config.flags &= ~RANDOMX_FLAG_HARD_AES;
        }

        if (worker[0]) {
            size_t ulen = strlen(user);
            snprintf(user + ulen, sizeof(user) - ulen, ".%s", worker);
        }

        stratum_ctx_t stratum;
        miner_pool_t miner;

        printf("[main] connecting to %s ...\n", url);
        if (stratum_connect(&stratum, url, user, pass) != 0) {
            fprintf(stderr, "[main] connection failed, retrying in 5s...\n");
            sleep(5);
            continue;
        }
        if (stratum_login(&stratum) != 0) {
            fprintf(stderr, "[main] login failed, retrying in 5s...\n");
            stratum_disconnect(&stratum);
            sleep(5);
            continue;
        }
        printf("[main] connected, starting miner...\n");

        if (miner_pool_init(&miner, &stratum, num_threads) != 0) {
            fprintf(stderr, "[main] miner init failed, retrying in 5s...\n");
            stratum_disconnect(&stratum);
            sleep(5);
            continue;
        }

        miner.cpu_rand_mode = cpu_rand_mode;
        miner.rand_mem = false;
        miner_pool_start(&miner);
        enable_raw_mode();

        printf("[main] mining started. Press 'h' hashrate, 'r' results, 'd' diffbar, 'c' config, 'q' quit.\n");

        while (g_running && stratum.connected) {
            int key = read_key();
            if (key == 'h' || key == 'H') {
                uint64_t total = 0;
                for (int i = 0; i < miner.num_threads; i++)
                    total += miner.threads[i].hashes_total;
                printf("\n[main] hashrate report:\n");
                for (int i = 0; i < miner.num_threads; i++)
                    printf("  thread %d: %u H/s (total: %lu)\n", i,
                           miner.threads[i].hash_rate, miner.threads[i].hashes_total);
                printf("  total: %lu hashes\n\n", total);
            } else if (key == 'r' || key == 'R') {
                printf("\n[main] share results: accepted=%lu rejected=%lu\n\n",
                       stratum.shares_accepted, stratum.shares_rejected);
            } else if (key == 'd' || key == 'D') {
                stratum_job_t snap;
                bool has = stratum_get_job(&stratum, &snap);
                printf("\n[main] share report:\n");
                if (has)
                    printf("  pool target: %s (diff=%lu)\n",
                           snap.target, (unsigned long)target_to_diff(snap.target));
                else
                    printf("  pool target: (no job)\n");
                for (int i = 0; i < miner.num_threads; i++)
                    printf("  thread %d: found=%lu submitted=%lu\n", i,
                           miner.threads[i].shares_found,
                           miner.threads[i].shares_submitted);
                printf("\n");
            } else if (key == 'c' || key == 'C') {
                printf("\n[main] config:\n");
                printf("  jit:         %s\n", (rx_config.flags & RANDOMX_FLAG_JIT) ? "on" : "off");
                printf("  aes:         %s\n", (rx_config.flags & RANDOMX_FLAG_HARD_AES) ? "on" : "off");
                printf("  full-mem:    %s\n", rx_config.full_mem ? "on" : "off");
                printf("  large-pages: %s\n", (rx_config.flags & RANDOMX_FLAG_LARGE_PAGES) ? "on" : "off");
                printf("  randomize-cpu: %s\n",
                       miner.cpu_rand_mode == CPU_RANDOMIZE_SINE ? "sine" :
                       miner.cpu_rand_mode == CPU_RANDOMIZE_BROWN ? "brown" :
                       miner.cpu_rand_mode == CPU_RANDOMIZE_BURST ? "burst" : "off");
                printf("  threads:     %d\n\n", miner.num_threads);
            } else if (key == 'q' || key == 'Q') {
                printf("\n[main] quitting...\n");
                g_running = 0;
            }
            usleep(100000);
        }

        disable_raw_mode();
        miner_pool_stop(&miner);
        miner_pool_wait(&miner);
        stratum_disconnect(&stratum);

        if (g_running) {
            printf("[main] disconnected, retrying in 5s...\n");
            sleep(5);
        }
    }

    miner_pool_cleanup_rx();
    printf("[main] exited cleanly.\n");
    return 0;
}
