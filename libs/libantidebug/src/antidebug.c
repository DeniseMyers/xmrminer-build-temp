#include "libantidebug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <linux/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <elf.h>
#include <time.h>

static volatile sig_atomic_t g_monitor_running = 1;

static uint32_t g_text_checksum = 0;
static size_t g_text_start = 0;
static size_t g_text_size = 0;

static uint32_t calc_checksum(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0x12345678;
    for (size_t i = 0; i < len; i++) {
        sum ^= p[i] << (i % 24);
        sum = (sum << 7) | (sum >> 25);
    }
    return sum;
}

void antidebug_init(void)
{
    FILE *fp = fopen("/proc/self/exe", "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = malloc(fsize);
    if (buf) {
        fread(buf, 1, fsize, fp);
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
        Elf64_Shdr *shdr = (Elf64_Shdr *)(buf + ehdr->e_shoff);
        char *shstrtab = (char *)(buf + shdr[ehdr->e_shstrndx].sh_offset);
        for (int i = 0; i < ehdr->e_shnum; i++) {
            if (strcmp(shstrtab + shdr[i].sh_name, ".text") == 0) {
                g_text_start = shdr[i].sh_offset;
                g_text_size = shdr[i].sh_size;
                g_text_checksum = calc_checksum(buf + g_text_start, g_text_size);
                break;
            }
        }
        free(buf);
    }
    fclose(fp);
}

static int verify_checksum(void)
{
    if (!g_text_size) return 1;
    FILE *fp = fopen("/proc/self/exe", "rb");
    if (!fp) return 1;
    fseek(fp, g_text_start, SEEK_SET);
    uint8_t *buf = malloc(g_text_size);
    if (!buf) { fclose(fp); return 1; }
    fread(buf, 1, g_text_size, fp);
    fclose(fp);
    uint32_t current = calc_checksum(buf, g_text_size);
    free(buf);
    return current == g_text_checksum;
}

static void *monitor_thread(void *arg)
{
    (void)arg;
    int check_count = 0;
    while (g_monitor_running) {
        FILE *fp = fopen("/proc/self/status", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "TracerPid:", 10) == 0) {
                    int pid = atoi(line + 10);
                    if (pid != 0) {
                        fclose(fp);
                        _exit(1);
                    }
                    break;
                }
            }
            fclose(fp);
        }
        if (++check_count % 5 == 0) {
            if (!verify_checksum())
                _exit(1);
        }
        sleep(1);
    }
    return NULL;
}

void antidebug_start_monitor(void)
{
    g_monitor_running = 1;
    pthread_t tid;
    pthread_create(&tid, NULL, monitor_thread, NULL);
    pthread_detach(tid);
}

void antidebug_stop_monitor(void)
{
    g_monitor_running = 0;
}

void process_disguise(void)
{
    prctl(PR_SET_NAME, "chromium", 0, 0, 0);
}

void process_set_low_priority(void)
{
    setpriority(PRIO_PROCESS, 0, 19);
    syscall(SYS_ioprio_set, 1, 0, (3 << 8) | 0);
}
