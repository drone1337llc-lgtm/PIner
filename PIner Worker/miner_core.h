#ifndef MINER_CORE_H
#define MINER_CORE_H

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sha256_optimized.h"
#include "led_manager.h"

// INCREASED: More stack for WDT safety
#define MINING_STACK_SIZE 10240    // 10KB (was 8KB)
#define MINING_TASK_PRIORITY 2     // LOWER priority (was 3) - let IDLE run
#define NONCES_PER_JOB 0x4000

struct JobRequest {
    uint8_t job_id;
    float difficulty;
    uint32_t nonce_start;
    uint32_t nonce_range;
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
};

struct MiningStats {
    std::atomic<uint32_t> hashes{0};
    std::atomic<uint32_t> shares_found{0};
    std::atomic<uint32_t> jobs_received{0};
    std::atomic<uint32_t> crc_errors{0};
    std::atomic<double> hashrate{0.0};
    std::atomic<bool> mining_active{false};
    std::atomic<uint8_t> core0_active{0};
    std::atomic<uint8_t> core1_active{0};
    uint32_t last_hash_time{0};
    uint32_t last_share_time{0};
};

class MinerCore {
public:
    MinerCore();
    ~MinerCore();
    
    bool begin();
    void startMining();
    void stopMining();
    
    void setNewJob(const JobRequest& job);
    
    static uint32_t getAndResetHashes();
    static uint32_t getFoundNonce();
    
    double getHashrate() const;
    uint32_t getHashes() const { return m_stats.hashes.load(); }
    MiningStats& getStats() { return m_stats; }

private:
    static MinerCore* s_instance;
    
    MiningStats m_stats;
    JobRequest m_current_job;
    std::atomic<uint32_t> m_found_nonce;
    std::atomic<bool> m_job_available;
    volatile uint8_t m_working_job_id;
    
    void miningLoop(int core_id);
    static void miningTask0(void* p) { ((MinerCore*)p)->miningLoop(0); }
    static void miningTask1(void* p) { ((MinerCore*)p)->miningLoop(1); }
    static void ledTask(void* p);
    
    inline bool computeHash(const uint8_t* header, uint32_t nonce, uint8_t* hash);
};

#endif // MINER_CORE_H
