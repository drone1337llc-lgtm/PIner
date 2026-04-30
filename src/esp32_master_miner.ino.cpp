#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <algorithm> 
#include <vector>
#include "i2c_protocol.h"
#include "config.h"

// --- DUAL BUS CONFIGURATION ---
// Wire (Internal): Pins 8 & 9 (for TCA9554 and Touch)
// Wire1 (Cluster): Dedicated pins for external miners
#define CLUSTER_SDA 17 
#define CLUSTER_SCL 18
TwoWire ClusterBus = TwoWire(1); 

// --- GLOBALS ---
SemaphoreHandle_t internalBusMutex; // Protects Wire (not needed for ClusterBus)
LGFX_Waveshare tft;
LGFX_Sprite canvas(&tft);
WiFiClient poolClient;
bool i2c_ready = false;
int current_page = 0; 

struct SlaveData {
    uint8_t address;
    float hashrate;
    uint32_t accepted;
    uint32_t rejected;
};
std::vector<SlaveData> slave_list;

struct {
    uint64_t hashes = 0;
    uint32_t accepted = 0;
    uint32_t rejected = 0;
    uint64_t start_time = 0;
    float difficulty = 0;
    String job_id = "INIT";
    uint8_t header[76];
    bool new_job = false;
    bool wifi_up = false;
} stats;

// --- I2C TASK (CORE 1) - Dedicated to Mining ---
void i2cTask(void* pv) {
    while (!i2c_ready) vTaskDelay(100 / portTICK_PERIOD_MS);
    
    while (1) {
        std::vector<uint8_t> found_addresses;

        // 1. SCAN PHASE (Now on ClusterBus)
        // No mutex needed! ClusterBus is only used by this task.
        for (uint8_t i = I2C_SCAN_START; i <= I2C_SCAN_END; i++) {
            ClusterBus.beginTransmission(i);
            if (ClusterBus.endTransmission() == 0) {
                found_addresses.push_back(i);
            }
        }

        // 2. UPDATE SLAVE LIST
        if (found_addresses.size() != slave_list.size()) {
            slave_list.clear();
            for (uint8_t addr : found_addresses) {
                slave_list.push_back({addr, 0.0, 0, 0});
            }
        }

        // 3. JOB DISPATCH PHASE
        if (!slave_list.empty() && stats.new_job) {
            uint8_t n_offset = 0x10;
            for (auto &slave : slave_list) {
                JobI2cRequest req; 
                req.cmd = 0xA1; 
                req.nonce_start_byte = n_offset;
                n_offset += 0x10; 
                req.difficulty = stats.difficulty;
                memcpy(req.buffer, stats.header, 76);
                req.crc = 0; // Update with your CommandCrc8 helper

                ClusterBus.beginTransmission(slave.address);
                ClusterBus.write((uint8_t*)&req, sizeof(req));
                ClusterBus.endTransmission();
            }
            stats.new_job = false;
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

// --- UI TASK (CORE 0) ---
void uiTask(void* pv) {
    tft.init(); 
    tft.setRotation(0);
    canvas.setPsram(true); 
    canvas.createSprite(320, 480);
    uint16_t tx, ty;

    while (1) {
        // Only mutex the internal bus (Wire) for touch
        if (xSemaphoreTake(internalBusMutex, pdMS_TO_TICKS(50))) {
            if (tft.getTouch(&tx, &ty)) {
                current_page = (current_page == 0) ? 1 : 0;
                vTaskDelay(200 / portTICK_PERIOD_MS);
            }
            xSemaphoreGive(internalBusMutex);
        }

        canvas.fillSprite(0x0000);
        // ... (Your UI Drawing code) ...
        canvas.pushSprite(0, 0);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    internalBusMutex = xSemaphoreCreateMutex();

    // 1. Initialize Internal Bus (for Expander/Touch)
    Wire.begin(8, 9, 400000); 

    // 2. Initialize Cluster Bus (for Slaves)
    ClusterBus.begin(CLUSTER_SDA, CLUSTER_SCL, 400000);
    ClusterBus.setTimeOut(50);

    // 3. Waveshare Power Sequence (on Internal Wire)
    if (xSemaphoreTake(internalBusMutex, portMAX_DELAY)) {
        Wire.beginTransmission(0x20); Wire.write(0x03); Wire.write(0xFC); Wire.endTransmission();
        Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x01); Wire.endTransmission();
        delay(20);
        Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x00); Wire.endTransmission();
        delay(20);
        Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x03); Wire.endTransmission();
        xSemaphoreGive(internalBusMutex);
    }

    pinMode(TFT_BL, OUTPUT); 
    digitalWrite(TFT_BL, HIGH);

    i2c_ready = true;
    WiFi.begin("SSID", "PASS");
    
    xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(i2cTask, "I2C", 4096, NULL, 1, NULL, 1);
}

void loop() {
    // Keep WiFi and Stratum handling on Core 1 (default) or loop
    if (WiFi.status() == WL_CONNECTED) {
        stats.wifi_up = true;
        // handleStratum(); 
    }
    vTaskDelay(10);
}