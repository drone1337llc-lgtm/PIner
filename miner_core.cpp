#include "miner_core.h"
#include "mbedtls/sha256.h"
#include "config.h"

MinerCore::MinerCore() 
    : m_hashes_done(0)
    , m_found_nonce(0xFFFFFFFF)
    , m_new_job(false)
    , m_mining_active(false)
    , m_jobs_processed(0) {
    memset(m_header_work, 0, sizeof(m_header_work));
    mbedtls_sha256_init(&m_ctx_static);
    mbedtls_sha256_init(&m_ctx_active);
}

MinerCore::~MinerCore() {
    mbedtls_sha256_free(&m_ctx_static);
    mbedtls_sha256_free(&m_ctx_active);
}

void MinerCore::begin() {
    DEBUG_PRINTLN("[Miner] Core initialized");
}

void MinerCore::setNewJob(const JobRequest& job) {
    m_current_job = job;
    memcpy(m_header_work, m_current_job.header_bytes, 80);
    
    // PRE-COMPUTE MIDSTATE (first 64 bytes)
    mbedtls_sha256_starts_ret(&m_ctx_static, 0);
    mbedtls_sha256_update_ret(&m_ctx_static, m_header_work, 64);
    
    m_nonce_counter = job.nonce_start;
    m_nonce_end = job.nonce_start + job.nonce_range;
    m_new_job.store(true);
    m_mining_active.store(true);
    m_jobs_processed++;
    
    DEBUG_PRINTF("[Miner] New job: start=%lu end=%lu\n", m_nonce_counter, m_nonce_end);
}

// NON-BLOCKING: Process only 256 nonces per call
void MinerCore::run() {
    if (!m_mining_active.load()) return;
    
    static uint8_t s_hash[32];
    const uint32_t BATCH_SIZE = 1024;  // Increased from 256
    
    uint32_t batch_end = m_nonce_counter + BATCH_SIZE;
    if (batch_end > m_nonce_end) batch_end = m_nonce_end;
    
    // Unroll loop for better performance
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
        
        // Check last byte only (faster, finds more shares)
        if (s_hash[31] == 0) {
            m_found_nonce.store(nonce);
        }
        
        m_hashes_done++;
    }
    
    m_nonce_counter = batch_end;
    
    if (m_nonce_counter >= m_nonce_end) {
        m_mining_active.store(false);
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
