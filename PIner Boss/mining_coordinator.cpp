#include "mining_coordinator.h"
#include "terminal_ui.h"
#include <iostream>
#include <chrono>
#include <sys/time.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <unistd.h>

extern TerminalUI g_ui;

MiningCoordinator::MiningCoordinator(const std::string& pool_host, int pool_port,
                                     const std::string& pool_user, const std::string& pool_pass)
    : m_i2c_master(std::make_unique<I2CMaster>())
    , m_stratum(std::make_unique<StratumClient>(pool_host, pool_port, pool_user, pool_pass))
    , m_web_server(std::make_unique<WebServer>(8080))
    , m_running(false)
    , m_current_job_id(0)
    , m_pool_user(pool_user) {
    std::memset(m_header_buffer, 0, sizeof(m_header_buffer));
    std::memset(m_hash_buffer, 0, sizeof(m_hash_buffer));
}

MiningCoordinator::~MiningCoordinator() {
    stop();
}

bool MiningCoordinator::addI2CSlave(uint8_t address) {
    std::lock_guard<std::mutex> lock(m_miners_mutex);
    
    if (m_miners.find(address) != m_miners.end()) {
        return false;
    }
    
    auto state = std::make_unique<ESP32MinerState>(address);
    state->last_activity.store(getCurrentTimeMs());
    
    m_miners[address] = std::move(state);
    m_i2c_slaves.push_back(address);
    m_stats.active_workers.fetch_add(1);
    
    std::cout << "[I2C] Added slave 0x" << std::hex << (int)address << std::dec << std::endl;
    return true;
}

bool MiningCoordinator::removeI2CSlave(uint8_t address) {
    std::lock_guard<std::mutex> lock(m_miners_mutex);
    
    auto it = m_miners.find(address);
    if (it == m_miners.end()) return false;
    
    m_miners.erase(it);
    m_i2c_slaves.erase(std::remove(m_i2c_slaves.begin(), m_i2c_slaves.end(), address), 
                       m_i2c_slaves.end());
    m_stats.active_workers.fetch_sub(1);
    
    return true;
}

size_t MiningCoordinator::getWorkerCount() const {
    std::lock_guard<std::mutex> lock(m_miners_mutex);
    return m_i2c_slaves.size();
}

bool MiningCoordinator::start() {
    std::cout << "[COORD] Starting mining coordinator..." << std::endl;
    
    if (!m_i2c_master->start()) {
        std::cerr << "[COORD] I2C initialization failed!" << std::endl;
        return false;
    }
    
    auto addresses = m_i2c_master->scan();
    for (uint8_t addr : addresses) {
        addI2CSlave(addr);
    }
    
    std::cout << "[COORD] Found " << addresses.size() << " I2C slaves" << std::endl;
    
    if (!m_web_server->start()) {
        std::cerr << "[COORD] Web server failed to start!" << std::endl;
        return false;
    }
    
    if (m_stratum->connect()) {
        m_stratum->subscribe();
        m_stratum->authorize();
        m_stratum->startReceiveThread();
    } else {
        std::cerr << "[COORD] Failed to connect to pool" << std::endl;
    }
    
    m_running.store(true);
    m_stats.start_time.store(getCurrentTimeMs());
    
    m_main_loop_thread = std::thread(&MiningCoordinator::mainLoop, this);
    m_i2c_loop_thread = std::thread(&MiningCoordinator::i2cLoop, this);
    m_stratum_loop_thread = std::thread(&MiningCoordinator::stratumLoop, this);
    m_web_loop_thread = std::thread(&MiningCoordinator::webLoop, this);
    m_stats_loop_thread = std::thread(&MiningCoordinator::statsLoop, this);
    
    std::cout << "[COORD] Mining coordinator online" << std::endl;
    return true;
}

