# XMRMiner — Monero Stratum Miner (RandomX)

Mines Monero (XMR) using RandomX algorithm. Supports TCP, TLS, WS, WSS to connect to any pool or proxy. Built with OLLVM-16 for maximum obfuscation.

## Quick Start

```bash
make clean && make
./xmrminer --run
```

## Documentation

- [Architecture](docs/architecture.md) — System design and data flow
- [Security Features](docs/security.md) — Anti-RE, anti-debug, string encryption
- [Decoy Traffic](docs/decoy.md) — HTTP noise system for traffic obfuscation
- [Build System](docs/build.md) — Compilation, OLLVM, patcher
- [Deployment](docs/deployment.md) — Server setup and HF space deployment
- [CLI Reference](docs/cli.md) — Command line options

## License

Private — not for distribution.
