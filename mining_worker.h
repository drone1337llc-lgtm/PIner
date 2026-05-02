#ifndef MINING_WORKER_H
#define MINING_WORKER_H

#include "sha256_miner.h"
#include <thread>
#include <atomic>
#include <sched.h>

class MiningWorker {
public:
    MiningWorker(int worker_id);
    ~MiningWorker();
    
    void start();
    void updateJob(const uint8_t* header, uint32_t nonce_start, uint32_t nonce_end, double difficulty, uint64_t job_version);
    void stop();
    
    uint64_t getHashCount() const;
    void resetHashCount();
    uint32_t getFoundNonce() const { return m_found_nonce.load(std::memory_order_acquire); }
    uint64_t getJobVersion() const { return m_job_version.load(std::memory_order_acquire); }
    bool hasFoundShare() const { return m_found_nonce.load(std::memory_order_acquire) != 0xFFFFFFFF; }
    void clearFoundNonce() { m_found_nonce.store(0xFFFFFFFF, std::memory_order_release); }
    int getId() const { return m_worker_id; }

private:
    int m_worker_id;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<uint32_t> m_found_nonce{0xFFFFFFFF};
    std::atomic<uint64_t> m_job_version{0};
    
    SHA256Miner m_miner;
    
    void workerThread();
};

#endif
