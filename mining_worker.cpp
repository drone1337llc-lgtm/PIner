#include "mining_worker.h"
#include <iostream>
#include <thread>
#include <sched.h>

MiningWorker::MiningWorker(int worker_id) : m_worker_id(worker_id) {}
MiningWorker::~MiningWorker() { stop(); }

void MiningWorker::start() {
    if (m_running.load()) return;
    m_running.store(true, std::memory_order_release);
    m_found_nonce.store(0xFFFFFFFF, std::memory_order_release);
    m_thread = std::thread(&MiningWorker::workerThread, this);
    
    // Pin to Core 1, 2, or 3 (NOT Core 0 - that's for Stratum)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int core = (m_worker_id % 3) + 1;  // Maps 0,1,2 to cores 1,2,3
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(m_thread.native_handle(), sizeof(cpuset), &cpuset);
    std::cout << "[Worker " << m_worker_id << "] Started on core " << core << std::endl;
}

void MiningWorker::updateJob(const uint8_t* header, uint32_t nonce_start, uint32_t nonce_end, double difficulty, uint64_t job_version) {
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