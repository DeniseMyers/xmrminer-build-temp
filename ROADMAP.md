# XMRMiner Roadmap

## Anti-Detect Features

| # | Feature | Status | Notes |
|---|---------|--------|-------|
| 1 | Process name disguise | ✅ Fixed | prctl → "chromium" |
| 2 | Background priority | ✅ Fixed | nice=19, ioprio=768 |
| 3 | Syscall jitter | ✅ Fixed | 0-100us, 20% chance |
| 4 | Syscall pattern shuffle | ✅ Fixed | stat/getrusage/clock_gettime, 15% |
| 5 | Malloc jitter | ✅ Fixed | 0-1ms, 30% during VM init |
| 6 | Socket jitter | ⏭️ Skipped | Network-related, excluded |
| 7 | Thread count limit | ⏭️ Skipped | Not needed |
| 8 | Encrypted strings | ✅ Fixed | XOR-encoded in stratum.c |
| 9 | Burst mode | ❌ Not Fixed | Sleep too short / not organic |
| 10 | Random memory alloc | ✅ Fixed | -randm flag, 25% chance of 0-500us delay |

## Other Fixes

| Item | Status | Notes |
|------|--------|-------|
| Nonce 3-byte NiceHash | ✅ Fixed | 0x00010000 |
| Auto-diff removed | ✅ Fixed | Pool/proxy controls |
| Race condition fix | ✅ Fixed | stratum_get_job() everywhere |
| FPE crash fix | ✅ Fixed | evasion.c removed |
| Full sync | ✅ Fixed | Exact copy of xmrminer-clean |
