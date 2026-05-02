#ifndef MINER_CORE_H
#define MINER_CORE_H

#include <Arduino.h>
#include <atomic>
#include "config.h" 
#include "sha256_optimized.h"
#include "i2c_protocol.h"

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
    
    uint32_t getTotalHashes() const { return m_total_hashes.load(); }
    uint32_t getHashes() const { return m_hashes_done.load(); }
    double getHashrate() const { return m_hashrate.load(); }
    void updateHashrate();
    
    bool isMining() const { return m_mining_active.load(); }
    uint32_t getJobsProcessed() const { return m_jobs_processed; }
    
    void setCoreId(int core_id) { m_core_id = core_id; }
    
    uint32_t getJobNonceStart() const { return m_current_job.nonce_start; }
    uint32_t getJobNonceRange() const { return m_current_job.nonce_range; }
    const uint8_t* getJobHeader() const { return m_current_job.header_bytes; }
    const uint32_t* getJobMidstate() const { return m_current_job.midstate; }
    const uint32_t* getJobBake() const { return m_current_job.bake; }

private:
    JobRequest m_current_job;
    uint8_t m_header_work[80];
    sha256_context_opt m_sha_ctx;
    float m_difficulty;  // ✅ ADDED
    
    std::atomic<uint32_t> m_hashes_done{0};
    std::atomic<uint32_t> m_total_hashes{0};
    std::atomic<uint32_t> m_found_nonce{0xFFFFFFFF};
    std::atomic<bool> m_mining_active{false};
    std::atomic<double> m_hashrate{0.0};
    
    uint32_t m_nonce_counter;
    uint32_t m_nonce_end;
    uint32_t m_jobs_processed{0};
    uint32_t m_last_hashrate_update{0};
    int m_core_id{0};
    
    inline bool checkHashFast(const uint8_t* hash) {
        return (hash[30] == 0 && hash[31] == 0);
    }
};

#endif
