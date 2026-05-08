#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include "pi_miner_config.h"
#include "sha256_miner.h"
#include "mining_worker.h"
#include "stratum_client.h"
#include "web_server.h"
#include "logger.h"
#include <fstream>
#include <sstream>

std::atomic<double> g_cpu_temp{0.0};
std::atomic<int> g_cpu_freq{0};
std::atomic<bool> g_hashrate_warning{false};
std::atomic<bool> g_running{true};
std::atomic<uint64_t> g_total_hashes{0};
std::atomic<uint64_t> g_shares_found{0};
std::atomic<uint64_t> g_shares_submitted{0};
std::atomic<uint64_t> g_shares_accepted{0};
std::atomic<uint64_t> g_shares_rejected{0};
std::atomic<double> g_hashrate{0.0};
uint64_t g_start_time = 0;

std::deque<double> g_hashrate_samples;
std::mutex g_hashrate_mutex;
std::vector<std::unique_ptr<MiningWorker>> g_workers;
std::unique_ptr<StratumClient> g_stratum;
std::unique_ptr<WebServer> g_web_server;

std::mutex g_share_mutex;
std::condition_variable g_share_cv;

struct PendingShare {
    uint32_t nonce;
    std::string job_id;
    std::string extranonce2;
    std::string ntime;
    uint64_t job_version;
};
std::vector<PendingShare> g_pending_shares;

std::mutex g_job_mutex;
std::string g_current_job_id;
std::string g_current_extranonce2;
std::string g_current_ntime;
std::atomic<uint64_t> g_current_job_version{0};

void systemMonitorThread() {
    LOG_INFO("SystemMonitor thread started");
    uint64_t last_check = 0;
    double last_hashrate = 0.0;
    int low_hashrate_counter = 0;
    
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(CPU_FREQ_CHECK_MS));
        
        std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");
        if (temp_file.is_open()) {
            int temp_raw;
            temp_file >> temp_raw;
            g_cpu_temp.store(temp_raw / 1000.0, std::memory_order_relaxed);
            
            if (g_cpu_temp.load(std::memory_order_relaxed) > THERMAL_THROTTLE_TEMP) {
                LOG_WARN("High temperature detected: " + std::to_string(g_cpu_temp.load()) + "°C");
            }
        }
        
        std::ifstream freq_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
        if (freq_file.is_open()) {
            int freq_khz;
            freq_file >> freq_khz;
            g_cpu_freq.store(freq_khz, std::memory_order_relaxed);
        }
        
        double current_hashrate = g_hashrate.load(std::memory_order_relaxed);
        if (last_hashrate > 0 && current_hashrate < last_hashrate * 0.7) {
            low_hashrate_counter++;
            if (low_hashrate_counter >= 3) {
                LOG_WARN("Hashrate dropped significantly: " + 
                    std::to_string(current_hashrate/1000000) + " MH/s");
                g_hashrate_warning.store(true);
            }
        } else {
            low_hashrate_counter = 0;
            g_hashrate_warning.store(false);
        }
        last_hashrate = current_hashrate;
        
        static int log_counter = 0;
        if (++log_counter >= 10) {
            log_counter = 0;
            std::stringstream ss;
            ss << "System: " << g_cpu_temp.load() << "°C | " 
               << g_cpu_freq.load()/1000 << " MHz | "
               << (g_hashrate.load()/1000000) << " MH/s | Workers: " << NUM_MINING_THREADS;
            LOG_INFO(ss.str());
        }
    }
    LOG_INFO("SystemMonitor thread stopped");
}

void signalHandler(int signum) {
    LOG_INFO("Received signal " + std::to_string(signum) + ", shutting down...");
    g_running.store(false, std::memory_order_release);
}

