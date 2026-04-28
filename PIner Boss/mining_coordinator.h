#ifndef MINING_COORDINATOR_H
#define MINING_COORDINATOR_H

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <string>
#include <queue>
#include <condition_variable>
#include <set>

#include "config.h"
#include "i2c_master.h"
#include "stratum_client.h"
#include "sha256_utils.h"
#include "web_server.h"
#include "common.h"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct MiningStatsCopy {
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t total_nonces;
    uint64_t start_time;
    uint64_t uptime_seconds;
    double current_hashrate;
    double difficulty;
    uint32_t active_workers;
};

struct MiningStats {
    std::atomic<uint64_t> shares_accepted{0};
    std::atomic<uint64_t> shares_rejected{0};
    std::atomic<uint64_t> total_nonces{0};
    std::atomic<uint64_t> start_time{0};
    std::atomic<double> current_hashrate{0.0};
    std::atomic<double> difficulty{1.0};
    std::atomic<uint32_t> active_workers{0};
    
    MiningStatsCopy getCopy() const {
        MiningStatsCopy copy;
        copy.shares_accepted = shares_accepted.load(std::memory_order_relaxed);
        copy.shares_rejected = shares_rejected.load(std::memory_order_relaxed);
        copy.total_nonces = total_nonces.load(std::memory_order_relaxed);
        copy.start_time = start_time.load(std::memory_order_relaxed);
        copy.current_hashrate = current_hashrate.load(std::memory_order_relaxed);
        copy.difficulty = difficulty.load(std::memory_order_relaxed);
        copy.active_workers = active_workers.load(std::memory_order_relaxed);
        copy.uptime_seconds = 0;
        return copy;
    }
};

struct ESP32MinerState {
    std::atomic<uint32_t> last_nonce_start{0};
    std::atomic<uint32_t> total_hashes{0};
    std::atomic<uint32_t> shares_found{0};
    std::atomic<bool> active{true};
    std::atomic<uint64_t> last_activity{0};
    uint8_t address;
    
    ESP32MinerState() = default;
    explicit ESP32MinerState(uint8_t addr) : address(addr) {}
    
    ESP32MinerState(const ESP32MinerState&) = delete;
    ESP32MinerState& operator=(const ESP32MinerState&) = delete;
};

struct SubmittedShare {
    uint32_t nonce;
    std::string job_id;
    std::string extranonce2;
    std::string ntime;
    uint64_t submit_time;
    std::atomic<bool> verified{false};
    std::atomic<bool> accepted{false};
};

// ============================================================================
// MINING COORDINATOR CLASS
// ============================================================================

class MiningCoordinator {
public:
    MiningCoordinator(const std::string& pool_host, int pool_port,
                     const std::string& pool_user, const std::string& pool_pass);
    ~MiningCoordinator();
    
    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return m_running.load(std::memory_order_relaxed); }
    
    // Statistics
    MiningStatsCopy getStats() const;
    uint64_t getCurrentTimeMs() const;
    void updateDisplayStats(DisplayStats& stats);
    
    // Worker management
    bool addI2CSlave(uint8_t address);
    bool removeI2CSlave(uint8_t address);
    size_t getWorkerCount() const;

private:
    // Core components
    std::unique_ptr<I2CMaster> m_i2c_master;
    std::unique_ptr<StratumClient> m_stratum;
    std::unique_ptr<WebServer> m_web_server;
    
    // State
    std::atomic<bool> m_running;
    std::atomic<uint8_t> m_current_job_id;
    MiningStats m_stats;
    
    // Threads
    std::thread m_main_loop_thread;
    std::thread m_i2c_loop_thread;
    std::thread m_stratum_loop_thread;
    std::thread m_web_loop_thread;
    std::thread m_stats_loop_thread;
    
    // Job synchronization
    mutable std::mutex m_job_mutex;
    MiningJob m_current_job;
    std::string m_current_extranonce2;
    
    // Share tracking (prevent duplicates)
    std::set<uint32_t> m_submitted_nonces;
    std::mutex m_nonce_mutex;
    mutable std::mutex m_shares_mutex;
    std::map<uint32_t, std::shared_ptr<SubmittedShare>> m_pending_shares;
    
    // Pool configuration
    std::string m_pool_user;
    
    // Worker management
    mutable std::mutex m_miners_mutex;
    std::unordered_map<uint8_t, std::unique_ptr<ESP32MinerState>> m_miners;
    std::vector<uint8_t> m_i2c_slaves;
    
    // Pre-allocated buffers
    uint8_t m_header_buffer[HEADER_BUFFER_SIZE];
    uint8_t m_hash_buffer[32];
    
    // Main loops
    void mainLoop();
    void i2cLoop();
    void stratumLoop();
    void webLoop();
    void statsLoop();
    
    // Mining operations
    void distributeJob();
    void collectResults();
    void verifyAndSubmitShare(uint32_t nonce, uint8_t slave_addr);
    void updateHashrate();
    
    // Job processing
    bool prepareJobHeader();
};

#endif // MINING_COORDINATOR_H
