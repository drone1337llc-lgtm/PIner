#include "mining_worker.h"
#include <iostream>
#include <thread>
#include <sched.h>
#include <unistd.h>

MiningWorker::MiningWorker(int worker_id, int core_id) 
    : m_worker_id(worker_id), m_core_id(core_id) {}

MiningWorker::~MiningWorker() { stop(); }

void MiningWorker::start() {
    if (m_running.load(std::memory_order_acquire)) return;
    
    m_running.store(true, std::memory_order_release);
    m_found_nonce.store(0xFFFFFFFF, std::memory_order_release);
    m_thread = std::thread(&MiningWorker::workerThread, this);
    
    // Pin to assigned core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(m_core_id, &cpuset);
    
    if (pthread_setaffinity_np(m_thread.native_handle(), sizeof(cpuset), &cpuset) != 0) {
        std::cerr << "[Worker " << m_worker_id << "] Failed to set affinity to core " 
                  << m_core_id << std::endl;
    }
    
    // Higher priority for mining threads (SCHED_FIFO for deterministic scheduling)
    struct sched_param sp;
    sp.sched_priority = 25;  // High priority but below stratum (30)
    pthread_setschedparam(m_thread.native_handle(), SCHED_FIFO, &sp);
    
    std::cout << "[Worker " << m_worker_id << "] Started on core " << m_core_id << std::endl;
}

void MiningWorker::updateJob(const uint8_t* header, uint32_t nonce_start, uint32_t nonce_end, 
                              double difficulty, uint64_t job_version) {
    m_job_version.store(job_version, std::memory_order_release);
    m_miner.setJob(header, nonce_start, nonce_end, difficulty, job_version);
}

void MiningWorker::stop() {
    m_running.store(false, std::memory_order_release);
    if (m_thread.joinable()) m_thread.join();
}

void MiningWorker::workerThread() {
    while (m_running.load(std::memory_order_acquire)) {
        m_miner.mine_continuous(m_found_nonce, m_running);
    }
}

uint64_t MiningWorker::getHashCount() const { return m_miner.getHashCount(); }
void MiningWorker::resetHashCount() { m_miner.resetHashCount(); }
