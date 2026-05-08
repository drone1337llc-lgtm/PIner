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
cmake .. -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a53 -march=armv8-a+simd -ftree-vectorize -funroll-loops -flto -ffast-math" \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_EXE_LINKER_FLAGS="-flto -Wl,--gc-sections"

make -j4

echo ""
echo "=== Build Complete ==="
echo "Expected hashrate: 1.8 - 2.2 MH/s"
echo ""
echo "Run with: sudo ./pi_miner --pool-user bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
