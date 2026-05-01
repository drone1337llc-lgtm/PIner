#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include "config.h"
#ifdef SCREEN
#include "displayDriver.h"
#endif
#include "stratum.h"
#include "i2c_master.h"
#include "i2c_protocol.h"

WiFiClient client;

bool g_is_mining = false;
float g_total_hashrate = 0.0f;
float g_current_difficulty = 0.01f;  // Start with LOW difficulty
uint32_t g_total_shares = 0;
uint32_t g_rejected_shares = 0;
String g_pool_status = "Disconnected";

mining_subscribe g_worker;
mining_job g_current_job;

std::vector<SlaveData> g_slave_list;
std::vector<uint8_t> g_slave_addresses;

uint32_t g_last_scan_time = 0;
uint32_t g_last_hashrate_calc = 0;
uint32_t g_period_nonces = 0;
uint8_t g_current_job_id = 0;

bool g_button1_pressed = false;
bool g_button2_pressed = false;
uint32_t g_last_button_check = 0;

// Forward declaration for display access
extern std::vector<SlaveData> g_slave_list;

void checkButtons() {
    if (millis() - g_last_button_check < 50) return;
    g_last_button_check = millis();
    
    if (digitalRead(BUTTON1_PIN) == LOW) {
        g_button1_pressed = true;
        Serial.println("[BTN1] Pressed");
    }
    
    if (digitalRead(BUTTON2_PIN) == LOW) {
        g_button2_pressed = true;
        Serial.println("[BTN2] Pressed");
    }
}

bool connectToPool() {
    Serial.println("[Pool] Connecting...");
    g_pool_status = "Connecting...";
    
    if (!client.connect(POOL_URL, POOL_PORT)) {
        Serial.println("[Pool] Connection failed");
        g_pool_status = "Retry...";
        return false;
    }
    
    Serial.println("[Pool] Connected!");
    
    if (!tx_mining_subscribe(client, g_worker)) {
        Serial.println("[Pool] Subscribe failed");
        return false;
    }
    
    if (!tx_mining_auth(client, POOL_USER, POOL_PASS)) {
        Serial.println("[Pool] Auth failed");
        return false;
    }
    
    // Suggest lower difficulty for ESP32 mining
    tx_suggest_difficulty(client, 0.01f);
    
    g_pool_status = "Mining";
    g_is_mining = true;
    Serial.println("[Pool] Ready");
    return true;
}

void buildBlockHeader(uint8_t* header) {
    memset(header, 0, 76);
    
    uint32_t version = strtoul(g_current_job.version.c_str(), NULL, 16);
    header[0] = (version >> 24) & 0xFF;
    header[1] = (version >> 16) & 0xFF;
    header[2] = (version >> 8) & 0xFF;
    header[3] = version & 0xFF;
    
    for (int i = 0; i < 32 && i < g_current_job.prev_block_hash.length() / 2; i++) {
        char byteStr[3] = {g_current_job.prev_block_hash[i*2], 
                          g_current_job.prev_block_hash[i*2+1], '\0'};
        header[4 + i] = (uint8_t)strtoul(byteStr, NULL, 16);
    }
}

