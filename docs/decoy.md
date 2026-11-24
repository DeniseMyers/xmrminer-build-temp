# Decoy Traffic System

## Overview

The decoy system makes XMRMiner's network traffic look like normal web browsing by maintaining persistent HTTP connections to 30+ major websites on ports 80 and 443.

## Purpose

Without decoys, `ss` or `netstat` would show:
```
ESTAB  chromium  213.192.2.99:41928  ← suspicious single connection
```

With decoys:
```
ESTAB  chromium  142.250.80.46:443    ← Google
ESTAB  chromium  151.101.1.140:80     ← Reddit
ESTAB  chromium  151.101.65.140:8080  ← random high port noise
CLOSE  chromium  151.101.65.140:80    ← Ars Technica
ESTAB  chromium  213.192.2.99:41928   ← mining (buried)
ESTAB  chromium  140.82.121.3:443     ← GitHub
ESTAB  chromium  142.250.80.46:54321  ← random high port noise
...
Total: 27+ connections on diverse ports
```

The mining port 41928 is buried among 26+ legitimate-looking connections across many ports.

## Implementation

### Files
- `src/decoy.h` — Header with `decoy_start()` / `decoy_stop()`
- `src/decoy.c` — Implementation with four threads

### Threads

#### 1. HTTP Decoy Thread
Makes actual HTTP requests to major sites:
- Connects to site on port 80 or 443
- Sends `GET /path HTTP/1.1` with random User-Agent
- Receives response (or timeout)
- Closes connection
- Sleeps 3-15 seconds (random)

#### 2. Noise Thread
Maintains persistent connections:
- Keeps 15 connections open at all times
- Cycles connections every 5-15 seconds
- Drops 1-3 random connections per cycle
- Creates new ones to fill pool

#### 3. DB Noise Thread
Connects to common database hostnames on DB ports:
- Targets 50+ DB hostnames (mongo, mysql, postgres, redis, etc.)
- Uses 50+ DB/service ports (27017, 3306, 5432, 6379, etc.)
- Maintains ~15 connection attempts
- Cycles every 3-10 seconds

#### 4. Port Noise Thread
Connects to famous websites on random high ports:
- Same 27 popular sites as noise thread
- Random ports in range 1024-65535
- Creates traffic diversity beyond just 80/443
- Maintains ~15 connections
- Cycles every 5-15 seconds

### Target Sites (30+)

| Site | Ports |
|------|-------|
| www.google.com | 80, 443 |
| www.amazon.com | 80, 443 |
| www.facebook.com | 80, 443 |
| www.youtube.com | 80, 443 |
| www.twitter.com | 80, 443 |
| www.reddit.com | 80, 443 |
| www.wikipedia.org | 80, 443 |
| www.microsoft.com | 80, 443 |
| www.apple.com | 80, 443 |
| www.github.com | 80, 443 |
| www.netflix.com | 80, 443 |
| www.twitch.tv | 80, 443 |
| www.yahoo.com | 80 |
| www.bing.com | 80 |
| www.cnn.com | 80 |
| www.bbc.com | 80 |
| www.nytimes.com | 80 |
| www.ebay.com | 80, 443 |
| www.spotify.com | 80, 443 |
| www.medium.com | 80 |
| www.imdb.com | 80 |
| www.pinterest.com | 80 |
| www.flickr.com | 80 |
| www.dropbox.com | 80, 443 |
| www.slack.com | 80 |
| www.discord.com | 80, 443 |
| www.zoom.us | 80 |
| www.heroku.com | 80 |
| www.digitalocean.com | 80 |
| www.vultr.com | 80 |

### User-Agents (8 variants)

```
Chrome 125 (Windows)
Safari 17.4 (macOS)
Firefox 126 (Linux)
Edge 125 (Windows)
Safari Mobile (iPhone)
Safari Mobile (iPad)
Chrome 124 (Windows)
Chrome 125 (macOS)
```

Randomly selected per request.

### Request Paths (30 variants)

```
/, /index.html, /about, /contact, /blog,
/products, /services, /pricing, /faq, /help,
/support, /login, /register, /dashboard, /settings,
/profile, /api/v1/status, /api/v2/users,
/static/js/app.js, /static/css/style.css,
/images/logo.png, /favicon.ico, /robots.txt,
/sitemap.xml, /search?q=hello+world,
/category/electronics, /item/12345, /cart,
/checkout, /order/confirm
```

## Timing

| Parameter | Value |
|-----------|-------|
| HTTP request interval | 3-15 seconds (random) |
| Noise connection pool | 15 persistent |
| Noise cycle interval | 5-15 seconds (random) |
| DB noise pool | ~15 connection attempts |
| DB noise cycle interval | 3-10 seconds (random) |
| Port noise pool | 15 persistent |
| Port noise cycle interval | 5-15 seconds (random) |
| Connections dropped per cycle | 1-3 |
| Connection timeout | 3 seconds |

## Randomness

All randomness uses OpenSSL's `RAND_bytes()`:
```c
unsigned int idx;
RAND_bytes((unsigned char *)&idx, sizeof(idx));
idx %= NUM_TARGETS;
```

No `rand()` — cryptographically secure randomness.

## Traffic Analysis

### What an observer sees

1. **Port distribution**: 80, 443, plus random high ports (1024-65535)
2. **Connection count**: 27+ (normal for busy server)
3. **Destination diversity**: 30+ different sites + 50+ DB hostnames (normal)
4. **Request patterns**: Random paths, random intervals (normal)
5. **User-Agents**: Multiple browsers (normal)
6. **Protocol diversity**: HTTP + DB protocols (normal for dev machine)

### What they don't see

- Port 41928 hidden among noise
- No mining protocol signatures in HTTP traffic
- No suspicious timing patterns
- No single-connection anomaly

## Testing

```bash
make clean && make
./xmrminer --run &
sleep 10
ss -tnp | grep chromium
# Should show 12+ connections
kill %1
```

## Configuration

To modify decoy behavior, edit `src/decoy.c`:

```c
// Pool size (default: 12)
while (keep_count < 12 && g_decoy_running) {

// Cycle interval (default: 5-15s)
delay = 5000 + (delay % 10000);

// Connections dropped per cycle (default: 1-2)
drop = 1 + (drop % 2);
```
