# 🪙 ESPMiner Project - Beginner's Guide

A distributed Bitcoin mining system using ESP32 microcontrollers. One master board coordinates multiple slave boards to solve mining puzzles together.

---

## ⚠️ Important Disclaimer

**This is for EDUCATIONAL purposes only.** ESP32 miners produce extremely low hashrate (~10-50 KH/s total) and will NOT earn meaningful Bitcoin. Modern ASIC miners are millions of times faster. This project teaches:
- I2C communication
- Distributed computing
- Stratum protocol
- Embedded systems programming

---

## 📦 Hardware Requirements

### Master Board (1x)
| Item | Specification | Notes |
|------|--------------|-------|
| Board | TTGO T-Display ESP32 | Has built-in screen |
| Display | 1.14" IPS LCD | 240x135 pixels |
| Price | ~$10-15 | AliExpress, Amazon |

### Slave Boards (2-6x recommended)
| Item | Specification | Notes |
|------|--------------|-------|
| Board | ESP32-WROOM-32 | DO NOT use ESP32-C3 |
| GPIO | Must have GPIO 21/22 | For I2C communication |
| Price | ~$5-8 each | AliExpress, Amazon |

### Additional Hardware
| Item | Quantity | Notes |
|------|----------|-------|
| Jumper Wires | 10-20 | Female-to-Female |
| Breadboard | 1-2 | For connections |
| USB Cables | 1 per board | For power & programming |
| 4.7kΩ Resistors | 2 (optional) | I2C pull-ups if needed |

---

## 🔌 Wiring Diagram

### Master (TTGO T-Display) ↔ Slave (ESP32-WROOM-32)

```
┌─────────────────────────┐         ┌─────────────────────────┐
│    TTGO T-Display       │         │    ESP32-WROOM-32       │
│       (Master)          │         │       (Slave)           │
├─────────────────────────┤         ├─────────────────────────┤
│ GPIO 21 (SDA) ──────────┼─────────┼── GPIO 21 (SDA)         │
│ GPIO 22 (SCL) ──────────┼─────────┼── GPIO 22 (SCL)         │
│ GND        ─────────────┼─────────┼── GND                   │
│ 5V/VIN     ─────────────┼─────────┼── 5V/VIN                │
└─────────────────────────┘         └─────────────────────────┘
         │                                   │
         └─────── Common Ground ─────────────┘
```

### Multiple Slaves (Daisy Chain)

```
Master ─────┬────── Slave 1 (0x10)
            │
            ├────── Slave 2 (0x11)
            │
            ├────── Slave 3 (0x12)
            │
            └────── Slave 4 (0x13)
            
All SDA connected together
All SCL connected together
All GND connected together
All VCC connected together
```

### ⚠️ Critical Wiring Notes

1. **COMMON GROUND** - All boards MUST share the same ground
2. **GPIO 21/22** - Do NOT use GPIO 8/9 (reserved for flash memory)
3. **Power** - Each slave needs its own USB power connection
4. **Pull-up Resistors** - Add 4.7kΩ from SDA→3.3V and SCL→3.3V if communication is unstable

---

## 💻 Software Setup

### Step 1: Install PlatformIO

1. **Install VS Code**: https://code.visualstudio.com/
2. **Install PlatformIO Extension**:
   - Open VS Code
   - Click Extensions (Ctrl+Shift+X)
   - Search "PlatformIO IDE"
   - Click Install
3. **Restart VS Code**

### Step 2: Clone the Project

```bash
git clone <your-repository-url>
cd ESPMiner
```

Or copy all project files to a folder.

### Step 3: Install Dependencies

PlatformIO will automatically install dependencies on first build. Wait for completion.

### Step 4: Configure WiFi & Pool

Edit `config.h`:

```cpp
// WIFI SETTINGS
#define WIFI_SSID       "YourWiFiName"
#define WIFI_PASS       "YourWiFiPassword"

// STRATUM POOL SETTINGS
#define POOL_URL        "pool.solomining.de"
#define POOL_PORT       3333
#define POOL_USER       "your-bitcoin-address"
#define POOL_PASS       "x"
```

---

## 🔧 Configuration

### Master Board (TTGO T-Display)

1. Open `platformio.ini`
2. Select environment: `ttgo-t-display`
3. Upload code:
   - Click PlatformIO icon (left sidebar)
   - Click Upload (arrow icon)
   - Select COM port when prompted

### Slave Boards (ESP32-WROOM-32)

**IMPORTANT**: Each slave needs a UNIQUE I2C address!

1. Open `src/main.cpp` (slave code)
2. Find line 10:
   ```cpp
   #define SLAVE_I2C_ADDRESS   0x10    // ← CHANGE THIS per slave
   ```
3. Assign unique addresses:
   | Slave # | Address |
   |---------|---------|
   | Slave 1 | `0x10` |
   | Slave 2 | `0x11` |
   | Slave 3 | `0x12` |
   | Slave 4 | `0x13` |
   | Slave 5 | `0x14` |
   | Slave 6 | `0x15` |

4. Upload to each slave individually:
   - Change address
   - Upload to that slave
   - Label the board with its address
   - Repeat for next slave

5. Select environment: `esp32dev`

---

## 🚀 First Run Checklist

### Before Powering On

- [ ] All wiring connections secure
- [ ] Common ground connected
- [ ] Each slave has unique I2C address
- [ ] WiFi credentials configured
- [ ] Pool address configured

### Power On Sequence

1. **Connect all slaves** to USB power
2. **Wait 5 seconds** for slaves to initialize
3. **Connect master** to USB power
4. **Open Serial Monitor** (115200 baud)

### Expected Output