void poolTask(void* pv) {
    while (1) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!client.connected()) {
                g_is_mining = false;
                g_pool_status = "Reconnecting...";
                delay(5000);
                connectToPool();
            } else {
                while (client.available()) {
                    String line = client.readStringUntil('\n');
                    stratum_method method = parse_mining_method(line);
                    
                    if (method == MINING_NOTIFY) {
                        if (parse_mining_notify(line, g_current_job)) {
                            Serial.println("[Pool] New job received");
                            buildBlockHeader(g_current_job.header_bytes);
                        }
                    } else if (method == MINING_SET_DIFFICULTY) {
                        parse_mining_set_difficulty(line, g_current_difficulty);
                        Serial.printf("[Pool] Difficulty updated: %.6f\n", g_current_difficulty);
                    }
                }
            }
        } else {
            g_pool_status = "WiFi Disconnected";
            g_is_mining = false;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void i2cTask(void* pv) {
    Serial.println("[I2C] Starting I2C task...");
    
    while (1) {
        // I2C SCAN
        if (millis() - g_last_scan_time >= I2C_SCAN_INTERVAL_MS) {
            Serial.println("[I2C] Scanning for slaves...");
            g_slave_addresses = i2c_master_scan(I2C_SCAN_START, I2C_SCAN_END);
            
            g_slave_list.clear();
            for (uint8_t addr : g_slave_addresses) {
                SlaveData slave;
                slave.address = addr;
                slave.last_seen = millis();
                slave.shares = 0;
                slave.hashrate = 0.0f;
                slave.hashes_processed = 0;
                g_slave_list.push_back(slave);
                Serial.printf("[I2C] Added slave at 0x%02X\n", addr);
            }
            
            Serial.printf("[I2C] Total slaves: %d\n", g_slave_list.size());
            g_last_scan_time = millis();
        }
        
        if (g_slave_list.empty()) {
            Serial.println("[I2C] No slaves found, waiting...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        
        if (!g_is_mining) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        
        // FEED NEW JOB
        if (g_current_job.job_id.length() > 0) {
            g_current_job_id++;
            uint8_t header[76];
            buildBlockHeader(header);
            
            Serial.printf("[I2C] Feeding job %d to %d slaves\n", 
                         g_current_job_id, g_slave_addresses.size());
            
            i2c_feed_slaves(g_slave_addresses, g_current_job_id, 0x00, 
                           g_current_difficulty, header);
            g_current_job.job_id = "";
        }
        
        // HIT SLAVES
        i2c_hit_slaves(g_slave_addresses);
        vTaskDelay(pdMS_TO_TICKS(5));
        
        // HARVEST RESULTS
        uint32_t processed_this_round = 0;
        std::vector<uint32_t> found_nonces = i2c_harvest_slaves(
            g_slave_addresses, g_current_job_id, processed_this_round);
        g_period_nonces += processed_this_round;
        
        // SUBMIT SHARES
        for (uint32_t nonce : found_nonces) {
            unsigned long submit_id;
            if (tx_mining_submit(client, g_worker, g_current_job, nonce, submit_id)) {
                g_total_shares++;
                Serial.printf("[Share] Submitted: %08X (ID: %lu)\n", nonce, submit_id);
                
                // Update slave share count
                for (auto& slave : g_slave_list) {
                    slave.shares++;
                }
            }
        }
        
        // CALCULATE HASHRATE
        if (millis() - g_last_hashrate_calc >= HASHRATE_UPDATE_MS) {
            g_total_hashrate = (float)g_period_nonces / 1000.0f;
            Serial.printf("[Hashrate] %.2f KH/s\n", g_total_hashrate);
            g_period_nonces = 0;
            g_last_hashrate_calc = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(I2C_POLL_INTERVAL_MS));
    }
}
#ifdef SCREEN
void uiTask(void* pv) {
    while (1) {
        checkButtons();
        
        float accuracy = 100.0f;
        if (g_total_shares + g_rejected_shares > 0) {
            accuracy = ((float)g_total_shares / 
                       (g_total_shares + g_rejected_shares)) * 100.0f;
        }
        
        updateUI(g_slave_list.size(), g_total_hashrate, g_current_difficulty,
                g_total_shares, g_pool_status, accuracy);
        
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_MS));
    }
}
#endif
void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n=== TTGO T-Display Mining Master ===");
    
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
    
    pinMode(ADC_POWER_PIN, OUTPUT);
    digitalWrite(ADC_POWER_PIN, HIGH);
    #ifdef SCREEN
    initDisplay();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(40, 50);
    tft.print("ESPMiner");
    tft.setTextSize(1);
    tft.setCursor(60, 80);
    tft.print("TTGO T-Display");
    delay(2000);
    #endif
    
    if (i2c_master_init() != 0) {
        Serial.println("[FATAL] I2C failed!");
        while (1) delay(1000);
    }
    
    Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_attempts < 30) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Failed!");
        g_pool_status = "WiFi Failed";
    }
    #ifdef SCREEN
    xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 1, NULL, 0);
    #endif
    xTaskCreatePinnedToCore(poolTask, "Pool", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(i2cTask, "I2C", 8192, NULL, 2, NULL, 1);
    
    Serial.println("=== Ready ===");
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
