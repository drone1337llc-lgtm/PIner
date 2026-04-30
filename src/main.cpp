#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include "config.h"
#include "displayDriver.h"
#include "stratum.h"     // Your provided Stratum API
#include "i2c_master.h"  // Your provided ESP-IDF I2C API

WiFiClient client;
bool is_mining = false;
float globalHashrate = 0.0f;
float globalDiff = 0.0f;
uint32_t lastUptime = 0;

// Stratum Global Objects
mining_subscribe mWorker;
mining_job mJob;

// Global UI Objects
LGFX_Master tft;
LGFX_Sprite canvas(&tft);

struct SlaveData {
    uint8_t address;
    uint32_t last_seen;
    uint32_t shares;
    float last_hashrate_raw;
};
std::vector<SlaveData> slave_list;

struct {
    float difficulty = MINIMUM_ACCEPTABLE_DIFFICULTY;
    uint8_t header[76];
    bool new_job = false;
    uint32_t total_shares = 0;
    uint32_t rejected_shares = 0;
    String pool_status = "Connecting...";
} stats;

// --- POOL CONNECTION LOGIC ---
bool connectToPool() {
    Serial.println("[Pool] Connecting...");
    stats.pool_status = "Connecting...";
    
    if (!client.connect(POOL_URL, POOL_PORT)) {
        Serial.println("[Pool] Connection Failed");
        stats.pool_status = "Retry...";
        return false;
    }

    // Use your Stratum API to Handshake
    if (!tx_mining_subscribe(client, mWorker)) return false;
    if (!tx_mining_auth(client, POOL_USER, "x")) return false; // Replace "x" if your pool needs a pass
    
    stats.pool_status = "Mining";
    is_mining = true;
    return true;
}

