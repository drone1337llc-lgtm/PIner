#include "miner_core.h"
#include <esp_task_wdt.h>

MinerCore* MinerCore::s_instance = nullptr;

MinerCore::MinerCore() 
    : m_found_nonce(0xFFFFFFFF)
    , m_job_available(false)
    , m_working_job_id(0xFF) {
    s_instance = this;
    m_stats.hashes = 0;
    m_stats.shares_found = 0;
    m_stats.jobs_received = 0;
    m_stats.crc_errors = 0;
    m_stats.hashrate = 0;
    m_stats.mining_active = false;
    m_stats.core0_active = 0;
    m_stats.core1_active = 0;
    m_stats.last_hash_time = 0;
    m_stats.last_share_time = 0;
}

MinerCore::~MinerCore() {
    stopMining();
}

void MinerCore::setNewJob(const JobRequest& job) {
    m_current_job = job;
    m_found_nonce.store(0xFFFFFFFF);
    m_working_job_id = job.job_id & 0xFF;
    m_job_available.store(true);
    m_stats.jobs_received++;
    m_stats.last_hash_time = millis();
    
    LED.setPattern(LED_MINING_ACTIVE);
    
    Serial.printf("[MINER] Job %d loaded, range: %u nonces\n", 
                  job.job_id, job.nonce_range);
}

bool MinerCore::begin() {
    LED.begin();
    LED.setPattern(LED_NO_JOB);
    
    xTaskCreatePinnedToCore(ledTask, "LedTask", 2048, this, 1, NULL, 0);
    
    BaseType_t result0 = xTaskCreatePinnedToCore(
        miningTask0, "Miner0", MINING_STACK_SIZE, this, 
        MINING_TASK_PRIORITY, NULL, 0);
    
    BaseType_t result1 = xTaskCreatePinnedToCore(
        miningTask1, "Miner1", MINING_STACK_SIZE, this, 
        MINING_TASK_PRIORITY, NULL, 1);
    
    if (result0 == pdPASS && result1 == pdPASS) {
        Serial.printf("[MINER] Both cores started (%d byte stack)\n", MINING_STACK_SIZE);
        return true;
    }
    Serial.printf("[MINER] Failed! Core0=%d, Core1=%d\n", result0, result1);
    return false;
}

void MinerCore::ledTask(void* p) {
    MinerCore* miner = (MinerCore*)p;
    uint32_t last_check = 0;
    
    while (true) {
        if (millis() - last_check >= 10) {
            LED.update();
            last_check = millis();
            
            if (!miner->m_stats.mining_active.load()) {
                LED.setPattern(LED_OFF);
            } else if (!miner->m_job_available.load()) {
                LED.setPattern(LED_NO_JOB);
            } else if (miner->m_stats.crc_errors.load() > 10) {
                LED.setPattern(LED_ERROR);
            } else {
                uint32_t since_share = millis() - miner->m_stats.last_share_time;
                if (since_share < 2000) {
                    LED.setPattern(LED_SHARE_FOUND);
                } else {
                    double hr = miner->getHashrate();
                    if (hr > 50000) {
                        LED.setPattern(LED_MINING_ACTIVE);
                    } else {
                        LED.setPattern(LED_MINING_IDLE);
                    }
                }
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

IRAM_ATTR inline bool MinerCore::computeHash(const uint8_t* header, uint32_t nonce, uint8_t* hash) {
    uint8_t full_header[80];
    memcpy(full_header, header, 76);
    
    full_header[76] = nonce & 0xFF;
    full_header[77] = (nonce >> 8) & 0xFF;
    full_header[78] = (nonce >> 16) & 0xFF;
    full_header[79] = (nonce >> 24) & 0xFF;
    
    sha256_double(full_header, 80, hash);
    
    return (hash[0] == 0 && hash[1] == 0);
}

void MinerCore::miningLoop(int core_id) {
    uint8_t header[80];
    uint8_t hash[32];
    uint32_t wdt_counter = 0;
    
    Serial.printf("[CORE%d] Mining started on core %d\n", core_id, xPortGetCoreID());
    
    memcpy(header, m_current_job.version, 4);
    memcpy(header + 4, m_current_job.prev_block_hash, 32);
    memcpy(header + 36, m_current_job.merkle_root, 32);
    memcpy(header + 68, m_current_job.ntime, 4);
    memcpy(header + 72, m_current_job.nbits, 4);
    
    uint32_t core_hashes = 0;
    uint32_t last_report = 0;
    uint32_t batch_count = 0;
    const uint32_t BATCH_SIZE = 500;  // Reduced from 1000 for better reporting
    
    while (true) {
        esp_task_wdt_reset();
        
        if (!m_stats.mining_active.load() || !m_job_available.load()) {
            vTaskDelay(50 / portTICK_PERIOD_MS);
            continue;
        }
        
        if (m_working_job_id != (m_current_job.job_id & 0xFF)) {
            memcpy(header, m_current_job.version, 4);
            memcpy(header + 4, m_current_job.prev_block_hash, 32);
            memcpy(header + 36, m_current_job.merkle_root, 32);
            memcpy(header + 68, m_current_job.ntime, 4);
            memcpy(header + 72, m_current_job.nbits, 4);
            core_hashes = 0;
            last_report = 0;
            batch_count = 0;
            continue;
        }
        
        uint32_t range = m_current_job.nonce_range / 2;
        uint32_t start = m_current_job.nonce_start + (core_id * range);
        uint32_t end = start + range;
        
        for (uint32_t n = start; n < end; n++) {
            uint32_t found = m_found_nonce.load(std::memory_order_acquire);
            if (found != 0xFFFFFFFF || 
                m_working_job_id != (m_current_job.job_id & 0xFF)) {
                break;
            }
            
            if (computeHash(header, n, hash)) {
                uint32_t expected = 0xFFFFFFFF;
                if (m_found_nonce.compare_exchange_strong(expected, n, 
                        std::memory_order_acq_rel)) {
                    m_stats.shares_found++;
                    m_stats.last_share_time = millis();
                    Serial.printf("[CORE%d] ✓ SHARE! Nonce: 0x%08X\n", core_id, n);
                }
                break;
            }
            
            m_stats.hashes++;
            m_total_hashes++;
            core_hashes++;
            batch_count++;
            
            // FIXED: Yield less frequently (every 500 instead of 1000)
            if (batch_count >= BATCH_SIZE) {
                batch_count = 0;
                vTaskDelay(1 / portTICK_PERIOD_MS);
            }
            
            if (core_hashes - last_report >= 50000) {
                Serial.printf("[CORE%d] %u hashes\n", core_id, core_hashes);
                last_report = core_hashes;
            }
            
            if (++wdt_counter >= 500) {
                wdt_counter = 0;
                esp_task_wdt_reset();
            }
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}


void MinerCore::startMining() { 
    m_stats.mining_active = true; 
    m_stats.last_hash_time = millis();
    Serial.println("[MINER] Mining started");
}

void MinerCore::stopMining() { 
    m_stats.mining_active = false; 
    LED.setPattern(LED_OFF);
}

uint32_t MinerCore::getAndResetHashes() { 
    return s_instance->m_stats.hashes.exchange(0); 
}

uint32_t MinerCore::getFoundNonce() { 
    return s_instance->m_found_nonce.exchange(0xFFFFFFFF); 
}

double MinerCore::getHashrate() const {
    uint32_t elapsed = millis() - m_stats.last_hash_time;
    if (elapsed == 0 || elapsed > 10000) return 0;
    return (m_stats.hashes.load() * 1000.0) / elapsed;
}
