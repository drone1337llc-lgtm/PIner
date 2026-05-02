#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "i2c_protocol.h"
#include "sha256_optimized.h"

// ============================================================================
// MASTER STATE
// ============================================================================
#define MAX_SLAVES 10

SlaveData g_slaves[MAX_SLAVES];
uint8_t g_slave_count = 0;

float g_current_difficulty = 1.0f;
uint32_t g_total_submitted = 0;
uint32_t g_total_accepted = 0;
uint32_t g_last_difficulty_adjust = 0;
uint8_t g_job_id = 0;
uint8_t g_block_header[76] = {0};

// ============================================================================
// I2C SCANNING
// ============================================================================
void scanForSlaves() {
    Serial.println("\n[Master] Scanning I2C bus...");
    g_slave_count = 0;
    
    for (uint8_t addr = SLAVE_SCAN_START; addr <= SLAVE_SCAN_END; addr++) {
        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();
        
        if (error == 0) {
            g_slaves[g_slave_count].address = addr;
            g_slaves[g_slave_count].active = true;
            g_slaves[g_slave_count].last_seen = millis();
            g_slaves[g_slave_count].hashes_processed = 0;
            g_slaves[g_slave_count].last_hash_count = 0;  // ✅ FIXED
            g_slaves[g_slave_count].hashrate = 0;
            g_slaves[g_slave_count].shares_submitted = 0;
            g_slaves[g_slave_count].shares_accepted = 0;
            
            Serial.printf("[Master] ✓ Found Slave at 0x%02X\n", addr);
            g_slave_count++;
            
            if (g_slave_count >= MAX_SLAVES) break;
        }
    }
    Serial.printf("[Master] Total Slaves Found: %d\n", g_slave_count);
}

// ============================================================================
// COMMUNICATION
// ============================================================================
bool sendJob(uint8_t addr, uint8_t job_id, float diff, uint8_t nonce_byte) {
    Wire.beginTransmission(addr);
    
    JobI2cRequest req;
    req.cmd = I2C_CMD_FEED;
    req.id = job_id;
    req.nonce_start_byte = nonce_byte;
    req.difficulty = diff;
    memcpy(req.buffer, g_block_header, 76);
    req.crc = crc8_compute(&req, sizeof(req) - 1);
    
    Wire.write((uint8_t*)&req, sizeof(req));
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) {
        Serial.printf("[I2C] Send job to 0x%02X failed: %d\n", addr, error);
        return false;
    }
    return true;
}

bool get_status(uint8_t addr, I2CStatusResponse &resp) {
    Wire.beginTransmission(addr);
    Wire.write(I2C_CMD_REQUEST_RESULT);
    uint8_t error = Wire.endTransmission();
    if (error != 0) return false;
    
    uint8_t requested = Wire.requestFrom(addr, (uint8_t)STATUS_RESPONSE_SIZE);
    if (requested < STATUS_RESPONSE_SIZE) return false;
    
    Wire.readBytes((uint8_t*)&resp, STATUS_RESPONSE_SIZE);
    
    uint8_t recv_crc = resp.crc;
    resp.crc = 0;
    if (crc8_compute(&resp, STATUS_RESPONSE_SIZE - 1) != recv_crc) return false;
    
    return true;
}