void MiningCoordinator::mainLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        if (m_stratum->hasNewJob()) {
            {
                std::lock_guard<std::mutex> lock(m_job_mutex);
                m_current_job = m_stratum->getCurrentJob();
                
                try {
                    m_current_job_id.store(
                        static_cast<uint8_t>(std::stoi(m_current_job.job_id, nullptr, 16) & 0xFF));
                } catch (...) {
                    m_current_job_id.fetch_add(1);
                }
                
                float pool_diff = static_cast<float>(m_stratum->getEffectiveDifficulty());
                m_stats.difficulty.store(std::min(pool_diff, MAX_DIFFICULTY));
            }
            
            m_stratum->clearNewJobFlag();
            
            if (prepareJobHeader()) {
                std::thread([this]() { distributeJob(); }).detach();
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(MAIN_LOOP_INTERVAL_MS));
    }
}

bool MiningCoordinator::prepareJobHeader() {
    std::lock_guard<std::mutex> lock(m_job_mutex);
    
    if (m_current_job.job_id.empty()) return false;
    
    MiningSubscribe sub = m_stratum->getSubscribeInfo();
    std::string merkle = SHA256Utils::calculateMerkleRoot(
        m_current_job.coinb1, sub.extranonce1, "00000000", 
        m_current_job.coinb2, m_current_job.merkle_branches);
    
    SHA256Utils::buildBlockHeader(
        m_current_job.version, m_current_job.prev_block_hash, merkle,
        m_current_job.ntime, m_current_job.nbits, 0, m_header_buffer);
    
    return true;
}

void MiningCoordinator::distributeJob() {
    std::vector<uint8_t> active_slaves;
    float diff;
    uint8_t job_id;
    
    {
        std::lock_guard<std::mutex> lock(m_miners_mutex);
        for (const auto& [addr, state] : m_miners) {
            if (state->active.load(std::memory_order_relaxed)) {
                active_slaves.push_back(addr);
            }
        }
    }
    
    if (active_slaves.empty()) {
        std::cerr << "[JOB] No active slaves!" << std::endl;
        g_ui.addLogMessage("[ERROR] No active slaves!");
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_job_mutex);
        diff = m_stats.difficulty.load(std::memory_order_relaxed);
        job_id = m_current_job_id.load(std::memory_order_relaxed);
    }
    
    std::cout << "[JOB] " << (int)job_id << " → " 
              << active_slaves.size() << " slaves" << std::endl;
    
    uint32_t base_nonce = 0;
    uint32_t nonces_per_slave = 0x10000;
    
    bool all_success = true;
    
    for (size_t i = 0; i < active_slaves.size(); i++) {
        uint32_t slave_nonce_start = base_nonce + (i * nonces_per_slave);
        
        JobI2cRequest request;
        request.cmd = I2C_CMD_FEED;
        request.id = job_id;
        request.nonce_start = slave_nonce_start;
        request.difficulty = diff;
        std::memcpy(request.buffer, m_header_buffer, 76);
        request.crc = 0;
        request.crc = CryptoUtils::crc8(&request, sizeof(request));
        
        bool success = m_i2c_master->writeBytes(active_slaves[i], 
                                                 reinterpret_cast<uint8_t*>(&request),
                                                 sizeof(request));
        
        if (!success) {
            std::cerr << "[JOB] Failed: 0x" << std::hex 
                      << (int)active_slaves[i] << std::dec << std::endl;
            all_success = false;
        }
        
        usleep(500);
    }
    
    if (all_success) {
        g_ui.addLogMessage("Job " + std::to_string(job_id) + " sent");
    }
}

void MiningCoordinator::collectResults() {
    std::vector<uint8_t> active_slaves;
    
    {
        std::lock_guard<std::mutex> lock(m_miners_mutex);
        for (const auto& [addr, state] : m_miners) {
            if (state->active.load(std::memory_order_relaxed)) {
                active_slaves.push_back(addr);
            }
        }
    }
    
    if (active_slaves.empty()) {
        static uint32_t warn_count = 0;
        warn_count++;
        if (warn_count % 20 == 0) {
            std::cerr << "[I2C] WARNING: No active slaves! Count=" 
                      << warn_count << std::endl;
        }
        return;
    }
    
    // DEBUG: Show slave count
    static uint32_t harvest_count = 0;
    harvest_count++;
    if (harvest_count % 20 == 0) {
        std::cout << "[COORD] Harvesting from " << active_slaves.size() 
                  << " slaves..." << std::endl;
    }
    
    uint32_t hashes_processed = 0;
    std::vector<uint32_t> found_nonces = m_i2c_master->harvestSlaves(
        active_slaves, m_current_job_id.load(), hashes_processed);
    
    // DEBUG: ALWAYS show harvest results
    if (harvest_count % 10 == 0) {
        std::cout << "[COORD] Harvest complete: " 
                  << found_nonces.size() << " nonces, "
                  << hashes_processed << " hashes" << std::endl;
    }
    
    // CRITICAL: Add to total stats
    if (hashes_processed > 0) {
        m_stats.total_nonces.fetch_add(hashes_processed, std::memory_order_relaxed);
    }
    
    for (uint32_t nonce : found_nonces) {
        verifyAndSubmitShare(nonce, active_slaves[0]);
    }
}


