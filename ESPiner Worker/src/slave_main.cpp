#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "i2c_protocol.h"
#include "sha256_optimized.h"

// ============================================================================
// SLAVE STATE
// ============================================================================
volatile uint32_t g_hashes_computed = 0;
volatile uint32_t g_found_nonce = 0xFFFFFFFF;
volatile bool g_has_job = false;
volatile float g_current_difficulty = 10.0f;
uint8_t g_job_header[80];
uint32_t g_job_midstate[8];
uint32_t g_job_bake[15];
uint32_t g_nonce_current = 0;
uint32_t g_nonce_end = 0;

static uint8_t* i2c_rx_buf = nullptr;
static uint8_t g_rx_data[256];
static volatile bool g_rx_ready = false;
static volatile int g_rx_len = 0;

// ============================================================================
// I2C SLAVE TASK (Core 1)
// ============================================================================
void i2c_slave_task(void* pv) {
    i2c_config_t conf;
    memset(&conf, 0, sizeof(i2c_config_t));
    
    conf.mode = I2C_MODE_SLAVE;
    conf.sda_io_num = (gpio_num_t)I2C_SDA_PIN;
    conf.scl_io_num = (gpio_num_t)I2C_SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.slave.addr_10bit_en = 0;
    conf.slave.slave_addr = SLAVE_I2C_ADDRESS;
    
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) {
        Serial.printf("[I2C] Config failed: %d\n", err);
        vTaskDelete(NULL);
        return;
    }
    
    err = i2c_driver_install(I2C_PORT, conf.mode, 256, 256, 0);
    if (err != ESP_OK) {
        Serial.printf("[I2C] Install failed: %d\n", err);
        vTaskDelete(NULL);
        return;
    }
    
    Serial.printf("[I2C] Slave at 0x%02X\n", SLAVE_I2C_ADDRESS);
    
    i2c_rx_buf = (uint8_t*)malloc(256);
    if (!i2c_rx_buf) {
        Serial.println("[I2C] Buffer alloc failed");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        int len = i2c_slave_read_buffer(I2C_PORT, i2c_rx_buf, 256, 100 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            memcpy(g_rx_data, i2c_rx_buf, len);
            g_rx_len = len;
            g_rx_ready = true;
            
            if (len >= 1 && g_rx_data[0] == I2C_CMD_REQUEST_RESULT) {
                I2CStatusResponse resp;
                resp.cmd = I2C_CMD_SLAVE_RESULT;
                resp.status = (g_found_nonce != 0xFFFFFFFF) ? 0x02 : 0x01;
                resp.nonce = g_found_nonce;
                resp.hash_count = g_hashes_computed;
                resp.crc = crc8_compute(&resp, sizeof(resp) - 1);
                
                i2c_slave_write_buffer(I2C_PORT, (uint8_t*)&resp, sizeof(resp), 10 / portTICK_PERIOD_MS);
                
                if (resp.status == 0x02) g_found_nonce = 0xFFFFFFFF;
            }
        }
        vTaskDelay(1);
    }
}

// ============================================================================
// I2C PROCESS TASK (Core 1)
// ============================================================================
void i2c_process_task(void* pv) {
    while (1) {
        if (g_rx_ready) {
            uint8_t cmd = g_rx_data[0];
            g_rx_ready = false;
            
            if (cmd == I2C_CMD_FEED && g_rx_len >= (int)(sizeof(JobI2cRequest) - 1)) {
                JobI2cRequest req;
                memcpy(&req, g_rx_data, sizeof(req));
                
                uint8_t exp_crc = req.crc;
                req.crc = 0;
                if (crc8_compute(&req, sizeof(req) - 1) == exp_crc) {
                    memcpy(g_job_header, req.buffer, 76);
                    sha256_midstate(g_job_midstate, g_job_header);
                    sha256_bake(g_job_midstate, g_job_header + 64, g_job_bake);
                    
                    g_nonce_current = (uint32_t)req.nonce_start_byte << 24;
                    g_nonce_end = g_nonce_current + 0x1000000;
                    g_current_difficulty = req.difficulty;
                    g_has_job = true;
                    
                    Serial.printf("[Job] ID:%d Diff:%.2f\n", req.id, req.difficulty);
                } else {
                    Serial.printf("[I2C] CRC fail (exp:0x%02X got:0x%02X)\n", 
                                 exp_crc, crc8_compute(&req, sizeof(req) - 1));
                }
            }
        }
        vTaskDelay(2);
    }
}

// ============================================================================
// MINING TASK (Core 1) - ALL TASKS ON CORE 1
// ============================================================================
void mining_task(void* pv) {
    uint8_t hash[32];
    uint32_t hash_count = 0;
    
    while (1) {
        if (g_has_job) {
            // ✅ Small batches for frequent yields (prevents WDT)
            uint32_t batch_size = 64;
            uint32_t batch_end = g_nonce_current + batch_size;
            if (batch_end > g_nonce_end) batch_end = g_nonce_end;
            
            for (uint32_t n = g_nonce_current; n < batch_end; n++) {
                g_job_header[72] = n & 0xFF;
                g_job_header[73] = (n >> 8) & 0xFF;
                g_job_header[74] = (n >> 16) & 0xFF;
                g_job_header[75] = (n >> 24) & 0xFF;
                
                // ✅ Pass difficulty to hash function for share detection
                if (sha256_double_baked(g_job_midstate, g_job_header + 64, g_job_bake, hash, g_current_difficulty)) {
                    g_found_nonce = n;
                    Serial.printf("!!! SHARE: 0x%08X (Diff: %.2f)\n", n, g_current_difficulty);
                }
                
                g_hashes_computed++;
                hash_count++;
            }
            
            g_nonce_current = batch_end;
            if (g_nonce_current >= g_nonce_end) {
                g_has_job = false;
                Serial.println("[Miner] Job complete, waiting...");
            }
            
            // ✅ Yield after every batch (critical for stability)
            vTaskDelay(0);
        } else {
            // No job - wait for new one
            vTaskDelay(10);
        }
    }
}

// ============================================================================
// STATUS TASK (Core 1)
// ============================================================================
void status_task(void* pv) {
    uint32_t last_h = 0;
    uint32_t last_t = millis();
    
    while (1) {
        vTaskDelay(HASHRATE_UPDATE_MS);
        uint32_t h = g_hashes_computed;
        uint32_t t = millis();
        float dt = (t - last_t) / 1000.0f;
        if (dt > 0) {
            Serial.printf("[Self] %.2f H/s (Total: %lu)\n", (h - last_h)/dt, h);
        }
        last_h = h;
        last_t = t;
    }
}

// ============================================================================
// SETUP & LOOP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== ESP32 Mining Slave ===");
    
    // ✅ Disable WDT - we yield properly so don't need it
    esp_task_wdt_deinit();
    
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);
    pinMode(I2C_SCL_PIN, INPUT_PULLUP);
    
    // ✅ ALL TASKS ON CORE 1 - Core 0 stays free for IDLE
    xTaskCreatePinnedToCore(i2c_slave_task, "I2C_Slave", MINING_STACK_SIZE, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(i2c_process_task, "I2C_Proc", MINING_STACK_SIZE, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(mining_task, "Miner", MINING_STACK_SIZE, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(status_task, "Status", 4000, NULL, 3, NULL, 1);
    
    Serial.println("=== Slave Ready ===");
    Serial.printf("[Info] All tasks on Core 1, Core 0 free for IDLE\n");
}

void loop() {
    // Core 0 just idles - keeps IDLE0 task happy
    vTaskDelay(100);
}