// --- TASKS ---
void poolTask(void *pv) {
    while(1) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!client.connected()) {
                is_mining = false;
                connectToPool();
            } else {
                // Listen for new jobs and difficulty changes
                while (client.available()) {
                    String line = client.readStringUntil('\n');
                    stratum_method method = parse_mining_method(line);
                    
                    if (method == MINING_NOTIFY) {
                        if (parse_mining_notify(line, mJob)) {
                            stats.new_job = true;
                            Serial.println("[Pool] New Job Received & Parsed");
                        }
                    } else if (method == MINING_SET_DIFFICULTY) {
                        parse_mining_set_difficulty(line, stats.difficulty);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void i2cTask(void *pv) {
    uint8_t current_job_id = 0;
    uint32_t last_hash_calc = millis();
    uint32_t period_nonces = 0;

    while (1) {
        // 1. SCAN (Every 10s to keep bus clean)
        static uint32_t lastScan = 0;
        if (millis() - lastScan > 10000) {
            std::vector<uint8_t> scanned = i2c_master_scan(0x08, 0x77);
            slave_list.clear();
            for (uint8_t addr : scanned) {
                slave_list.push_back({addr, millis(), 0, 0.0f});
            }
            lastScan = millis();
        }

        if (slave_list.empty() || !is_mining) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Extract raw addresses for your i2c_master functions
        std::vector<uint8_t> slave_addrs;
        for (auto &s : slave_list) slave_addrs.push_back(s.address);

        // 2. FEED NEW JOB
        if (stats.new_job) {
            current_job_id++; 
            // Note: Make sure stats.header is populated by your block-builder if required!
            i2c_feed_slaves(slave_addrs, current_job_id, 0x00, stats.difficulty, stats.header);
            stats.new_job = false;
        }

        // 3. HIT & HARVEST
        i2c_hit_slaves(slave_addrs);
        vTaskDelay(pdMS_TO_TICKS(5)); // Brief pause so slaves can prepare the buffer

        uint32_t processed_this_round = 0;
        std::vector<uint32_t> found_nonces = i2c_harvest_slaves(slave_addrs, current_job_id, processed_this_round);
        period_nonces += processed_this_round;

        // 4. SUBMIT SHARES
        for (uint32_t nonce : found_nonces) {
            unsigned long submit_id;
            if (tx_mining_submit(client, mWorker, mJob, nonce, submit_id)) {
                stats.total_shares++;
                Serial.printf("[!] Submitted Share: %08X\n", nonce);
            }
        }

        // 5. CALCULATE HASHRATE (Every second)
        if (millis() - last_hash_calc >= 1000) {
            // Assumes processed_nonces is raw hashes. Convert to KH/s.
            globalHashrate = (float)period_nonces / 1000.0f;
            period_nonces = 0;
            last_hash_calc = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // I2C Polling Rate
    }
}

// --- UI IMPLEMENTATION ---
void initDisplay() {
    tft.init();
    tft.setRotation(1);
    canvas.setColorDepth(8);
    canvas.createSprite(tft.width(), tft.height());
    canvas.fillSprite(TFT_BLACK);
    canvas.pushSprite(0, 0);
}

void updateUI(int slaveCount, float totalHashrate, float diff, uint32_t uptime, String status, float acc) {
    canvas.fillSprite(TFT_BLACK);
    
    // TOP BAR
    canvas.fillRect(0, 0, 320, 25, 0x4208);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    canvas.setCursor(10, 5);
    canvas.print("ESPiner Master");
    
    // DIFFICULTY (Dynamically right-aligned)
    char diffStr[16];
    snprintf(diffStr, sizeof(diffStr), "D:%.0f", diff); 
    int diffWidth = canvas.textWidth(diffStr);
    canvas.setCursor(315 - diffWidth, 5); // 320 width - text width - 5px padding
    canvas.print(diffStr);
    
    // HASHRATE COLOR LOGIC
    uint16_t hashColor;
    if (totalHashrate <= 0.01f) hashColor = TFT_RED;
    else if (totalHashrate <= 100.0f) hashColor = TFT_YELLOW;
    else hashColor = TFT_GREEN;
    
    // HASHRATE VALUE (Truncated & Centered)
    canvas.setTextColor(hashColor);
    canvas.setTextSize(4);
    
    char hashStr[16];
    snprintf(hashStr, sizeof(hashStr), "%.0f", totalHashrate); // %.0f removes decimals
    int hashWidth = canvas.textWidth(hashStr);
    
    int leftCenter = 88; // Half of 176 (where the right-side boxes begin)
    canvas.setCursor(leftCenter - (hashWidth / 2), 35);
    canvas.print(hashStr);
    
    // HASHRATE LABEL (Centered)
    canvas.setTextSize(2);
    int unitWidth = canvas.textWidth("KH/s");
    canvas.setCursor(leftCenter - (unitWidth / 2), 70);
    canvas.print("KH/s");
    
    // SHARES
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    canvas.setCursor(10, 105);
    canvas.printf("S:%lu", stats.total_shares); 
    
    // ACCURACY
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.setCursor(10, 125);
    canvas.printf("ACC:%.1f%%", acc); 
    
    // SLAVE BOXES
    int boxW = 68, boxH = 24, startX = 176, startY = 32, padX = 4, padY = 4; 
    for (int i = 0; i < 8; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = startX + (col * (boxW + padX));
        int y = startY + (row * (boxH + padY));
        
        if (i < slave_list.size()) {
            canvas.fillRoundRect(x, y, boxW, boxH, 3, 0x7BEF); 
            canvas.drawRoundRect(x, y, boxW, boxH, 3, TFT_WHITE);
            canvas.setTextColor(TFT_WHITE);
            canvas.setTextSize(1);
            canvas.setCursor(x + (boxW - 30) / 2, y + 8);
            canvas.printf("0x%02X", slave_list[i].address);
        } else {
            canvas.fillRoundRect(x, y, boxW, boxH, 3, 0x4208); 
            canvas.drawRoundRect(x, y, boxW, boxH, 3, TFT_RED);
            canvas.setTextColor(TFT_WHITE); 
            canvas.setTextSize(1);
            canvas.setCursor(x + (boxW - 18) / 2, y + 8);
            canvas.print("---");
        }
    }
    
    // FOOTER STATUS
    canvas.fillRect(0, 145, 320, 25, TFT_RED);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    String footerMsg = "NODE:" + status;
    int footerX = (320 - (footerMsg.length() * 12)) / 2;
    canvas.setCursor(max(5, footerX), 150);
    canvas.print(footerMsg);
    
    canvas.pushSprite(0, 0);
}

void uiTask(void *pvParameters) {
    while (1) {
        float acc = 100.0f;
        if (stats.total_shares + stats.rejected_shares > 0) {
            acc = ((float)stats.total_shares / (stats.total_shares + stats.rejected_shares)) * 100.0f;
        }

        updateUI(slave_list.size(), globalHashrate, stats.difficulty, millis() / 1000, stats.pool_status, acc);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    initDisplay();
    
    // Initialize your custom ESP-IDF I2C Driver instead of Wire.begin()
    if (i2c_master_start() == ESP_OK) {
        Serial.println("I2C Master Init OK");
    } else {
        Serial.println("I2C Master Init FAILED");
    }

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    // Split the workload into specific cores
    xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(poolTask, "Pool", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(i2cTask, "I2C", 8192, NULL, 2, NULL, 1);
}

void loop() {
    // Left empty. FreeRTOS Tasks are handling everything!
    vTaskDelay(portMAX_DELAY);
}