void MiningCoordinator::verifyAndSubmitShare(uint32_t nonce, uint8_t slave_addr) {
    // STEP 1: DUPLICATE NONCE CHECK
    {
        std::lock_guard<std::mutex> lock(m_nonce_mutex);
        if (m_submitted_nonces.find(nonce) != m_submitted_nonces.end()) {
            std::cout << "[SHARE] Duplicate nonce 0x" << std::hex << nonce 
                      << std::dec << " from 0x" << (int)slave_addr << " - ignoring" << std::endl;
            return;
        }
        m_submitted_nonces.insert(nonce);
        
        if (m_submitted_nonces.size() > 1000) {
            m_submitted_nonces.erase(m_submitted_nonces.begin());
        }
    }
    
    std::cout << "[SHARE] Found by 0x" << std::hex << (int)slave_addr 
              << std::dec << " - Nonce: 0x" << std::hex << nonce << std::dec << std::endl;
    
    g_ui.addLogMessage("[SHARE] 0x" + std::to_string(slave_addr) + 
                       " found nonce 0x" + std::to_string(nonce) + "!");

    // STEP 2: BUILD COMPLETE 80-BYTE BLOCK HEADER WITH NONCE
    uint8_t header[80];
    {
        std::lock_guard<std::mutex> lock(m_job_mutex);
        std::memcpy(header, m_header_buffer, 76);
        header[76] = nonce & 0xFF;
        header[77] = (nonce >> 8) & 0xFF;
        header[78] = (nonce >> 16) & 0xFF;
        header[79] = (nonce >> 24) & 0xFF;
    }
    
    // STEP 3: DOUBLE SHA256 HASH THE COMPLETE HEADER
    std::vector<uint8_t> hash = SHA256Utils::sha256d(header, 80);
    
    std::cout << "[SHARE] Hash: " << SHA256Utils::bytesToHex(hash.data(), 8) << "..." << std::endl;
    
    // STEP 4: VERIFY HASH MEETS DIFFICULTY TARGET
    double current_difficulty = m_stats.difficulty.load(std::memory_order_relaxed);
    
    if (!SHA256Utils::verifyShare(hash.data(), current_difficulty)) {
        std::cerr << "[SHARE] INVALID - Nonce 0x" << std::hex << nonce 
                  << std::dec << " does not meet difficulty " << current_difficulty << std::endl;
        
        m_stats.shares_rejected.fetch_add(1, std::memory_order_relaxed);
        g_ui.addLogMessage("[REJECT] Invalid share from 0x" + std::to_string(slave_addr));
        return;
    }
    
    std::cout << "[SHARE] VALID - Hash meets difficulty " << current_difficulty << std::endl;
    
    // STEP 5: SUBMIT SHARE TO STRATUM POOL
    bool submitted = false;
    {
        std::lock_guard<std::mutex> lock(m_job_mutex);
        
        std::string job_id = m_current_job.job_id;
        std::string ntime = m_current_job.ntime;
        std::string extranonce2 = "00000000";
        
        submitted = m_stratum->submitShare(job_id, extranonce2, ntime, nonce);
    }
    
    // STEP 6: UPDATE STATISTICS
    if (submitted) {
        m_stats.shares_accepted.fetch_add(1, std::memory_order_relaxed);
        std::cout << "[STRATUM] Share accepted! (Total: " 
                  << m_stats.shares_accepted.load() << ")" << std::endl;
        g_ui.addLogMessage("[ACCEPT] Share submitted successfully!");
    } else {
        m_stats.shares_rejected.fetch_add(1, std::memory_order_relaxed);
        std::cerr << "[STRATUM] Share rejected by pool!" << std::endl;
        g_ui.addLogMessage("[REJECT] Pool rejected share");
    }
}

