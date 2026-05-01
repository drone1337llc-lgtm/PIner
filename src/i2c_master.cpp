#include "i2c_master.h"
#include "config.h"
#include "sha256_optimized.h"
#include <Arduino.h>
#include <driver/i2c.h>

#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_TX_BUF   1024
#define I2C_MASTER_RX_BUF   1024

static i2c_config_t s_i2c_config;

int i2c_master_init() {
    Serial.printf("[I2C] Initializing with SDA=%d, SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
    
    memset(&s_i2c_config, 0, sizeof(s_i2c_config));
    s_i2c_config.mode = I2C_MODE_MASTER;
    s_i2c_config.sda_io_num = I2C_SDA_PIN;
    s_i2c_config.scl_io_num = I2C_SCL_PIN;
    s_i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    s_i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    s_i2c_config.master.clk_speed = I2C_FREQ;

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &s_i2c_config);
    if (err != ESP_OK) {
        Serial.printf("[I2C] Param config failed: %d\n", err);
        return -1;
    }
    
    err = i2c_driver_install(I2C_MASTER_NUM, s_i2c_config.mode, 
                            I2C_MASTER_TX_BUF, I2C_MASTER_RX_BUF, 0);
    if (err != ESP_OK) {
        Serial.printf("[I2C] Driver install failed: %d\n", err);
        return -1;
    }
    
    // Simple scan to verify
    Serial.println("[I2C] Running bus scan...");
    int found = 0;
    for (uint8_t addr = I2C_SCAN_START; addr <= I2C_SCAN_END; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        
        esp_err_t test = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        
        if (test == ESP_OK) {
            Serial.printf("[I2C] ✓ Device at 0x%02X\n", addr);
            found++;
        }
    }
    
    if (found == 0) {
        Serial.println("[I2C] ⚠ NO DEVICES FOUND");
    } else {
        Serial.printf("[I2C] Found %d devices\n", found);
    }
    
    Serial.println("[I2C] Master initialized");
    return 0;
}

std::vector<uint8_t> i2c_master_scan(uint8_t start, uint8_t end) {
    std::vector<uint8_t> found_slaves;
    
    for (uint8_t addr = start; addr <= end; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 10 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            found_slaves.push_back(addr);
        }
    }
    
    return found_slaves;
}

void i2c_feed_slaves(const std::vector<uint8_t>& slaves, uint8_t id, 
                     uint8_t nonce_start, float difficulty, const uint8_t* buffer) {
    if (slaves.empty()) {
        Serial.println("[I2C] Feed skipped - no slaves");
        return;
    }
    
    JobI2cRequest request;
    memset(&request, 0, sizeof(request));
    
    request.cmd = MINER_CMD_FEED;
    request.id = id;
    request.nonce_start_byte = nonce_start;
    request.difficulty = difficulty;
    memcpy(request.buffer, buffer, 76);
    
    for (size_t i = 0; i < slaves.size(); i++) {
        request.crc = crc8_compute(&request, sizeof(request) - 1);
        
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (slaves[i] << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, (const uint8_t*)&request, sizeof(request), true);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 10 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        
        if (ret != ESP_OK) {
            Serial.printf("[I2C] Feed failed for 0x%02X: %d\n", slaves[i], ret);
        } else {
            Serial.printf("[I2C] Job %d sent to 0x%02X\n", id, slaves[i]);
        }
    }
}

void i2c_hit_slaves(const std::vector<uint8_t>& slaves) {
    if (slaves.empty()) return;
    
    uint8_t request[2];
    request[0] = MINER_CMD_REQUEST_RESULT;
    request[1] = crc8_compute(request, 1);
    
    for (size_t i = 0; i < slaves.size(); i++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (slaves[i] << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, request, 2, true);
        i2c_master_stop(cmd);
        
        i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 10 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
    }
}

std::vector<uint32_t> i2c_harvest_slaves(const std::vector<uint8_t>& slaves, 
                                          uint8_t id, uint32_t &total_processed_nonce) {
    std::vector<uint32_t> found_nonces;
    total_processed_nonce = 0;
    
    for (size_t i = 0; i < slaves.size(); i++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (slaves[i] << 1) | I2C_MASTER_READ, true);
        
        I2CStatusResponse response;
        i2c_master_read(cmd, (uint8_t*)&response, sizeof(response), I2C_MASTER_LAST_NACK);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 10 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            uint8_t calculated_crc = crc8_compute(&response, sizeof(response) - 1);
            
            if (calculated_crc == response.crc) {
                if (response.status == 0x02 && response.nonce != 0xFFFFFFFF) {
                    found_nonces.push_back(response.nonce);
                    Serial.printf("[Share] From 0x%02X: %08X\n", slaves[i], response.nonce);
                }
            }
        }
    }
    
    return found_nonces;
}
