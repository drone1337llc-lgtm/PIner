#ifndef MINER_CORE_H
#define MINER_CORE_H

#include <Arduino.h>
#include <atomic>
#include <mbedtls/sha256.h>
#include "config.h"

struct JobRequest {
    uint8_t     header_bytes[80];
    uint32_t    job_id;
    float       difficulty;
    uint32_t    nonce_start;
    uint32_t    nonce_range;
};

class MinerCore {
public:
    MinerCore();
    ~MinerCore();
    
    void begin();
    void run();
    void setNewJob(const JobRequest& job);
    uint32_t getAndResetHashes();
    uint32_t getFoundNonce();
    void clearFoundNonce();
    bool isMining() const { return m_mining_active.load(); }
    uint32_t getJobsProcessed() const { return m_jobs_processed; }

private:
    JobRequest m_current_job;
    mbedtls_sha256_context m_ctx_static;
    mbedtls_sha256_context m_ctx_active;
    
    uint8_t m_header_work[80];
    uint32_t m_nonce_counter;
    uint32_t m_nonce_end;
    
    std::atomic<uint32_t> m_hashes_done{0};
    std::atomic<uint32_t> m_found_nonce{0xFFFFFFFF};
    std::atomic<bool> m_new_job{false};
    std::atomic<bool> m_mining_active{false};
    std::atomic<uint32_t> m_jobs_processed{0};
};

#endif
