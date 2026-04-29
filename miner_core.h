#ifndef MINER_CORE_H
#define MINER_CORE_H

#include <Arduino.h>
#include "mbedtls/sha256.h"
#include <atomic>
#include <cstring>

struct JobRequest {
    uint8_t job_id;
    float difficulty;
    uint32_t nonce_start;
    uint32_t nonce_range;
    uint8_t header_bytes[80];
};

class MinerCore {
public:
    MinerCore();
    ~MinerCore();
    
    void begin();
    void setNewJob(const JobRequest& job);
    void run();
    
    uint32_t getAndResetHashes();
    uint32_t getFoundNonce();
    void clearFoundNonce();
    
    // ADD THESE FOR DISPLAY:
    uint32_t getTotalHashes() const { return m_total_hashes.load(); }
    uint32_t getHashes() const { return m_hashes_done.load(); }
    double getHashrate() const { return m_hashrate.load(); }
    void updateHashrate();
    
    bool isMining() const { return m_mining_active.load(); }
    uint32_t getJobsProcessed() const { return m_jobs_processed; }

private:
    JobRequest m_current_job;
    uint8_t m_header_work[80];
    mbedtls_sha256_context m_ctx_static;
    mbedtls_sha256_context m_ctx_active;
    
    std::atomic<uint32_t> m_hashes_done;
    std::atomic<uint32_t> m_total_hashes;  // ADD THIS
    std::atomic<uint32_t> m_found_nonce;
    std::atomic<bool> m_mining_active;
    std::atomic<double> m_hashrate;  // ADD THIS
    
    uint32_t m_nonce_counter;
    uint32_t m_nonce_end;
    uint32_t m_jobs_processed;
    uint32_t m_last_hashrate_update;
};

#endif
