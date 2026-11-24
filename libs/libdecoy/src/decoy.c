#include "libdecoy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <openssl/rand.h>

static volatile int g_decoy_running = 0;
static pthread_t g_decoy_tid;
static pthread_t g_noise_tid;
static pthread_t g_db_noise_tid;
static pthread_t g_port_noise_tid;

typedef struct {
    const char *host;
    int port;
} decoy_target_t;

static const decoy_target_t targets[] = {
    {"www.google.com",      80},
    {"www.google.com",      443},
    {"www.amazon.com",      80},
    {"www.amazon.com",      443},
    {"www.facebook.com",    80},
    {"www.facebook.com",    443},
    {"www.youtube.com",     80},
    {"www.youtube.com",     443},
    {"www.twitter.com",     80},
    {"www.twitter.com",     443},
    {"www.reddit.com",      80},
    {"www.reddit.com",      443},
    {"www.wikipedia.org",   80},
    {"www.wikipedia.org",   443},
    {"www.microsoft.com",   80},
    {"www.microsoft.com",   443},
    {"www.apple.com",       80},
    {"www.apple.com",       443},
    {"www.github.com",      80},
    {"www.github.com",      443},
    {"www.stackoverflow.com",80},
    {"www.netflix.com",     80},
    {"www.netflix.com",     443},
    {"www.twitch.tv",       80},
    {"www.twitch.tv",       443},
    {"www.yahoo.com",       80},
    {"www.bing.com",        80},
    {"www.cnn.com",         80},
    {"www.bbc.com",         80},
    {"www.nytimes.com",     80},
    {"www.reuters.com",     80},
    {"www.baidu.com",       80},
    {"www.aliexpress.com",  80},
    {"www.ebay.com",        80},
    {"www.ebay.com",        443},
    {"www.walmart.com",     80},
    {"www.spotify.com",     80},
    {"www.spotify.com",     443},
    {"www.medium.com",      80},
    {"www.imdb.com",        80},
    {"www.pinterest.com",   80},
    {"www.flickr.com",      80},
    {"www.dropbox.com",     80},
    {"www.dropbox.com",     443},
    {"www.slack.com",       80},
    {"www.discord.com",     80},
    {"www.discord.com",     443},
    {"www.zoom.us",         80},
    {"www.heroku.com",      80},
    {"www.digitalocean.com",80},
    {"www.vultr.com",       80},
    {"www.linode.com",      80},
};

#define NUM_TARGETS (sizeof(targets) / sizeof(targets[0]))

static const char *g_user_agents[] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_5) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Safari/605.1.15",
    "Mozilla/5.0 (X11; Linux x86_64; rv:126.0) Gecko/20100101 Firefox/126.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 Edg/125.0.0.0",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (iPad; CPU OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_5) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36",
};
#define NUM_UA (sizeof(g_user_agents) / sizeof(g_user_agents[0]))

static char *g_paths[] = {
    "/", "/index.html", "/about", "/contact", "/blog",
    "/products", "/services", "/pricing", "/faq", "/help",
    "/support", "/login", "/register", "/dashboard", "/settings",
    "/profile", "/api/v1/status", "/api/v2/users",
    "/static/js/app.js", "/static/css/style.css",
    "/images/logo.png", "/favicon.ico", "/robots.txt",
    "/sitemap.xml", "/search?q=hello+world",
    "/category/electronics", "/item/12345", "/cart",
    "/checkout", "/order/confirm",
};
#define NUM_PATHS (sizeof(g_paths) / sizeof(g_paths[0]))

static int decoy_connect(const char *host, int port) {
    struct addrinfo hints, *res;
    char port_str[16];
    int fd = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return -1;
    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }
    struct timeval tv = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void decoy_http_request(const decoy_target_t *t) {
    int fd = decoy_connect(t->host, t->port);
    if (fd < 0) return;
    unsigned int pidx, uaidx;
    RAND_bytes((unsigned char *)&pidx, sizeof(pidx));
    RAND_bytes((unsigned char *)&uaidx, sizeof(uaidx));
    pidx %= NUM_PATHS;
    uaidx %= NUM_UA;
    char req[1024];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-US,en;q=0.5\r\n"
        "Accept-Encoding: gzip, deflate\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        g_paths[pidx], t->host, g_user_agents[uaidx]);
    send(fd, req, strlen(req), 0);
    char buf[2048];
    recv(fd, buf, sizeof(buf), 0);
    close(fd);
}

