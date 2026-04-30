#include <Arduino.h>
#include <Wire.h>
#include <esp_task_wdt.h>
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

// Dual-core mining support
#if SOC_CPU_CORES_NUM >= 2
MinerCore miner_core1;
TaskHandle_t mining_task_handle = nullptr;
#endif

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
// DUAL-CORE MINING TASK
// ============================================================================

#if SOC_CPU_CORES_NUM >= 2
void miningTaskCore1(void* parameter) {
    miner_core1.setCoreId(1);
    
    while (true) {
        if (i2c_slave->m_mining_enabled && miner.isMining()) {
            // FIX: Use public getter methods instead of accessing private members
            uint32_t range = miner.getJobNonceRange();
            uint32_t start = miner.getJobNonceStart() + (range / 2);
            uint32_t end = miner.getJobNonceStart() + range;
            
            miner_core1.runBatch(start, end);
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}
#endif

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    
    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTLN("  ESP32 Bitcoin Miner Slave (Optimized)");
    DEBUG_PRINTLN("========================================");
    
    // Configure WDT
    esp_task_wdt_init(WDT_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL);
    
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
    miner.setCoreId(0);
    
    #if SOC_CPU_CORES_NUM >= 2
    miner_core1.begin();
    miner_core1.setCoreId(1);
    
    // Start mining task on Core 1
    xTaskCreatePinnedToCore(
        miningTaskCore1,
        "Mining_Core1",
        MINING_STACK_SIZE,
        NULL,
        3,
        &mining_task_handle,
        1
    );
    DEBUG_PRINTLN("[Setup] Core 1 mining task started");
    #endif
    
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
        i2c_slave->m_mining_enabled = false;
        DEBUG_PRINTLN("[Loop] Heartbeat timeout - mining disabled");
    }
    
    if (i2c_slave->m_mining_enabled) {
        if (i2c_slave->hasNewJob()) {
            JobI2cRequest raw_job = i2c_slave->getCurrentJob();
            
            JobRequest m_job;
            m_job.job_id = raw_job.id;
            m_job.difficulty = raw_job.difficulty;
            m_job.nonce_start = raw_job.nonce_start;
            m_job.nonce_range = NONCES_PER_JOB;
            memcpy(m_job.header_bytes, raw_job.buffer, 76);
            
            miner.setNewJob(m_job);
            
            #if SOC_CPU_CORES_NUM >= 2
            // Core 1 will automatically pick up the job
            #endif
            
            i2c_slave->clearNewJob();
            DEBUG_PRINTF("[Loop] New job received: ID=%d\n", m_job.job_id);
        }
        
        // Core 0 mines first half of nonce range
        if (miner.isMining()) {
            // FIX: Use public getter methods instead of accessing private members
            uint32_t range = miner.getJobNonceRange();
            uint32_t start = miner.getJobNonceStart();
            uint32_t end = start + (range / 2);
            
            miner.runBatch(start, end);
        }
    }
    
    // Report hashes to I2C
    uint32_t hashes = miner.getAndResetHashes();
    #if SOC_CPU_CORES_NUM >= 2
    hashes += miner_core1.getAndResetHashes();
    #endif
    i2c_slave->addHashes(hashes);
    
    // Report found nonce
    uint32_t found_nonce = miner.getFoundNonce();
    #if SOC_CPU_CORES_NUM >= 2
    if (found_nonce == 0xFFFFFFFF) {
        found_nonce = miner_core1.getFoundNonce();
    }
    #endif
    if (found_nonce != 0xFFFFFFFF) {
        i2c_slave->setFoundNonce(found_nonce);
        miner.clearFoundNonce();
        #if SOC_CPU_CORES_NUM >= 2
        miner_core1.clearFoundNonce();
        #endif
        g_shares_found++;
        DEBUG_PRINTF("[Loop] Share found! Total: %lu\n", g_shares_found);
    }
    
    // Update hashrate calculation
    miner.updateHashrate();
    
    // Update LCD display
    #ifdef LCD
    DisplayStats stats;
    stats.hashrate = miner.getHashrate();
    stats.total_hashes = miner.getTotalHashes();
    #if SOC_CPU_CORES_NUM >= 2
    stats.total_hashes += miner_core1.getTotalHashes();
    #endif
    stats.shares_found = g_shares_found;
    stats.jobs_received = miner.getJobsProcessed();
    stats.crc_errors = i2c_slave->m_crc_errors;
    stats.mining_active = miner.isMining();
    stats.core0_active = true;
    #if SOC_CPU_CORES_NUM >= 2
    stats.core1_active = (mining_task_handle != nullptr);
    #endif
    
    display.updateStats(stats);
    display.handleButtons();
    #endif
    
    // Reset WDT
    esp_task_wdt_reset();
    
    // NO DELAY - MAXIMIZE HASH TIME
}