void shareSubmissionThread() {
    LOG_INFO("ShareSubmission thread started");
    
    { std::lock_guard<std::mutex> lock(g_share_mutex); g_pending_shares.clear(); }
    
    while (g_running.load(std::memory_order_acquire)) {
        std::vector<PendingShare> shares_to_submit;
        {
            std::unique_lock<std::mutex> lock(g_share_mutex);
            g_share_cv.wait_for(lock, std::chrono::milliseconds(10), []{
                return !g_pending_shares.empty() || !g_running.load(std::memory_order_acquire);
            });
            if (!g_pending_shares.empty()) {
                shares_to_submit = std::move(g_pending_shares);
                g_pending_shares.clear();
            }
        }
        for (auto& share : shares_to_submit) {
            if (g_stratum && g_stratum->isConnected()) {
                if (g_stratum->submitShare(share.job_id, share.extranonce2, share.ntime, share.nonce)) {
                    g_shares_submitted.fetch_add(1, std::memory_order_relaxed);
                } else {
                    g_shares_rejected.fetch_add(1, std::memory_order_relaxed);
                    LOG_ERROR("Submit failed! Nonce: " + std::to_string(share.nonce));
                }
            } else {
                g_shares_rejected.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    LOG_INFO("ShareSubmission thread stopped");
}

void miningCoordinatorThread() {
    LOG_INFO("Coordinator thread started");
    uint64_t last_hashrate_update = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t last_hash_count = 0;
    uint64_t current_job_version = 0;

    for (auto &worker : g_workers) worker->start();

    while (g_running.load(std::memory_order_acquire)) {
        if (g_stratum && g_stratum->hasNewJob()) {
            const uint8_t *header = g_stratum->getHeader();
            std::string job_id = g_stratum->getJobId();
            double difficulty = g_stratum->getDifficulty();
            std::string extranonce2 = g_stratum->getExtranonce2();
            std::string ntime = g_stratum->getNtime();

            if (header != nullptr && !job_id.empty()) {
                LOG_INFO("New job: " + job_id + " en2=" + extranonce2);
                current_job_version++;
                g_current_job_version.store(current_job_version, std::memory_order_release);
                
                {
                    std::lock_guard<std::mutex> lock(g_job_mutex);
                    g_current_job_id = job_id;
                    g_current_extranonce2 = extranonce2;
                    g_current_ntime = ntime;
                }

                // Distribute nonce range across all workers
                uint32_t total_nonces = NONCES_PER_THREAD * g_workers.size();
                uint32_t nonces_per_worker = total_nonces / g_workers.size();
                
                for (size_t i = 0; i < g_workers.size(); i++) {
                    uint32_t nonce_start = i * nonces_per_worker;
                    uint32_t nonce_end = (i + 1) * nonces_per_worker;
                    if (nonce_start % 4 != 0) nonce_start += (4 - (nonce_start % 4));
                    if (nonce_end % 4 != 0) nonce_end += (4 - (nonce_end % 4));
                    g_workers[i]->updateJob(header, nonce_start, nonce_end, difficulty, current_job_version);
                }
                g_stratum->clearNewJobFlag();
            }
        }

        // Check all workers for found shares
        for (auto &worker : g_workers) {
            if (worker->hasFoundShare()) {
                uint32_t nonce = worker->getFoundNonce();
                uint64_t job_ver = worker->getJobVersion();
                g_shares_found.fetch_add(1, std::memory_order_relaxed);

                std::string job_id, extranonce2, ntime;
                {
                    std::lock_guard<std::mutex> lock(g_job_mutex);
                    job_id = g_current_job_id;
                    extranonce2 = g_current_extranonce2;
                    ntime = g_current_ntime;
                }

                {
                    std::lock_guard<std::mutex> lock(g_share_mutex);
                    g_pending_shares.push_back({nonce, job_id, extranonce2, ntime, job_ver});
                }
                g_share_cv.notify_one();
                worker->clearFoundNonce();
            }
        }

        // Update hashrate
        uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t time_delta = current_time - last_hashrate_update;

        if (time_delta >= HASHRATE_UPDATE_MS) {
            uint64_t total_hashes = 0;
            for (auto &worker : g_workers) total_hashes += worker->getHashCount();

            if (total_hashes >= last_hash_count && time_delta > 0) {
                uint64_t hash_delta = total_hashes - last_hash_count;
                double instant_hashrate = static_cast<double>(hash_delta) / (time_delta / 1000.0);

                if (instant_hashrate > 0 && instant_hashrate < 1000000000.0) {
                    std::lock_guard<std::mutex> lock(g_hashrate_mutex);
                    g_hashrate_samples.push_back(instant_hashrate);
                    if (g_hashrate_samples.size() > HASHRATE_SAMPLE_COUNT) 
                        g_hashrate_samples.pop_front();
                    double sum = 0.0;
                    for (double sample : g_hashrate_samples) sum += sample;
                    g_total_hashes.store(total_hashes, std::memory_order_relaxed);
                    g_hashrate.store(sum / g_hashrate_samples.size(), std::memory_order_relaxed);
                }
            }
            last_hash_count = total_hashes;
            last_hashrate_update = current_time;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (auto &worker : g_workers) worker->stop();
    LOG_INFO("Coordinator thread stopped");
}

void webStatsThread() {
    LOG_INFO("WebStats thread started");
    while (g_running.load(std::memory_order_acquire)) {
        WebServerStats stats;
        stats.hashrate = g_hashrate.load(std::memory_order_relaxed);
        stats.shares_accepted = g_shares_accepted.load(std::memory_order_relaxed);
        stats.shares_rejected = g_shares_rejected.load(std::memory_order_relaxed);
        stats.total_nonces = g_total_hashes.load(std::memory_order_relaxed);
        stats.uptime_seconds = (std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count() - g_start_time) / 1000;
        stats.difficulty = g_stratum ? g_stratum->getDifficulty() : 0;
        stats.pool_host = DEFAULT_POOL_HOST;
        stats.pool_connected = (g_stratum && g_stratum->isConnected());
        stats.esp32_count = NUM_MINING_THREADS;
        g_web_server->updateStats(stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(WEB_REFRESH_MS));
    }
    LOG_INFO("WebStats thread stopped");
}

void shareResponseThread() {
    LOG_INFO("ShareResponse thread started");
    uint64_t last_accepted = 0, last_rejected = 0;
    while (g_running.load(std::memory_order_acquire)) {
        uint64_t accepted = g_stratum ? g_stratum->getAcceptedCount() : 0;
        uint64_t rejected = g_stratum ? g_stratum->getRejectedCount() : 0;
        if (accepted > last_accepted) { 
            g_shares_accepted.fetch_add(accepted - last_accepted, std::memory_order_relaxed); 
            last_accepted = accepted; 
        }
        if (rejected > last_rejected) { 
            g_shares_rejected.fetch_add(rejected - last_rejected, std::memory_order_relaxed); 
            last_rejected = rejected; 
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOG_INFO("ShareResponse thread stopped");
}

void keepaliveThread();

void statusReportThread() {
    LOG_INFO("StatusReport thread started");
    int counter = 0;
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (++counter % 30 == 0) {
            uint64_t total = g_shares_accepted.load(std::memory_order_relaxed) + 
                            g_shares_rejected.load(std::memory_order_relaxed);
            double rate = total > 0 ? (100.0 * g_shares_accepted.load(std::memory_order_relaxed) / total) : 100.0;
            std::stringstream ss;
            ss << "Hashrate: " << (g_hashrate.load(std::memory_order_relaxed) / 1000000.0) 
               << " MH/s | Shares: " << g_shares_accepted.load(std::memory_order_relaxed) << "/" 
               << g_shares_rejected.load(std::memory_order_relaxed) << " (" << rate << "%) | Workers: " 
               << NUM_MINING_THREADS;
            LOG_INFO(ss.str());
        }
    }
    LOG_INFO("StatusReport thread stopped");
}

void connectionMonitorThread() {
    LOG_INFO("ConnectionMonitor thread started");
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (g_stratum && !g_stratum->isConnected()) {
            LOG_INFO("Connection lost, reconnecting...");
            g_stratum->reconnect();
        }
    }
    LOG_INFO("ConnectionMonitor thread stopped");
}

int main(int argc, char **argv) {
    Logger::getInstance().init("miner.log", false);
    LOG_INFO("=== Pi Bitcoin Miner Starting (Multi-Worker Optimized) ===");
    
    std::string pool_host = DEFAULT_POOL_HOST;
    int pool_port = DEFAULT_POOL_PORT;
    std::string pool_user = DEFAULT_POOL_USER;
    std::string pool_pass = DEFAULT_POOL_PASS;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--pool-host" && i + 1 < argc) pool_host = argv[++i];
        else if (arg == "--pool-port" && i + 1 < argc) pool_port = std::stoi(argv[++i]);
        else if (arg == "--pool-user" && i + 1 < argc) pool_user = argv[++i];
        else if (arg == "--pool-pass" && i + 1 < argc) pool_pass = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            return 0;
        }
    }

    g_start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    LOG_INFO("Pool: " + pool_host + ":" + std::to_string(pool_port));
    LOG_INFO("User: " + pool_user);
    LOG_INFO("Mining Cores: " + std::to_string(MINING_CORES) + " (cores " + 
             std::to_string(MINING_CORE_START) + "-" + std::to_string(MINING_CORE_START + MINING_CORES - 1) + ")");
    LOG_INFO("Workers: " + std::to_string(NUM_MINING_THREADS) + " (" + 
             std::to_string(WORKERS_PER_CORE) + " per core)");
    LOG_INFO("Dashboard: http://localhost:" + std::to_string(WEB_PORT));

    // Set main thread to core 0 (system/stratum)
    struct sched_param sp; 
    sp.sched_priority = 30;  // Highest priority for main thread
    cpu_set_t main_cpuset;
    CPU_ZERO(&main_cpuset);
    CPU_SET(0, &main_cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(main_cpuset), &main_cpuset);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

    // Create workers distributed across mining cores
    for (int i = 0; i < NUM_MINING_THREADS; i++) {
        int core_id = MINING_CORE_START + (i % MINING_CORES);
        g_workers.push_back(std::make_unique<MiningWorker>(i, core_id));
    }

    g_stratum = std::make_unique<StratumClient>(pool_host, pool_port, pool_user, pool_pass, DEFAULT_DIFFICULTY);
    g_web_server = std::make_unique<WebServer>(WEB_PORT);
    g_web_server->start();

    if (!g_stratum->connect()) {
        LOG_ERROR_ALWAYS("Failed to connect to pool!");
        Logger::getInstance().close();
        return 1;
    }

    LOG_INFO("Waiting for first job...");
    int wait_count = 0;
    while (!g_stratum->hasNewJob() && wait_count < 30) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
    }
    if (!g_stratum->hasNewJob()) {
        LOG_ERROR_ALWAYS("No job received from pool!");
        return 1;
    }
    LOG_INFO("Job received, starting " + std::to_string(NUM_MINING_THREADS) + " workers...");

    std::thread share_submit_thread(shareSubmissionThread);
    std::thread share_response_thread(shareResponseThread);
    std::thread coordinator_thread(miningCoordinatorThread);
    std::thread web_stats_thread(webStatsThread);
    std::thread status_report_thread(statusReportThread);
    std::thread connection_monitor_thread(connectionMonitorThread);
    std::thread keepalive_thread(keepaliveThread);
    std::thread system_monitor_thread(systemMonitorThread);

    try {
        while (g_running.load(std::memory_order_acquire)) 
            std::this_thread::sleep_for(std::chrono::seconds(1));
    } catch (const std::exception &e) {
        LOG_ERROR_ALWAYS("Exception: " + std::to_string(e.what()));
    }

    g_running.store(false, std::memory_order_release);
    g_share_cv.notify_all();

    if (share_submit_thread.joinable()) share_submit_thread.join();
    if (share_response_thread.joinable()) share_response_thread.join();
    if (coordinator_thread.joinable()) coordinator_thread.join();
    if (web_stats_thread.joinable()) web_stats_thread.join();
    if (status_report_thread.joinable()) status_report_thread.join();
    if (connection_monitor_thread.joinable()) connection_monitor_thread.join();
    if (keepalive_thread.joinable()) keepalive_thread.join();
    if (system_monitor_thread.joinable()) system_monitor_thread.join();

    g_stratum->disconnect();
    g_web_server->stop();
    LOG_INFO("=== Mining Stopped ===");
    Logger::getInstance().close();
    return 0;
}

void keepaliveThread() {
    LOG_INFO("Keepalive thread started");
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (g_stratum) g_stratum->update();
    }
    LOG_INFO("Keepalive thread stopped");
}