static void *noise_thread(void *arg) {
    (void)arg;
    const char *noise_hosts[] = {
        "www.google.com", "www.amazon.com", "www.facebook.com",
        "www.youtube.com", "www.twitter.com", "www.reddit.com",
        "www.wikipedia.org", "www.microsoft.com", "www.apple.com",
        "www.github.com", "www.netflix.com", "www.twitch.tv",
        "www.yahoo.com", "www.bing.com", "www.cnn.com",
        "www.bbc.com", "www.nytimes.com", "www.ebay.com",
        "www.spotify.com", "www.medium.com", "www.imdb.com",
        "www.pinterest.com", "www.flickr.com", "www.dropbox.com",
        "www.slack.com", "www.discord.com", "www.zoom.us",
    };
    #define NUM_NOISE (sizeof(noise_hosts) / sizeof(noise_hosts[0]))
    int keep_fds[32];
    int keep_count = 0;
    memset(keep_fds, -1, sizeof(keep_fds));
    int port_pool[] = {80, 443, 80, 443, 80, 443, 80, 443,
                       8080, 8443, 3000, 5000, 9090,
                       22, 25, 3306, 5432, 6379, 27017};
    #define NUM_PORTS (sizeof(port_pool) / sizeof(port_pool[0]))
    while (g_decoy_running) {
        while (keep_count < 15 && g_decoy_running) {
            unsigned int hidx, pidx;
            RAND_bytes((unsigned char *)&hidx, sizeof(hidx));
            RAND_bytes((unsigned char *)&pidx, sizeof(pidx));
            hidx %= NUM_NOISE;
            pidx %= NUM_PORTS;
            int fd = decoy_connect(noise_hosts[hidx], port_pool[pidx]);
            if (fd >= 0) {
                keep_fds[keep_count++] = fd;
                const char *req = "HEAD / HTTP/1.1\r\nHost: ";
                char full[512];
                snprintf(full, sizeof(full), "%s%s\r\nConnection: keep-alive\r\n\r\n", req, noise_hosts[hidx]);
                send(fd, full, strlen(full), 0);
            }
        }
        unsigned int drop;
        RAND_bytes((unsigned char *)&drop, sizeof(drop));
        drop = 1 + (drop % 3);
        for (unsigned int i = 0; i < drop && keep_count > 10; i++) {
            unsigned int didx;
            RAND_bytes((unsigned char *)&didx, sizeof(didx));
            didx %= keep_count;
            close(keep_fds[didx]);
            keep_fds[didx] = keep_fds[--keep_count];
            keep_fds[keep_count] = -1;
        }
        unsigned int delay;
        RAND_bytes((unsigned char *)&delay, sizeof(delay));
        delay = 5000 + (delay % 10000);
        usleep(delay * 1000);
    }
    for (int i = 0; i < keep_count; i++) {
        if (keep_fds[i] >= 0) close(keep_fds[i]);
    }
    return NULL;
}

static void *db_noise_thread(void *arg) {
    (void)arg;
    const char *db_hosts[] = {
        "127.0.0.1", "::1", "localhost",
        "db", "database", "mongo", "mysql", "postgres", "redis",
        "elastic", "couchdb", "cassandra", "rabbitmq", "kafka",
        "minio", "influxdb", "timescaledb", "mariadb", "memcached",
        "neo4j", "mssql", "oracle", "zookeeper", "solr",
        "etcd", "consul", "vault", "nomad", "portainer",
        "kibana", "grafana", "jenkins", "gitlab", "gitea",
        "sonarqube", "nexus", "harbor", "registry", "docker",
        "k8s", "kubernetes", "apiserver", "kubelet", "etcd-0",
        "redis-master", "redis-slave", "mongo-0", "mongo-1",
        "postgres-primary", "postgres-replica", "mysql-primary",
        "kafka-0", "kafka-1", "zookeeper-0", "rabbitmq",
    };
    #define NUM_DB_HOSTS (sizeof(db_hosts) / sizeof(db_hosts[0]))
    int db_ports[] = {
        27017, 27018, 27019,
        3306, 3307, 33060,
        5432, 5433,
        6379, 6380,
        9200, 9300,
        9042, 9160,
        5672, 5671,
        9092, 9093, 9094,
        5984, 5986,
        11211,
        8081, 8082, 8083,
        8086,
        8529,
        7474, 7687,
        1433, 1434,
        1521,
        2181,
        8983, 8984,
        7001, 7002,
        9100,
        2375, 2376,
        6443,
        10250, 10255,
        2379, 2380,
        8500, 8600,
        8200, 8201,
        4646, 4647, 4648,
        9000, 9001,
        5601,
        16686,
        9411,
        4317, 4318,
        24224,
        5044,
        8080, 8443,
        9990,
        8009,
    };
    #define NUM_DB_PORTS (sizeof(db_ports) / sizeof(db_ports[0]))
    int db_fds[40];
    int db_count = 0;
    memset(db_fds, -1, sizeof(db_fds));
    while (g_decoy_running) {
        while (db_count < 15 && g_decoy_running) {
            unsigned int hidx, pidx;
            RAND_bytes((unsigned char *)&hidx, sizeof(hidx));
            RAND_bytes((unsigned char *)&pidx, sizeof(pidx));
            hidx %= NUM_DB_HOSTS;
            pidx %= NUM_DB_PORTS;
            int fd = decoy_connect(db_hosts[hidx], db_ports[pidx]);
            if (fd >= 0) {
                db_fds[db_count++] = fd;
            }
            usleep(100000);
        }
        unsigned int drop;
        RAND_bytes((unsigned char *)&drop, sizeof(drop));
        drop = 1 + (drop % 3);
        for (unsigned int i = 0; i < drop && db_count > 10; i++) {
            unsigned int didx;
            RAND_bytes((unsigned char *)&didx, sizeof(didx));
            didx %= db_count;
            close(db_fds[didx]);
            db_fds[didx] = db_fds[--db_count];
            db_fds[db_count] = -1;
        }
        unsigned int delay;
        RAND_bytes((unsigned char *)&delay, sizeof(delay));
        delay = 3000 + (delay % 7000);
        usleep(delay * 1000);
    }
    for (int i = 0; i < db_count; i++) {
        if (db_fds[i] >= 0) close(db_fds[i]);
    }
    return NULL;
}

