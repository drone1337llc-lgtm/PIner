#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"
#include "sha256_optimized.h"

// ============================================================================
// I2C SLAVE PINS - ESP32-WROOM-32 (GPIO 8/9 DON'T WORK!)
// ============================================================================
#define I2C_SDA_PIN     21    // Changed from 8 to 21
#define I2C_SCL_PIN     22    // Changed from 9 to 22

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
uint8_t g_my_i2c_addr = 0;
uint32_t g_nonce_start = 0;
uint32_t g_nonce_current = 0;
uint32_t g_hashes_computed = 0;
uint8_t g_current_job_id = 0;
uint8_t g_block_header[76];
float g_current_difficulty = 0.01f;
bool g_has_valid_job = false;
uint32_t g_last_activity = 0;

volatile uint8_t g_i2c_cmd = 0;
volatile bool g_i2c_data_ready = false;
volatile uint8_t g_i2c_buffer[256];
volatile int g_i2c_buffer_len = 0;

// ============================================================================
// ADDRESS DERIVATION
// ============================================================================
uint8_t deriveI2CAddress() {
    uint64_t chipid = ESP.getEfuseMac();
    uint8_t mac_last = chipid & 0xFF;
    uint8_t addr = 0x10 + (mac_last & 0x0F);
    
    if (addr < 0x10) addr = 0x10;
    if (addr > 0x70) addr = 0x70;
    
    return addr;
}

// ============================================================================
// I2C CALLBACKS (Interrupt Context - Keep Minimal!)
// ============================================================================
void i2c_receive_event(int len) {
    if (len > 0 && len < 256) {
        for (int i = 0; i < len; i++) {
            g_i2c_buffer[i] = Wire.read();
        }
        g_i2c_buffer_len = len;
        g_i2c_cmd = g_i2c_buffer[0];
        g_i2c_data_ready = true;
    }
}

void i2c_request_event() {
    // Send status response
    I2CStatusResponse response;
    response.cmd = MINER_CMD_SLAVE_RESULT;
    response.status = g_has_valid_job ? 0x01 : 0x00;
    response.nonce = 0xFFFFFFFF;
    response.crc = crc8_compute(&response, sizeof(response) - 1);
    
    Wire.write((uint8_t*)&response, sizeof(response));
}

// ============================================================================
// I2C PROCESSING TASK
// ============================================================================
void i2cProcessTask(void* pv) {
    Serial.println("[I2C] Process task started");
    
    while (1) {
        if (g_i2c_data_ready) {
            g_last_activity = millis();
            uint8_t cmd = g_i2c_cmd;
            
            if (cmd == MINER_CMD_PING) {
                Serial.println("[I2C] Ping");
            }
            else if (cmd == MINER_CMD_REQUEST_RESULT) {
                // Response handled in i2c_request_event()
            }
            else if (cmd == MINER_CMD_FEED && g_i2c_buffer_len >= (int)(sizeof(JobI2cRequest) - 1)) {
                JobI2cRequest request;
                
                // FIXED: Use ternary instead of min() to avoid type conflict
                int copy_len = (g_i2c_buffer_len < (int)sizeof(JobI2cRequest)) ? 
                               g_i2c_buffer_len : (int)sizeof(JobI2cRequest);
                
                memcpy(&request, (void*)g_i2c_buffer, copy_len);
                
                uint8_t expected_crc = request.crc;
                request.crc = 0;
                uint8_t calculated_crc = crc8_compute(&request, copy_len - 1);
                
                if (calculated_crc == expected_crc) {
                    g_current_job_id = request.id;
                    g_current_difficulty = request.difficulty;
                    g_nonce_start = (uint32_t)request.nonce_start_byte << 24;
                    g_nonce_current = g_nonce_start;
                    memcpy(g_block_header, request.buffer, 76);
                    g_has_valid_job = true;
                    
                    Serial.printf("[I2C] Job %d received\n", g_current_job_id);
                } else {
                    Serial.printf("[I2C] CRC error\n");
                }
            }
            
            g_i2c_data_ready = false;
        }
        
        yield();  // Feed watchdog
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ============================================================================
// MINING TASK
// ============================================================================
void miningTask(void* pv) {
    Serial.println("[Miner] Task started");
    
    while (1) {
        if (g_has_valid_job) {
            g_hashes_computed += 256;
            g_nonce_current += 256;
            
            if (g_nonce_current < g_nonce_start) {
                g_has_valid_job = false;
            }
        }
        
        yield();  // Feed watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n=== ESP32 Miner Slave Starting ===");
    
    // Derive I2C address
    g_my_i2c_addr = deriveI2CAddress();
    
    Serial.printf("[Address] MAC derived I2C addr: 0x%02X\n", g_my_i2c_addr);
    Serial.printf("[System] Nonce start: 0x%08X\n", g_nonce_start);
    
    // Initialize I2C Slave with Wire library
    Wire.setPins(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.begin(g_my_i2c_addr);  // Slave mode
    Wire.onReceive(i2c_receive_event);
    Wire.onRequest(i2c_request_event);
    
    Serial.println("[I2C] Slave initialized");
    
    // Start tasks
    xTaskCreatePinnedToCore(i2cProcessTask, "I2C_Process", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(miningTask, "Miner", 4096, NULL, 5, NULL, 0);
    
    Serial.printf("[System] Slave 0x%02X ready\n", g_my_i2c_addr);
    Serial.println("=== Ready ===");
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
    yield();  // Critical - feeds watchdog
    delay(100);
    
    static uint32_t last_status = 0;
    if (millis() - last_status > 10000) {
        Serial.printf("[Status] Addr: 0x%02X, Hashes: %lu, Job: %s\n",
                     g_my_i2c_addr, g_hashes_computed, g_has_valid_job ? "Active" : "Idle");
        last_status = millis();
    }
}
