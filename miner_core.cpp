#include "miner_core.h"
#include "config.h"

MinerCore::MinerCore() 
    : m_hashes_done(0)
    , m_total_hashes(0)
    , m_found_nonce(0xFFFFFFFF)
    , m_mining_active(false)
    , m_jobs_processed(0)
    , m_hashrate(0.0)
    , m_last_hashrate_update(0) {
    memset(m_header_work, 0, sizeof(m_header_work));
    mbedtls_sha256_init(&m_ctx_static);
    mbedtls_sha256_init(&m_ctx_active);
}

MinerCore::~MinerCore() {
    mbedtls_sha256_free(&m_ctx_static);
    mbedtls_sha256_free(&m_ctx_active);
}

void MinerCore::begin() {
    // Already initialized in constructor
}

void MinerCore::setNewJob(const JobRequest& job) {
    m_current_job = job;
    memcpy(m_header_work, m_current_job.header_bytes, 80);
    
    mbedtls_sha256_starts_ret(&m_ctx_static, 0);
    mbedtls_sha256_update_ret(&m_ctx_static, m_header_work, 64);
    
    m_nonce_counter = job.nonce_start;
    m_nonce_end = job.nonce_start + job.nonce_range;
    m_mining_active.store(true);
    m_jobs_processed++;
}

void MinerCore::run() {
    if (!m_mining_active.load()) return;
    
    static uint8_t s_hash[32];
    const uint32_t BATCH_SIZE = 4096;
    
    uint32_t batch_end = m_nonce_counter + BATCH_SIZE;
    if (batch_end > m_nonce_end) batch_end = m_nonce_end;
    
    for (uint32_t nonce = m_nonce_counter; nonce < batch_end; nonce++) {
        m_header_work[76] = nonce & 0xFF;
        m_header_work[77] = (nonce >> 8) & 0xFF;
        m_header_work[78] = (nonce >> 16) & 0xFF;
        m_header_work[79] = (nonce >> 24) & 0xFF;
        
        mbedtls_sha256_clone(&m_ctx_active, &m_ctx_static);
        mbedtls_sha256_update_ret(&m_ctx_active, m_header_work + 64, 16);
        mbedtls_sha256_finish_ret(&m_ctx_active, s_hash);
        
        mbedtls_sha256_starts_ret(&m_ctx_active, 0);
        mbedtls_sha256_update_ret(&m_ctx_active, s_hash, 32);
        mbedtls_sha256_finish_ret(&m_ctx_active, s_hash);
        
        if (s_hash[31] == 0) {
            m_found_nonce.store(nonce);
        }
        
        m_hashes_done++;
        m_total_hashes++;
    }
    
    m_nonce_counter = batch_end;
    
    if (m_nonce_counter >= m_nonce_end) {
        m_mining_active.store(false);
    }
}

void MinerCore::updateHashrate() {
    uint32_t now = millis();
    static uint32_t last_hashes = 0;
    static uint32_t last_time = 0;
    
    if (now - last_time >= 1000) {
        uint32_t current_hashes = m_total_hashes.load();
        uint32_t hash_delta = current_hashes - last_hashes;
        
        m_hashrate.store(static_cast<double>(hash_delta) / ((now - last_time) / 1000.0));
        
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