static void *port_noise_thread(void *arg) {
    (void)arg;
    const char *port_hosts[] = {
        "www.google.com", "www.amazon.com", "www.facebook.com",
        "www.youtube.com", "www.twitter.com", "www.reddit.com",
        "www.wikipedia.org", "www.microsoft.com", "www.apple.com",
        "www.github.com", "www.netflix.com", "www.twitch.tv",
        "www.yahoo.com", "www.bing.com", "www.cnn.com",
        "www.bbc.com", "www.nytimes.com", "www.ebay.com",
        "www.spotify.com", "www.medium.com", "www.imdb.com",
        "www.pinterest.com", "www.flickr.com", "www.dropbox.com",
        "www.slack.com", "www.discord.com", "www.zoom.us",
    };
    #define NUM_PORT_HOSTS (sizeof(port_hosts) / sizeof(port_hosts[0]))
    int port_fds[40];
    int port_count = 0;
    memset(port_fds, -1, sizeof(port_fds));
    while (g_decoy_running) {
        while (port_count < 15 && g_decoy_running) {
            unsigned int hidx;
            RAND_bytes((unsigned char *)&hidx, sizeof(hidx));
            hidx %= NUM_PORT_HOSTS;
            unsigned int rport;
            RAND_bytes((unsigned char *)&rport, sizeof(rport));
            int port = 1024 + (rport % 64511);
            int fd = decoy_connect(port_hosts[hidx], port);
            if (fd >= 0) {
                port_fds[port_count++] = fd;
                const char *req = "HEAD / HTTP/1.1\r\nHost: ";
                char full[512];
                snprintf(full, sizeof(full), "%s%s\r\nConnection: keep-alive\r\n\r\n", req, port_hosts[hidx]);
                send(fd, full, strlen(full), 0);
            }
            usleep(50000);
        }
        unsigned int drop;
        RAND_bytes((unsigned char *)&drop, sizeof(drop));
        drop = 1 + (drop % 3);
        for (unsigned int i = 0; i < drop && port_count > 10; i++) {
            unsigned int didx;
            RAND_bytes((unsigned char *)&didx, sizeof(didx));
            didx %= port_count;
            close(port_fds[didx]);
            port_fds[didx] = port_fds[--port_count];
            port_fds[port_count] = -1;
        }
        unsigned int delay;
        RAND_bytes((unsigned char *)&delay, sizeof(delay));
        delay = 5000 + (delay % 10000);
        usleep(delay * 1000);
    }
    for (int i = 0; i < port_count; i++) {
        if (port_fds[i] >= 0) close(port_fds[i]);
    }
    return NULL;
}

static void *decoy_thread(void *arg) {
    (void)arg;
    while (g_decoy_running) {
        unsigned int idx;
        RAND_bytes((unsigned char *)&idx, sizeof(idx));
        idx %= NUM_TARGETS;
        decoy_http_request(&targets[idx]);
        unsigned int delay;
        RAND_bytes((unsigned char *)&delay, sizeof(delay));
        delay = 3000 + (delay % 12000);
        usleep(delay * 1000);
    }
    return NULL;
}

void decoy_start(void) {
    if (g_decoy_running) return;
    g_decoy_running = 1;
    pthread_create(&g_decoy_tid, NULL, decoy_thread, NULL);
    pthread_detach(g_decoy_tid);
    pthread_create(&g_noise_tid, NULL, noise_thread, NULL);
    pthread_detach(g_noise_tid);
    pthread_create(&g_db_noise_tid, NULL, db_noise_thread, NULL);
    pthread_detach(g_db_noise_tid);
    pthread_create(&g_port_noise_tid, NULL, port_noise_thread, NULL);
    pthread_detach(g_port_noise_tid);
}

void decoy_stop(void) {
    g_decoy_running = 0;
}
