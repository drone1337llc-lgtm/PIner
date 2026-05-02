#!/bin/bash
echo "=== Testing Optimization Step ==="
cd ~/piner/PinerSolo/build
sudo ./pi_miner --pool-user bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8 &
PID=$!

# Monitor for 2 minutes
for i in {1..12}; do
    sleep 10
    TEMP=$(vcgencmd measure_temp | cut -d= -f2)
    HASH=$(curl -s http://localhost:8080/stats.json | python3 -c "import sys,json; print(f'{json.load(sys.stdin)[\"hashrate\"]/1000000:.2f}')")
    echo "[$i] Hash: ${HASH} MH/s | Temp: $TEMP"
done

# Stop miner
sudo kill $PID
echo "=== Test Complete ==="
