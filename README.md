# PInerWorker

ESPiner and soon Jetson Nano Miner are in the other branches!!

# 🚀 Raspberry Pi Bitcoin Miner

A high-performance, quad-core optimized Bitcoin miner designed specifically for Raspberry Pi devices. Utilizes ARM NEON SIMD instructions for maximum SHA256 hashing performance.

---

## ⚠️ Important Disclaimer

> **Bitcoin mining on CPU is NOT profitable.** This project is for **educational purposes**, learning about cryptocurrency mining, stratum protocols, and ARM optimization. Do not expect to earn Bitcoin - you will spend more on electricity than you earn.

---

## 📋 Features

| Feature | Description |
|---------|-------------|
| **Quad-Core Optimization** | One mining thread per CPU core with CPU affinity |
| **ARM NEON SIMD** | 4-way parallel SHA256 hashing using NEON instructions |
| **Midstate Caching** | Pre-computes invariant hash state for 60% faster mining |
| **Stratum Protocol** | Full stratum pool support (compatible with most pools) |
| **Web Dashboard** | Real-time monitoring at `http://<pi-ip>:8080` |
| **Low Memory** | Optimized for Pi's limited RAM (<50MB usage) |
| **Real-time Priority** | Mining threads run at SCHED_FIFO priority |
| **No External Hardware** | Runs entirely on Pi (no ESP32 or ASIC required) |

---

## 📊 Performance Expectations

| Raspberry Pi Model | CPU | Expected Hashrate | Power Draw |
|-------------------|-----|-------------------|------------|
| **Pi Zero 2 W** | Quad-core 1.2 GHz | 1.8 - 2.2 MH/s | 2-3W |
| **Pi 3B+** | Quad-core 1.4 GHz | 2.5 - 3.5 MH/s | 4-5W |
| **Pi 4 Model B** | Quad-core 1.5 GHz | 3.5 - 5.0 MH/s | 5-7W |
| **Pi 5** | Quad-core 2.4 GHz | 6.0 - 8.0 MH/s | 8-10W |

*Hashrate depends on overclocking, cooling, and optimization level.*

---

## 🛠️ Prerequisites

### Hardware Requirements
- Raspberry Pi (Zero 2 W, 3B+, 4, or 5)
- MicroSD card (8GB minimum, 16GB+ recommended)
- Power supply (2.5A+ for Pi 4/5, 1.5A+ for Pi Zero 2 W)
- **Heatsink recommended** (Pi will reach 60-70°C under load)
- Ethernet or WiFi connection

### Software Requirements
- Raspberry Pi OS (64-bit recommended for NEON support)
- Internet connection for pool connectivity

---

## 📥 Installation Guide

### Step 1: Update System

```bash
sudo apt update && sudo apt upgrade -y
```

### Step 2: Install Dependencies

```bash
sudo apt install -y build-essential libssl-dev git cmake python3
```

### Step 3: Clone Repository

```bash
cd ~
git clone <your-repo-url> piner
cd ~/piner/PinerSolo
```

---

## ⚙️ Configuration (Optional but Recommended)

### For Pi Zero 2 W Users: Overclocking

> ⚠️ **Warning:** Overclocking may void warranty and increase heat. Use a heatsink!

1. **Edit boot configuration:**
   ```bash
   sudo nano /boot/firmware/config.txt
   ```
   *(On older OS: `/boot/config.txt`)*

2. **Add these lines at the END of the file:**
   ```ini
   # Pi Zero 2 W - Stable Overclock
   over_voltage=4
   arm_freq=1200
   gpu_freq=500
   sdram_freq=500
   force_turbo=1
   over_voltage_min=2
   ```

3. **Save and reboot:**
   ```bash
   sudo reboot
   ```

### Set CPU Governor to Performance

This prevents CPU from scaling down between mining batches:

```bash
# Set all cores to performance mode
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu > /dev/null
done

# Verify
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

**Expected output:** `performance` on all 4 lines

### Make CPU Governor Persistent (Auto-apply on boot)

```bash
sudo nano /etc/systemd/system/cpu-performance.service
```

Paste:
```ini
[Unit]
Description=Set CPU Governor to Performance
After=network.target

