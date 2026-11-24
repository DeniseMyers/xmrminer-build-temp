#!/bin/sh

echo "===== Fake Miner ====="

while true; do
    echo "[+] Starting SSH tunnel..."
    autossh -M 0 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ServerAliveInterval=30 -o ServerAliveCountMax=3 -i ./run_id -L 9990:localhost:8443 -N root@213.192.2.99 -p 41969 &
    SSH_PID=$!

    sleep 20

    echo "[+] Starting xmrminer..."
    ./xmrminer -o "wss://127.0.0.1:9990" -u x+7000 -t 2 --randomize-cpu

    echo "[-] Disconnected, restarting everything in 5s..."
    kill $SSH_PID 2>/dev/null
    sleep 5
done