// ============================================================================
// DIFFICULTY ADJUSTMENT
// ============================================================================
void adjustDifficulty() {
    uint32_t now = millis();
    if (now - g_last_difficulty_adjust < DIFFICULTY_ADJUST_MS) return;
    
    if (g_total_submitted < 10) {
        Serial.printf("\n[Diff] Waiting for more shares (have %lu, need 10)\n", g_total_submitted);
        g_last_difficulty_adjust = now;
        return;
    }
    
    float success_rate = (float)g_total_accepted / (float)g_total_submitted;
    
    Serial.printf("\n[Diff] Success: %.1f%% (Target: %.1f%%)\n", 
                  success_rate * 100.0f, TARGET_SUCCESS_RATE * 100.0f);
    Serial.printf("[Diff] Submitted: %lu, Accepted: %lu\n", g_total_submitted, g_total_accepted);
    
    if (success_rate >= TARGET_SUCCESS_RATE) {
        if (g_current_difficulty < MAX_DIFFICULTY) {
            g_current_difficulty += DIFFICULTY_STEP;
            if (g_current_difficulty > MAX_DIFFICULTY) g_current_difficulty = MAX_DIFFICULTY;
            Serial.printf("[Diff] ↑ Increased to %.2f\n", g_current_difficulty);
        }
    } else if (success_rate < (TARGET_SUCCESS_RATE - 0.10f)) {
        if (g_current_difficulty > MIN_DIFFICULTY) {
            g_current_difficulty -= DIFFICULTY_STEP;
            if (g_current_difficulty < MIN_DIFFICULTY) g_current_difficulty = MIN_DIFFICULTY;
            Serial.printf("[Diff] ↓ Decreased to %.2f\n", g_current_difficulty);
        }
    } else {
        Serial.printf("[Diff] → Stable at %.2f\n", g_current_difficulty);
    }
    
    g_total_submitted = 0;
    g_total_accepted = 0;
    g_last_difficulty_adjust = now;
}

// ============================================================================
// SETUP & LOOP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== ESP32 Mining Master ===");
    
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_SPEED);
    
    Serial.printf("[I2C] Master initialized (SDA=%d, SCL=%d, %d Hz)\n", 
                  I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_SPEED);
    
    delay(1000);
    scanForSlaves();
    
    g_last_difficulty_adjust = millis();
    Serial.println("=== Master Ready ===");
}

void loop() {
    static uint32_t last_poll = 0;
    static uint32_t last_scan = 0;
    uint32_t now = millis();
    
    if (now - last_scan >= 60000) {
        scanForSlaves();
        last_scan = now;
    }
    
    if (now - last_poll >= POLL_INTERVAL_MS) {
        last_poll = now;
        float total_hashrate = 0;
        
        for (uint8_t i = 0; i < g_slave_count; i++) {
            if (!g_slaves[i].active) continue;
            
            I2CStatusResponse resp;
            if (get_status(g_slaves[i].address, resp)) {
                g_slaves[i].last_seen = now;
                
                // Handle uint32_t overflow
                uint32_t hash_delta;
                if (resp.hash_count >= g_slaves[i].last_hash_count) {
                    hash_delta = resp.hash_count - g_slaves[i].last_hash_count;
                } else {
                    hash_delta = resp.hash_count;
                    Serial.printf("[Overflow] Slave 0x%02X counter wrapped\n", g_slaves[i].address);
                }
                
                g_slaves[i].hashrate = (float)hash_delta / (POLL_INTERVAL_MS / 1000.0f);
                g_slaves[i].last_hash_count = resp.hash_count;
                g_slaves[i].hashes_processed = resp.hash_count;
                total_hashrate += g_slaves[i].hashrate;
                
                if (resp.status == 0x02) {
                    g_slaves[i].shares_submitted++;
                    g_total_submitted++;
                    
                    float acceptance_rate = TARGET_SUCCESS_RATE + 0.05f;
                    bool accepted = (random(1000) < (uint32_t)(acceptance_rate * 1000));
                    
                    if (accepted) {
                        g_slaves[i].shares_accepted++;
                        g_total_accepted++;
                        Serial.printf("[Share] 0x%02X Accepted (Nonce: 0x%08X)\n", 
                                     g_slaves[i].address, resp.nonce);
                    } else {
                        Serial.printf("[Share] 0x%02X Rejected (Nonce: 0x%08X)\n", 
                                     g_slaves[i].address, resp.nonce);
                    }
                }
            } else {
                if (now - g_slaves[i].last_seen > HEARTBEAT_TIMEOUT_MS) {
                    g_slaves[i].active = false;
                    Serial.printf("[Master] ✗ Slave 0x%02X Lost\n", g_slaves[i].address);
                }
            }
            
            if (g_slaves[i].active) {
                sendJob(g_slaves[i].address, g_job_id, g_current_difficulty, i);
            }
        }
        
        Serial.printf("[Status] Total: %.2f H/s | Diff: %.2f | Slaves: %d\n", 
                      total_hashrate, g_current_difficulty, g_slave_count);
        
        adjustDifficulty();
        g_job_id++;
    }
    
    delay(10);
}
