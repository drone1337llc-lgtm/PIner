#ifndef MINER_CORE_H
#define MINER_CORE_H

#include <Arduino.h>
#include <atomic>
#include "config.h"
#include "sha256_optimized.h"

struct JobRequest {
    uint8_t job_id;
    float difficulty;
    uint32_t nonce_start;
    uint32_t nonce_range;
    uint8_t header_bytes[80];
    uint32_t midstate[8];
    uint32_t bake[15];
};

class MinerCore {
public:
    MinerCore();
    ~MinerCore();
    
    void begin();
    void setNewJob(const JobRequest& job);
    void run();
    void runBatch(uint32_t start_nonce, uint32_t end_nonce);
    
    uint32_t getAndResetHashes();
    uint32_t getFoundNonce();
    void clearFoundNonce();
    
    // Statistics
    uint32_t getTotalHashes() const { return m_total_hashes.load(); }
    uint32_t getHashes() const { return m_hashes_done.load(); }
    double getHashrate() const { return m_hashrate.load(); }
    void updateHashrate();
    
    bool isMining() const { return m_mining_active.load(); }
    uint32_t getJobsProcessed() const { return m_jobs_processed; }
    
    // Core assignment for dual-core mining
    void setCoreId(int core_id) { m_core_id = core_id; }
    
    // PUBLIC getters for dual-core mining access
    uint32_t getJobNonceStart() const { return m_current_job.nonce_start; }
    uint32_t getJobNonceRange() const { return m_current_job.nonce_range; }
    const uint8_t* getJobHeader() const { return m_current_job.header_bytes; }
    const uint32_t* getJobMidstate() const { return m_current_job.midstate; }
    const uint32_t* getJobBake() const { return m_current_job.bake; }

private:
    JobRequest m_current_job;
    uint8_t m_header_work[80];
    sha256_context_opt m_sha_ctx;
    
    std::atomic<uint32_t> m_hashes_done;
    std::atomic<uint32_t> m_total_hashes;
    std::atomic<uint32_t> m_found_nonce;
    std::atomic<bool> m_mining_active;
    std::atomic<double> m_hashrate;
    
    uint32_t m_nonce_counter;
    uint32_t m_nonce_end;
    uint32_t m_jobs_processed;
    uint32_t m_last_hashrate_update;
    int m_core_id;
    
    inline bool checkHashFast(const uint8_t* hash) {
        return (hash[30] == 0 && hash[31] == 0);
    }
};

#endif
