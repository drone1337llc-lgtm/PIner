#!/bin/bash
echo "=== Pi Zero 2 W - Final Optimized Build ==="
echo ""

# Set CPU governor using sysfs (cpufreq-set not available)
echo "Setting CPU governor to performance..."
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu > /dev/null
done

# Verify governor
echo ""
echo "CPU Governor Status:"
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "  $cpu: $(cat $cpu)"
done

# Verify overclock
echo ""
echo "Current CPU settings:"
vcgencmd measure?lock arm
vcgencmd measure_volts
vcgencmd measure_temp

# Clean build
echo ""
echo "Cleaning build..."
cd ~/piner/PinerSolo
rm -rf build
mkdir build
cd build

# Build
echo "Building with optimizations..."
cmake .. -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a53 -march=armv8-a+simd -ftree-vectorize -funroll-loops -flto"
make -j4

echo ""
echo "=== Build Complete ==="
echo "Expected hashrate: 1.8 - 2.2 MH/s"
echo ""
echo "Run with: sudo ./pi_miner --pool-user bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