[Service]
Type=oneshot
ExecStart=/bin/bash -c 'for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo performance > $cpu; done'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl daemon-reload
sudo systemctl enable cpu-performance
sudo systemctl start cpu-performance
```

---

## 🔨 Building the Miner

### Quick Build (Standard)

```bash
cd ~/piner/PinerSolo
mkdir build && cd build
cmake ..
make -j4
```

### Optimized Build Script (Recommended)

Save as `~/piner/PinerSolo/build_optimized.sh`:

```bash
#!/bin/bash
echo "=== Pi Bitcoin Miner - Optimized Build ==="
echo ""

# Set CPU governor
echo "Setting CPU governor to performance..."
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu > /dev/null
done

# Verify settings
echo ""
echo "CPU Status:"
vcgencmd measure_clock arm
vcgencmd measure_volts
vcgencmd measure_temp

# Clean build
echo ""
echo "Cleaning build..."
cd ~/piner/PinerSolo
rm -rf build
mkdir build
cd build

# Build with optimizations
echo "Building with optimizations..."
cmake ..
make -j4

echo ""
echo "=== Build Complete ==="
echo "Run: sudo ./pi_miner --pool-user <your-wallet>"
```

Make executable and run:
```bash
chmod +x ~/piner/PinerSolo/build_optimized.sh
./build_optimized.sh
```

---

## ▶️ Running the Miner

### Basic Usage

```bash
cd ~/piner/PinerSolo/build
sudo ./pi_miner --pool-user <your-btc-wallet-address>
```

### Full Options

```bash
sudo ./pi_miner \
  --pool-host pool.solomining.de \
  --pool-port 3333 \
  --pool-user bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8 \
  --pool-pass x
```

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--pool-host` | Stratum pool hostname | pool.solomining.de |
| `--pool-port` | Stratum pool port | 3333 |
| `--pool-user` | Pool username/wallet | (required) |
| `--pool-pass` | Pool password | x |
| `--help` | Show help message | - |

### Example: Different Pools

**Solo CK Pool:**
```bash
sudo ./pi_miner --pool-host solo.ckpool.org --pool-port 3333 --pool-user <wallet>
```

**NiceHash:**
```bash
sudo ./pi_miner --pool-host btc.usa.nicehash.com --pool-port 3335 --pool-user <wallet>
```

---

## 📊 Monitoring

### Console Output

The miner displays status every 10 seconds:
```
[Status] Hashrate: 1850000 H/s | Shares: 0/0 | Total: 9250000
```

### Web Dashboard

Open in browser:
```
http://<pi-ip-address>:8080
```

**Local access:**
```
http://localhost:8080
```

### Terminal Monitoring

**Temperature & Clock:**
```bash
watch -n 2 'vcgencmd measure_temp; vcgencmd measure_clock arm'
```

**Full Stats (Hashrate + Temp):**
```bash
watch -n 2 'echo "=== $(date) ==="; vcgencmd measure_temp; vcgencmd measure_clock arm; curl -s http://localhost:8080/stats.json | python3 -c "import sys,json; d=json.load(sys.stdin); print(f\"Hashrate: {d[\"hashrate\"]/1000000:.2f} MH/s\")"'
```

**CPU Usage:**
```bash
top -H -p $(pgrep pi_miner)
```

### Safe Temperature Ranges

| Temperature | Status | Action |
|-------------|--------|--------|
| < 60°C | ✅ Excellent | No action needed |
| 60-70°C | ✅ Good | Monitor regularly |
| 70-80°C | ⚠️ Warm | Add heatsink/fan |
| > 80°C | ❌ Too Hot | Reduce overclock or improve cooling |

---

## 🌐 Web Dashboard Features

| Section | Information |
|---------|-------------|
| **Hash Rate** | Current hashrate (auto-formatted: H/s, kH/s, MH/s) |
| **Total Hashes** | Cumulative hash count since start |
| **Accepted Shares** | Shares accepted by pool |
| **Rejected Shares** | Shares rejected by pool |
| **Success Rate** | Percentage of accepted shares |
| **Difficulty** | Current mining difficulty |
| **Uptime** | Time since miner started |
| **Workers** | Active mining threads (4 cores) |
| **Pool Status** | Connection status (Connected/Disconnected) |

---

## 🔧 Troubleshooting

### Problem: Miner Won't Start

**Solution:**
```bash
# Check if port 8080 is already in use
sudo lsof -i :8080

# Kill existing process if needed
sudo pkill pi_miner

# Try running again
sudo ./pi_miner --pool-user <wallet>
```

### Problem: Hashrate is Low (< 1 MH/s on Pi Zero 2 W)

**Checklist:**
1. Verify CPU governor is set to performance:
   ```bash
   cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
   ```
