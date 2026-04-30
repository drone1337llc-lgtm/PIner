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
#define CLUSTER_SCL 18
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
        updateUI(slave_list.size(), stats.difficulty, millis()/1000, stats.pool_status);
        vTaskDelay(500 / portTICK_PERIOD_MS);
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
    tft.setRotation(1);
    canvas.createSprite(tft.width(), tft.height());
}

void updateUI(int slaveCount, float diff, uint32_t uptime, String status) {
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextColor(TFT_GOLD);
    canvas.setTextSize(2);
    canvas.setCursor(10, 10);
    canvas.printf("ESPiner Master S3");
    
    canvas.drawFastHLine(0, 35, tft.width(), TFT_DARKGREY);
    
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(10, 50);
    canvas.printf("Slaves Online: %d", slaveCount);
    
    canvas.setCursor(10, 70);
    canvas.printf("Diff: %.2f", diff);
    
    canvas.setCursor(10, 90);
    canvas.printf("Shares: %lu", stats.total_shares);
    
    canvas.setCursor(10, 110);
    canvas.printf("Uptime: %lus", uptime);

    // Status Bar at bottom
    canvas.fillRect(0, tft.height()-25, tft.width(), 25, TFT_BLUE);
    canvas.setCursor(10, tft.height()-18);
    canvas.printf("NET: %s", status.c_str());
    
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