void MiningCoordinator::statsLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        updateHashrate();
        std::this_thread::sleep_for(std::chrono::milliseconds(STATS_UPDATE_INTERVAL_MS));
    }
}

void MiningCoordinator::updateHashrate() {
    uint64_t elapsed = (getCurrentTimeMs() - m_stats.start_time.load()) / 1000;
    if (elapsed > 0) {
        double hashrate = static_cast<double>(m_stats.total_nonces.load()) / elapsed;
        m_stats.current_hashrate.store(hashrate, std::memory_order_relaxed);
    }
}

void MiningCoordinator::stratumLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        if (!m_stratum->isConnected()) {
            std::cout << "[STRATUM] Reconnecting..." << std::endl;
            if (m_stratum->connect()) {
                m_stratum->subscribe();
                m_stratum->authorize();
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void MiningCoordinator::i2cLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        collectResults();
        std::this_thread::sleep_for(std::chrono::milliseconds(I2C_LOOP_INTERVAL_MS));
    }
}

void MiningCoordinator::webLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        DisplayStats stats;
        updateDisplayStats(stats);
        
        WebServerStats web_stats;
        web_stats.hashrate = stats.hashrate;
        web_stats.shares_accepted = stats.shares_accepted;
        web_stats.shares_rejected = stats.shares_rejected;
        web_stats.total_nonces = stats.total_nonces;
        web_stats.uptime_seconds = stats.uptime_seconds;
        web_stats.difficulty = stats.difficulty;
        web_stats.pool_host = stats.pool_host;
        web_stats.pool_connected = stats.pool_connected;
        web_stats.esp32_count = stats.esp32_count;
        
        for (uint8_t addr : stats.esp32_addresses) {
            std::stringstream ss;
            ss << "0x" << std::uppercase << std::hex << (int)addr;
            web_stats.esp32_workers.push_back(ss.str());
        }
        
        m_web_server->updateStats(web_stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(WEB_UPDATE_INTERVAL_MS));
    }
}

void MiningCoordinator::updateDisplayStats(DisplayStats& stats) {
    MiningStatsCopy s = m_stats.getCopy();
    stats.shares_accepted = s.shares_accepted;
    stats.shares_rejected = s.shares_rejected;
    stats.total_nonces = s.total_nonces;
    stats.uptime_seconds = (getCurrentTimeMs() - s.start_time) / 1000;
    stats.hashrate = s.current_hashrate;
    stats.difficulty = s.difficulty;
    stats.pool_host = m_stratum->getHost();
    stats.pool_connected = m_stratum->isConnected();
    stats.esp32_count = getWorkerCount();
    
    std::lock_guard<std::mutex> lock(m_miners_mutex);
    stats.esp32_addresses = m_i2c_slaves;
}

uint64_t MiningCoordinator::getCurrentTimeMs() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

MiningStatsCopy MiningCoordinator::getStats() const {
    return m_stats.getCopy();
}

void MiningCoordinator::stop() {
    std::cout << "[COORD] Stopping mining coordinator..." << std::endl;
    
    m_running.store(false, std::memory_order_relaxed);
    
    // Give threads time to exit gracefully (max 3 seconds)
    auto stop_time = std::chrono::steady_clock::now();
    
    if (m_main_loop_thread.joinable()) {
        m_main_loop_thread.join();
    }
    if (m_i2c_loop_thread.joinable()) {
        m_i2c_loop_thread.join();
    }
    if (m_web_loop_thread.joinable()) {
        m_web_loop_thread.join();
    }
    if (m_stratum_loop_thread.joinable()) {
        m_stratum_loop_thread.join();
    }
    if (m_stats_loop_thread.joinable()) {
        m_stats_loop_thread.join();
    }
    
    // Stop components
    m_web_server->stop();
    m_stratum->stopReceiveThread();
    m_stratum->disconnect();
    
    auto elapsed = std::chrono::steady_clock::now() - stop_time;
    std::cout << "[COORD] Stopped in " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() 
              << "ms" << std::endl;
}
