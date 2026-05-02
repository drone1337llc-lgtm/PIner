#include "miner_core.h"
#include "config.h"
#include <esp_task_wdt.h>
#include "led_manager.h"

MinerCore::MinerCore()
    : m_hashes_done(0)
    , m_total_hashes(0)
    , m_found_nonce(0xFFFFFFFF)
    , m_mining_active(false)
    , m_jobs_processed(0)
    , m_hashrate(0.0)
    , m_last_hashrate_update(0)
    , m_core_id(0)
    , m_difficulty(10.0f) {
    memset(m_header_work, 0, sizeof(m_header_work));
    memset(&m_sha_ctx, 0, sizeof(m_sha_ctx));
}

MinerCore::~MinerCore() {}

void MinerCore::begin() {
    // Hardware SHA is handled by mbedTLS automatically on ESP32
}

void MinerCore::setNewJob(const JobRequest &job) {
    m_current_job = job;
    m_difficulty = job.difficulty;
    memcpy(m_header_work, m_current_job.header_bytes, 80);

    sha256_midstate(m_current_job.midstate, m_header_work);
    sha256_bake(m_current_job.midstate, m_header_work + 64, m_current_job.bake);

    m_nonce_counter = job.nonce_start;
    m_nonce_end = job.nonce_start + job.nonce_range;
    m_mining_active.store(true);
    m_jobs_processed++;

    DEBUG_PRINTF("[Miner] Job %d started, nonces: %lu-%lu, Diff: %.2f\n",
                 job.job_id, job.nonce_start, m_nonce_end, m_difficulty);
}

void MinerCore::run() {
    if (!m_mining_active.load()) return;
    runBatch(m_nonce_counter, m_nonce_end);
}

void MinerCore::runBatch(uint32_t start_nonce, uint32_t end_nonce) {
    if (!m_mining_active.load()) return;
    
    uint8_t hash[32];
    uint32_t batch_end = start_nonce + HASH_BATCH_SIZE;
    if (batch_end > end_nonce) batch_end = end_nonce;
    
    for (uint32_t nonce = start_nonce; nonce < batch_end; nonce++) {
        m_header_work[72] = nonce & 0xFF;
        m_header_work[73] = (nonce >> 8) & 0xFF;
        m_header_work[74] = (nonce >> 16) & 0xFF;
        m_header_work[75] = (nonce >> 24) & 0xFF;
        
        // ✅ Pass difficulty parameter
        if (sha256_double_baked(m_current_job.midstate, m_header_work + 64, 
                                m_current_job.bake, hash, m_difficulty)) {
            m_found_nonce.store(nonce);
            LED.setPattern(LED_SHARE_FOUND);
            DEBUG_PRINTF("!!! Share Found: %08X (Diff: %.2f)\n", nonce, m_difficulty);
        }
        
        m_hashes_done.fetch_add(1);
        m_total_hashes.fetch_add(1);

        if ((nonce & 0x3FF) == 0) esp_task_wdt_reset();
    }
    m_nonce_counter = batch_end;
    if (m_nonce_counter >= m_nonce_end) m_mining_active.store(false);
}

void MinerCore::updateHashrate() {
    uint32_t now = millis();
    static uint32_t last_hashes = 0;
    static uint32_t last_time = 0;

    if (now - last_time >= HASHRATE_UPDATE_MS) {
        uint32_t current_hashes = m_total_hashes.load();
        uint32_t hash_delta = current_hashes - last_hashes;
        float time_delta = (now - last_time) / 1000.0f;
        
        if (time_delta > 0) {
            m_hashrate.store(static_cast<double>(hash_delta) / time_delta);
        }

        last_hashes = current_hashes;
        last_time = now;
    }
}

uint32_t MinerCore::getAndResetHashes() {
    return m_hashes_done.exchange(0);
}

uint32_t MinerCore::getFoundNonce() {
    return m_found_nonce.load();
}

void MinerCore::clearFoundNonce() {
    m_found_nonce.store(0xFFFFFFFF);
}
