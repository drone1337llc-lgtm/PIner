#!/bin/bash
echo "=== Pi Zero 2 W - Multi-Worker Optimized Build ==="
echo "Configuration: 6 workers across 3 cores (2 per core)"
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
cd /home/surge/piner/PinerSolo
rm -rf build
mkdir build
cd build

echo "Building with optimizations..."
cmake .. -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a53 -march=armv8-a+simd -ftree-vectorize -funroll-loops -flto -ffast-math -DNDEBUG -mfloat-abi=hard -mfpu=neon-fp-armv8" \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_EXE_LINKER_FLAGS="-flto -Wl,--gc-sections -Wl,--as-needed"

make -j4

echo ""
echo "=== Build Complete ==="
echo "Configuration:"
echo "  - Mining Cores: 3 (cores 1, 2, 3)"
echo "  - Workers: 6 (2 per core)"
echo "  - Core 0: Reserved for stratum/system"
echo "  - Difficulty: 0 (pool-determined)"
echo ""
echo "Log file: ~/piner/PinerSolo/build/miner.log"
echo "View logs: tail -f /home/surge/piner/PinerSolo/build/miner.log"
echo "Dashboard: http://localhost:8080"
echo ""
echo "Run with: sudo screen /home/surge/piner/PinerSolo/build/.pi_miner --pool-user bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