2. Verify all 4 cores are working:
   ```bash
   top -H -p $(pgrep pi_miner)
   ```
3. Check for compiler optimizations:
   ```bash
   cat CMakeFiles/pi_miner.dir/flags.make | grep CXX_FLAGS
   ```
4. Rebuild with optimized script

### Problem: Connection Failed

**Solutions:**
```bash
# Test internet connection
ping -c 4 google.com

# Test pool connectivity
ping -c 4 pool.solomining.de

# Check firewall
sudo ufw status

# Try different pool port
sudo ./pi_miner --pool-port 3333 --pool-user <wallet>
```

### Problem: Pi Won't Boot After Overclock

**Recovery:**
1. Power off Pi completely (unplug USB)
2. Remove SD card and insert into PC
3. Edit `config.txt` in boot partition
4. Remove or reduce overclock settings:
   ```ini
   over_voltage=0
   arm_freq=1200
   force_turbo=0
   ```
5. Safely eject and reinsert SD card
6. Power on Pi

### Problem: High Temperature (> 80°C)

**Solutions:**
1. Add aluminum heatsink ($5-10)
2. Add small 5V fan ($3-5)
3. Reduce overclock in `config.txt`
4. Improve case ventilation
5. Run in cooler environment

---

## 🚀 Advanced Optimization

### For Experts: Further Tuning

**1. Increase Batch Size** (more cache-efficient):
```cpp
// In pi_miner_config.h
#define HASH_BATCH_SIZE 16384  // Default: 8192
```

**2. Enable Link-Time Optimization:**
```cmake
# In CMakeLists.txt
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
```

**3. Disable Unused Services:**
```bash
# Stop Bluetooth (saves RAM/CPU)
sudo systemctl stop bluetooth
sudo systemctl disable bluetooth

# Stop GUI (if using Raspberry Pi OS Desktop)
sudo systemctl set-default multi-user.target
```

**4. Run as System Service:**

Create `/etc/systemd/system/pi-miner.service`:
```ini
[Unit]
Description=Pi Bitcoin Miner
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/piner/PinerSolo/build
ExecStart=/home/pi/piner/PinerSolo/build/pi_miner --pool-user <wallet>
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl daemon-reload
sudo systemctl enable pi-miner
sudo systemctl start pi-miner
sudo systemctl status pi-miner
```

---

## ❓ FAQ

**Q: Is this profitable?**  
A: No. CPU mining Bitcoin is not profitable. You'll earn fractions of a cent per month while spending more on electricity.

**Q: Can I mine other cryptocurrencies?**  
A: This miner is specifically for SHA256-based coins (Bitcoin, Bitcoin Cash, etc.). It won't work on Scrypt, Ethash, or other algorithms.

**Q: Why is my hashrate lower than expected?**  
A: Check CPU governor settings, ensure all 4 cores are active, verify compiler optimizations, and check for thermal throttling.

**Q: Will this damage my Pi?**  
A: No, as long as you monitor temperature and keep it under 80°C. The Pi has built-in thermal protection.

**Q: Can I run this 24/7?**  
A: Yes, but ensure proper cooling and a reliable power supply. Consider setting up monitoring alerts.

**Q: What pool should I use?**  
A: Popular options: Solo CK Pool, NiceHash, Poolin, or any stratum-compatible pool.

---

## 📁 Project Structure

```
piner/
├── CMakeLists.txt          # Build configuration
├── pi_miner_config.h       # Miner settings
├── pi_miner_main.cpp       # Main entry point
├── sha256_miner.cpp        # NEON-optimized SHA256
├── sha256_miner.h
├── mining_worker.cpp       # Thread management
├── mining_worker.h
├── stratum_client.cpp      # Pool communication
├── stratum_client.h
├── web_server.cpp          # Dashboard server
├── web_server.h
└── build_optimized.sh      # Build script
```

---

## 📄 License

This project is for educational purposes. Use at your own risk.

---

## 🙏 Credits

- SHA256 algorithm: NIST FIPS 180-4
- Stratum protocol: Bitcoin mining standard
- ARM NEON optimization: Custom implementation

---

## 📞 Support

For issues, questions, or contributions:
- Open an issue on GitHub
- Check existing troubleshooting section
- Monitor temperature and logs before reporting

---

**Happy Mining! 🎉**

*Remember: This is for learning, not profit. Enjoy the journey into cryptocurrency mining!*
