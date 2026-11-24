# Architecture

## Overview

XMRMiner is a Monero (XMR) miner using the RandomX proof-of-work algorithm. It connects to a stratum proxy via WebSocket Secure (WSS), receives mining jobs, and submits shares when valid hashes are found.

## Data Flow

```
┌─────────────┐     WSS      ┌─────────────┐     TCP      ┌─────────────┐
│  XMRMiner   │──────────────│  xmrproxy   │──────────────│    Pool     │
│  (client)   │   auth key   │  (server)   │   stratum    │  (stratum)  │
└──────┬──────┘              └─────────────┘              └─────────────┘
       │
       │  Decoy HTTP (ports 80/443)
       │
       ├──→ www.google.com
       ├──→ www.amazon.com
       ├──→ www.facebook.com
       └──→ ... (30+ sites)
```

## Components

### XMRMiner (Client)
- Connects to xmrproxy via WSS
- Authenticates with XOR-encoded auth key (`91253e8170324a949121d274fe4bff64`)
- Runs N miner threads (default: 2)
- Each thread mines independently using RandomX
- Submits shares when hash < target
- Maintains 12+ decoy HTTP connections to major sites

### xmrproxy (Server)
- Listens on `0.0.0.0:41928` for WSS connections
- Validates `X-Auth-Key` header
- Forwards stratum messages between client and pool
- Async epoll architecture for high concurrency

### Pool
- Standard Monero stratum pool
- Receives jobs, validates shares
- Provides difficulty adjustment

## Connection Lifecycle

1. **Handshake**: Client connects to proxy, sends `X-Auth-Key` header
2. **Login**: Client sends `mining.subscribe` + `mining.authorize`
3. **Job Loop**: Pool sends jobs → proxy forwards → client mines
4. **Share Submission**: Client finds valid hash → sends `mining.submit` → proxy forwards to pool
5. **Decoy**: Background thread maintains HTTP connections to major sites

## Thread Model

```
Main Thread
├── Anti-debug thread (TracerPid monitor, checksum verification)
├── Decoy HTTP thread (30+ site connections, ports 80/443)
├── Decoy noise thread (12+ persistent connections)
├── Miner thread 0 (RandomX hashing)
├── Miner thread 1 (RandomX hashing)
└── ... (up to 64 threads)
```

## Port Usage

| Port | Purpose | Direction |
|------|---------|-----------|
| 41928 | WSS mining connection | Outbound to proxy |
| 80 | Decoy HTTP | Outbound to major sites |
| 443 | Decoy HTTPS | Outbound to major sites |

## Memory Layout

```
.text       — OLLVM-obfuscated code
.rodata     — encrypted strings (per-build rolling XOR key)
.data       — Initialized globals
.bss        — Uninitialized globals
```

## Binary Size

~7.8MB statically linked. Includes:
- RandomX library (~2MB)
- OpenSSL 3.4.1 (~4MB)
- OLLVM-obfuscated miner code
- Runtime string decoder
