#!/bin/bash
echo "=== Pi Zero 2 W - Final Optimized Build ==="
echo ""

echo "Setting CPU governor to performance..."
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu > /dev/null
done

echo ""
echo "CPU Governor Status:"
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "  $cpu: $(cat $cpu)"
done

echo ""
echo "Current CPU settings:"
vcgencmd measure_clock arm
vcgencmd measure_volts
vcgencmd measure_temp

echo ""
echo "Cleaning build..."
cd ~/piner/PinerSolo
rm -rf build
mkdir build
cd build

echo "Building with optimizations..."
cmake .. -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a53 -march=armv8-a+simd -ftree-vectorize -funroll-loops -flto"
make -j4

echo ""
echo "=== Build Complete ==="
echo "Log file: ~/piner/PinerSolo/build/miner.log"
echo "View logs: tail -f ~/piner/PinerSolo/build/miner.log"
echo "Dashboard: http://localhost:8080"
echo ""
echo "Run with: ./pi_miner --pool-user bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
