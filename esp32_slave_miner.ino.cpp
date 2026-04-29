#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "miner_core.h"
#include "i2c_slave.h"

#ifdef LCD
#include "display_manager.h"
DisplayManager display;
#endif

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

MinerCore miner;
I2CSlave* i2c_slave = nullptr;

// Mining state
volatile uint32_t g_total_hashes = 0;
volatile uint32_t g_shares_found = 0;
volatile uint32_t g_last_activity = 0;

// ============================================================================
// HARDWARE DETECTION
// ============================================================================

uint8_t detectAddressFromLadder() {
    int adc_val = analogRead(LADDER_PIN);
    uint8_t slot = adc_val / 128; 
    uint8_t finalAddr = I2C_BASE_ADDRESS + slot;
    
    DEBUG_PRINTF("[Hardware] Pin: %d | ADC: %d | Slot: %d | Addr: 0x%02X\n", 
                 LADDER_PIN, adc_val, slot, finalAddr);
    
    return finalAddr;
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    
    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTLN("  ESP32 Bitcoin Miner Slave");
    DEBUG_PRINTLN("========================================");
    
    // Disable Watchdog on Core 0 (hashing can take time)
    disableCore0WDT();
    disableCore1WDT();
    
    // Detect I2C address
    uint8_t myAddr = detectAddressFromLadder();
    
    // Initialize display (if equipped)
    #ifdef LCD
    if (display.begin()) {
        display.showBootScreen();
        delay(1000);
    }
    #endif
    
    // Initialize miner core
    miner.begin();
    
    // Initialize I2C slave
    i2c_slave = new I2CSlave(myAddr);
    i2c_slave->begin(myAddr);
    
    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    DEBUG_PRINTLN("[Setup] Initialization complete");
    DEBUG_PRINTLN("Waiting for jobs from PI master...");
    
    #ifdef LCD
    display.showMiningScreen();
    #endif
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Check heartbeat timeout
    if (i2c_slave->m_mining_enabled && 
        (millis() - i2c_slave->m_last_heartbeat > HEARTBEAT_TIMEOUT_MS)) {
        Serial.println("[WARN] Heartbeat timeout - stopping mining");
        i2c_slave->m_mining_enabled = false;
    }
    
    // ONLY MINE IF ENABLED
    if (i2c_slave->m_mining_enabled) {
        // Get job if available
        if (i2c_slave->hasNewJob()) {
            JobI2cRequest raw_job = i2c_slave->getCurrentJob();
            
            JobRequest m_job;
            m_job.job_id = raw_job.id;
            m_job.difficulty = raw_job.difficulty;
            m_job.nonce_start = raw_job.nonce_start;
            m_job.nonce_range = NONCES_PER_JOB;
            memcpy(m_job.header_bytes, raw_job.buffer, 76);
            
            Serial.printf("[JOB] ID=%d, nonces %lu-%lu\n",
                          m_job.job_id,
                          m_job.nonce_start,
                          m_job.nonce_start + m_job.nonce_range - 1);
            
            miner.setNewJob(m_job);
            i2c_slave->clearNewJob();
        }
        
        // === THIS IS CRITICAL - CALL run() EVERY LOOP ===
        miner.run();
    }
    
    // Always report hashes to I2C master
    uint32_t hashes = miner.getAndResetHashes();
    i2c_slave->addHashes(hashes);
    
    // Report found nonce
    uint32_t found_nonce = miner.getFoundNonce();
    if (found_nonce != 0xFFFFFFFF) {
        Serial.printf("[SHARE] Found: 0x%08X\n", found_nonce);
        i2c_slave->setFoundNonce(found_nonce);
        miner.clearFoundNonce();
    }
    
    // Debug output every 5 seconds
    static uint32_t last_debug = 0;
    if (millis() - last_debug >= 5000) {
        Serial.printf("[DEBUG] Mining: %s | Hashes this cycle: %lu\n",
                      i2c_slave->m_mining_enabled ? "YES" : "NO",
                      hashes);
        last_debug = millis();
    }
    
    delay(1);  // Small delay to prevent watchdog
}