**Master Serial:**
```
=== TTGO T-Display Mining Master ===
[Display] Initialized
[I2C] ✓ Device at 0x10
[I2C] ✓ Device at 0x11
[I2C] ✓ Device at 0x12
[I2C] Found 3 devices
[WiFi] Connecting to YourWiFiName...
[WiFi] Connected! IP: 192.168.1.100
[Pool] Connected!
[Pool] Ready
[I2C] ✓ Job 1 sent to 0x10
[I2C] ✓ Job 1 sent to 0x11
[I2C] ✓ Job 1 sent to 0x12
[Hashrate] 15.50 KH/s (Total: 15500)
```

**Slave Serial (each):**
```
=== ESP32 Miner Slave Starting ===
[Address] Manual I2C address: 0x10
[I2C] ✓ Slave initialized at 0x10
[I2C] Slave task started
[Miner] Task started
[System] Slave 0x10 ready
=== Ready ===
[Status] Addr: 0x10, Hashes: 125000, Job: Active
```

---

## 📊 Understanding the Display

```
┌─────────────────────────────────────────┐
│ ESPMiner              D:0.01            │  ← Top bar (difficulty)
├─────────────────────────────────────────┤
│                                         │
│              15                         │  ← Hashrate (KH/s)
│             KH/s                        │
│                                         │
│  Shares: 0                              │
│  Acc: 100.0%                            │
│                                         │
│  ┌────┐ ┌────┐     ┌────┐ ┌────┐       │  ← Slave status boxes
│  │0x10│ │0x11│     │0x14│ │0x15│       │     Green = Active
│  └────┘ └────┘     └────┘ └────┘       │     Grey = Inactive
│  ┌────┐ ┌────┐     ┌────┐              │
│  │0x12│ │0x13│     │---│              │
│  └────┘ └────┘     └────┘              │
├─────────────────────────────────────────┤
│ Status: Mining          WiFi:OK         │  ← Bottom status bar
└─────────────────────────────────────────┘
```

---

## 🐛 Troubleshooting

### Problem: No slaves detected

| Symptom | Solution |
|---------|----------|
| `I2C] Found 0 devices` | Check wiring (SDA/SCL/GND) |
| | Verify all slaves are powered |
| | Check I2C addresses are unique |
| | Add 4.7kΩ pull-up resistors |
| | Try reducing `I2C_FREQ` to 50000 |

### Problem: Slaves keep rebooting

| Symptom | Solution |
|---------|----------|
| `rst:0x8 (TG1WDT_SYS_RESET)` | Reduce Serial output in slave code |
| | Set `I2C_DEBUG_VERBOSE 0` |
| | Check power supply is adequate |
| | Increase `vTaskDelay()` in mining loop |

### Problem: Feed failed error 263

| Symptom | Solution |
|---------|----------|
| `[I2C] ✗ Feed failed for 0x10: 263` | Increase timeout to 500ms |
| | Reduce I2C frequency to 50kHz |
| | Check wiring length (keep short) |
| | Power cycle all slaves |

### Problem: Hashrate shows 0

| Symptom | Solution |
|---------|----------|
| `[Hashrate] 0.00 KH/s` | Slaves are mining but no shares found |
| | This is NORMAL at pool difficulty |
| | Check slave status shows "Job: Active" |
| | Verify `g_hashes_computed` is increasing |

### Problem: Display not updating

| Symptom | Solution |
|---------|----------|
| Screen frozen on boot image | Check `#ifdef LCD` is defined |
| | Verify `-D LCD=1` in platformio.ini |
| | Check `USER_SETUP_ID=25` for TTGO |
| | Try reducing `DISPLAY_UPDATE_MS` |

---

## 📈 Expected Performance

| Configuration | Hashrate | Shares/Day | Earnings |
|--------------|----------|------------|----------|
| 1 Slave | ~3 KH/s | ~0 | $0.00 |
| 3 Slaves | ~10 KH/s | ~0 | $0.00 |
| 6 Slaves | ~20 KH/s | ~0 | $0.00 |

**Reality Check**: You will NOT earn Bitcoin. This is for learning only.

---

## 🔍 Debugging Tips

### Enable Verbose Logging (Slave)

```cpp
#define I2C_DEBUG_VERBOSE   1   // Change from 0 to 1
```

### Test Single Slave

Comment out all slaves except one in master code to isolate issues.

### Monitor I2C Bus

Use logic analyzer or oscilloscope to verify SDA/SCL signals.

### Check Power

Measure voltage at each slave's VCC pin (should be 3.3V-5V).

---

## 📚 Learning Resources

### I2C Protocol
- https://www.i2c-bus.org/
- Understand master/slave, addresses, ACK/NACK

### Stratum Protocol
- https://stratumprotocol.org/
- Mining subscribe, authorize, notify, submit

### ESP32 Documentation
- https://docs.espressif.com/
- GPIO pins, I2C driver, FreeRTOS tasks

### PlatformIO
- https://platformio.org/
- Project structure, environments, upload

---

## 🎯 Next Steps

Once everything works:

1. **Add more slaves** - Scale up to 10+ boards
2. **Optimize mining** - Implement real SHA256 hashing
3. **Add web interface** - Monitor via browser
4. **Log data** - Store hashrate history to SD card
5. **Improve UI** - Add graphs, settings menu

---

## 📞 Support

If you get stuck:

1. Check Serial output from master AND slaves
2. Verify wiring matches diagram exactly
3. Ensure all grounds are connected
4. Try one slave at a time
5. Search error messages in ESP32 documentation

**Common Error Codes:**
- `263` = Timeout (check wiring, speed)
- `0x107` = ESP_ERR_TIMEOUT (increase timeout)
- `rst:0x8` = Watchdog reset (reduce Serial, add delays)

---

## 📄 License

This project is open source for educational purposes. Use at your own risk.

---

**Good luck and happy mining! 🪙⛏️**
