#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <vector>
#include "i2c_protocol.h"
#include "config.h"
#include "displayDriver.h" // Added UI Driver

// Global UI Objects
LGFX_Master tft;
LGFX_Sprite canvas(&tft);

uint8_t crc8_compute(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

#define CLUSTER_SDA 17 
#define CLUSTER_SCL 16
TwoWire ClusterBus = TwoWire(1); 

struct SlaveData {
    uint8_t address;
    uint32_t last_seen;
    uint32_t shares;
};
std::vector<SlaveData> slave_list;

struct {
    float difficulty = 0;
    uint8_t header[76];
    bool new_job = false;
    uint32_t total_shares = 0;
    String pool_status = "Connecting...";
} stats;

// --- NEW UI TASK ---
void uiTask(void* pv) {
    initDisplay();
    while(1) {
        updateUI(slave_list.size(), currentHashrate, currentDiff, uptimeSecs, "Connected");
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void i2cTask(void* pv) {
    while (1) {
        // 1. SCAN
        for (uint8_t i = I2C_SCAN_START; i <= I2C_SCAN_END; i++) {
            ClusterBus.beginTransmission(i);
            if (ClusterBus.endTransmission() == 0) {
                bool exists = false;
                for(auto &s : slave_list) if(s.address == i) exists = true;
                if(!exists) slave_list.push_back({i, millis(), 0});
            }
        }

        // 2. DISPATCH & 3. POLL (Integrated Logic)
        if (!slave_list.empty()) {
            for (auto &slave : slave_list) {
                if (stats.new_job) {
                    JobI2cRequest req; 
                    req.cmd = I2C_CMD_FEED; 
                    req.difficulty = stats.difficulty;
                    memcpy(req.buffer, stats.header, 76);
                    req.crc = crc8_compute(&req.id, sizeof(req) - 2); 

                    ClusterBus.beginTransmission(slave.address);
                    ClusterBus.write((uint8_t*)&req, sizeof(req));
                    ClusterBus.endTransmission();
                }

                // Poll for results
                ClusterBus.requestFrom(slave.address, (uint8_t)5); 
                if (ClusterBus.available() >= 5) {
                    uint8_t status = ClusterBus.read();
                    if (status == 0x02) { 
                        uint32_t nonce;
                        ClusterBus.readBytes((uint8_t*)&nonce, 4);
                        slave.shares++;
                        stats.total_shares++;
                        Serial.printf("[!] Share from 0x%02X: %08X\n", slave.address, nonce);
                    }
                }
            }
            stats.new_job = false;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// --- UI IMPLEMENTATION ---
void initDisplay() {
    tft.init();
    tft.setRotation(1); // Adjust 1 or 3 for your physical mounting
    
    // Use 8-bit color to save RAM and prevent flickering
    canvas.setColorDepth(8); 
    canvas.createSprite(tft.width(), tft.height());
    
    canvas.fillSprite(TFT_BLACK);
    canvas.pushSprite(0, 0);
}

void updateUI(int slaveCount, float totalHashrate, float diff, uint32_t uptime, String status) {
    canvas.fillSprite(TFT_BLACK);

    // --- 1. SLIM HEADER (0-30px) ---
    canvas.fillRect(0, 0, 320, 30, tft.color565(30, 30, 30)); 
    canvas.setTextColor(TFT_GOLD);
    canvas.setTextSize(2);
    canvas.setCursor(10, 7);
    canvas.print("ESPINER MASTER");
    
    // Status indicator dot in header
    canvas.fillCircle(300, 15, 6, (status == "Connected") ? TFT_GREEN : TFT_RED);

    // --- 2. LEFT SIDE: PERFORMANCE STATS ---
    canvas.setTextColor(TFT_WHITE);
    
    // Total Hashrate - High visibility
    canvas.setTextSize(2);
    canvas.setCursor(10, 45);
    canvas.setTextColor(TFT_CYAN);
    canvas.printf("%.2f KH/s", totalHashrate); 
    
    // Secondary Stats
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.setCursor(10, 75);
    canvas.printf("DIFFICULTY: %.1f", diff);
    
    canvas.setCursor(10, 95);
    canvas.printf("TOTAL SHARES: %lu", stats.total_shares);
    
    canvas.setCursor(10, 115);
    canvas.printf("UPTIME: %02d:%02d:%02d", (uptime/3600), (uptime%3600)/60, uptime%60);

    // --- 3. RIGHT SIDE: COMPACT 2x4 GRID ---
    // Total Width: 320. Grid area: ~160 to 310.
    int boxW = 65;
    int boxH = 30; // Shorter boxes
    int startX = 165; 
    int startY = 45;
    int padX = 8;
    int padY = 8;

    for (int i = 0; i < 8; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = startX + (col * (boxW + padX));
        int y = startY + (row * (boxH + padY));

        if (i < slave_list.size()) {
            // OCCUPIED - Solid Green
            canvas.fillRoundRect(x, y, boxW, boxH, 3, tft.color565(0, 140, 0)); 
            canvas.setTextColor(TFT_WHITE);
            canvas.setTextSize(1);
            canvas.setCursor(x + 12, y + 11);
            canvas.printf("0x%02X", slave_list[i].address);
        } else {
            // EMPTY - Outline Grey
            canvas.drawRoundRect(x, y, boxW, boxH, 3, tft.color565(60, 60, 60));
            canvas.setTextColor(tft.color565(80, 80, 80));
            canvas.setCursor(x + 22, y + 11);
            canvas.print("---");
        }
    }

    // --- 4. FOOTER ---
    canvas.fillRect(0, 215, 320, 25, tft.color565(0, 50, 120));
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(10, 222);
    canvas.printf("NODE: %s | SLAVES: %d/8", status.c_str(), slaveCount);

    // --- 5. SMOOTH REFRESH ---
    canvas.pushSprite(0, 0);
}

void setup() {
    Serial.begin(115200);
    delay(2000); 

    ClusterBus.begin(CLUSTER_SDA, CLUSTER_SCL, 100000);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    // Core 0 handles UI, Core 1 handles I2C/Mining logic
    xTaskCreatePinnedToCore(uiTask, "UI", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(i2cTask, "I2C", 4096, NULL, 2, NULL, 1);
}

void loop() {
    if(WiFi.status() == WL_CONNECTED) stats.pool_status = "WiFi OK";
    vTaskDelay(1000);
}