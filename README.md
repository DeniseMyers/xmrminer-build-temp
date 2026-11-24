# XMRMiner — Monero Stratum Miner (RandomX)

Mines Monero (XMR) using RandomX. Supports TCP/TLS/WS/WSS connections. Builds as static Linux x86_64 binaries with post-build `.rodata` encryption and optional light Tigress obfuscation on cold files.

## Quick Start

```bash
make clean && make STATIC=1 -j4
./xmrminer --help
```

## GitHub Actions

See `.github/workflows/build.yml` for compile-only CI (no miner execution). CI installs Tigress, runs `make tigress-heavy` (light automated hiding, cold files only), builds static binaries, and uploads artifacts.

## Documentation

- [Architecture](docs/architecture.md) — System design and data flow
- [Security Features](docs/security.md) — Anti-RE, anti-debug, string encryption
- [Decoy Traffic](docs/decoy.md) — HTTP noise system for traffic obfuscation
- [Build System](docs/build.md) — Build, Tigress, CI
- [Deployment](docs/deployment.md) — Deployment notes
- [CLI Reference](docs/cli.md) — Command line options

## License

Private — not for distribution.
