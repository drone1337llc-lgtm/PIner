#include <Arduino.h>
#include <driver/i2c.h>
#include "config.h"
#include "i2c_protocol.h"
#include "sha256_optimized.h"

// ============================================================================
// ⚙️ SLAVE ADDRESS - CHANGE THIS PER BOARD (0x10 to 0x70)
// ============================================================================
#define SLAVE_I2C_ADDRESS   0x15
// ============================================================================

// ============================================================================
// SERIAL OUTPUT LIMITING
// ============================================================================
volatile uint32_t g_last_serial_output = 0;
#define SERIAL_OUTPUT_INTERVAL_MS 1000

// ============================================================================
// SLAVE STATE
// ============================================================================
volatile uint32_t g_hashes_computed = 0;
volatile uint32_t g_found_nonce = 0xFFFFFFFF;
volatile bool g_has_job = false;
volatile float g_current_difficulty = 0.01f;
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
// LED HELPERS
// ============================================================================
void setLed(bool on) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
}

void blinkLed(int times, int interval_ms) {
    for (int i = 0; i < times; i++) {
        setLed(true);
        delay(interval_ms);
        setLed(false);
        if (i < times - 1) delay(interval_ms);
    }
}

// ============================================================================
// SERIAL HELPER
// ============================================================================
bool canPrintSerial() {
    if (millis() - g_last_serial_output >= SERIAL_OUTPUT_INTERVAL_MS) {
        g_last_serial_output = millis();
        return true;
    }
    return false;
}

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
    
    Serial.printf("[I2C] ✓ Slave at 0x%02X\n", SLAVE_I2C_ADDRESS);
    
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
                
                if (resp.status == 0x02) {
                    if (canPrintSerial()) {
                        Serial.printf("[Share] Found: %08X\n", resp.nonce);
                    }
                    // ✅ LED: Flash 3x on share found
                    blinkLed(3, 100);
                    g_found_nonce = 0xFFFFFFFF;
                }
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
                    
                    if (canPrintSerial()) {
                        Serial.printf("[Job] ✓ ID:%d Diff:%.3f\n", req.id, req.difficulty);
                    }
                } else {
                    if (canPrintSerial()) {
                        Serial.printf("[I2C] ✗ CRC fail\n");
                    }
                }
            }
        }
        vTaskDelay(2);
    }
}

// ============================================================================
// MINING TASK (Core 1) - WITH LED INDICATORS
// ============================================================================
void mining_task(void* pv) {
    uint8_t hash[32];
    uint32_t last_led_update = 0;
    bool led_state = false;
    
    while (1) {
        if (g_has_job) {
            uint32_t batch_size = 64;
            uint32_t batch_end = g_nonce_current + batch_size;
            if (batch_end > g_nonce_end) batch_end = g_nonce_end;
            
            for (uint32_t n = g_nonce_current; n < batch_end; n++) {
                g_job_header[72] = n & 0xFF;
                g_job_header[73] = (n >> 8) & 0xFF;
                g_job_header[74] = (n >> 16) & 0xFF;
                g_job_header[75] = (n >> 24) & 0xFF;
                
                if (sha256_double_baked(g_job_midstate, g_job_header + 64, g_job_bake, hash, g_current_difficulty)) {
                    g_found_nonce = n;
                    if (canPrintSerial()) {
                        Serial.printf("!!! SHARE: 0x%08X (Diff: %.3f)\n", n, g_current_difficulty);
                    }
                    // ✅ LED: Flash 3x on share found
                    blinkLed(3, 100);
                }
                
                g_hashes_computed++;
            }
            
            // ✅ LED: Slow blink while mining active (500ms on/off)
            if (millis() - last_led_update >= 500) {
                led_state = !led_state;
                setLed(led_state);
                last_led_update = millis();
            }
            
            g_nonce_current = batch_end;
            if (g_nonce_current >= g_nonce_end) {
                g_has_job = false;
                setLed(false);  // LED off when job complete
                if (canPrintSerial()) {
                    Serial.println("[Miner] Job complete");
                }
            }
            
            vTaskDelay(0);
        } else {
            // ✅ LED: Quick blink when waiting for job (2 seconds on/off)
            if (millis() - last_led_update >= 2000) {
                led_state = !led_state;
                setLed(led_state);
                last_led_update = millis();
            }
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
        vTaskDelay(5000);
        uint32_t h = g_hashes_computed;
        uint32_t t = millis();
        float dt = (t - last_t) / 1000.0f;
        if (dt > 0) {
            Serial.printf("[Status] %.2f H/s | Total: %lu | Job: %s\n", 
                         (h - last_h)/dt, h, g_has_job ? "Active" : "Idle");
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
    Serial.printf("[Address] 0x%02X\n", SLAVE_I2C_ADDRESS);
    
    // ✅ Initialize LED
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
    
    // ✅ Boot blink indicator (2 quick blinks)
    blinkLed(2, 200);
    
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);
    pinMode(I2C_SCL_PIN, INPUT_PULLUP);
    
    xTaskCreatePinnedToCore(i2c_slave_task, "I2C_Slave", MINING_STACK_SIZE, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(i2c_process_task, "I2C_Proc", MINING_STACK_SIZE, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(mining_task, "Miner", MINING_STACK_SIZE, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(status_task, "Status", 4000, NULL, 3, NULL, 1);
    
    g_last_serial_output = millis();
    
    Serial.println("=== Slave Ready ===");
}

void loop() {
    vTaskDelay(100);
}
