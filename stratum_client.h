#ifndef STRATUM_CLIENT_H
#define STRATUM_CLIENT_H

#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>
#include <thread>

class StratumClient {
public:
    StratumClient();
    StratumClient(const std::string& host, int port, const std::string& user, const std::string& pass, double difficulty = 5000.0);
    ~StratumClient();

    bool connect();
    bool reconnect();
    void disconnect();
    void update();

    bool hasNewJob() { return m_new_job_available.load(); }
    void clearNewJobFlag() { m_new_job_available.store(false); }
    
    const uint8_t* getHeader() { 
        std::lock_guard<std::recursive_mutex> lock(m_data_mutex);
        return m_current_header; 
    }
    
    double getDifficulty() { return m_difficulty.load(); }
    std::string getJobId() { 
        std::lock_guard<std::recursive_mutex> lock(m_data_mutex);
        return m_job_id; 
    }
    
    // NEW: Getters for share submission
    std::string getExtranonce2() {
        std::lock_guard<std::recursive_mutex> lock(m_data_mutex);
        return m_extranonce2;
    }
    std::string getNtime() {
        std::lock_guard<std::recursive_mutex> lock(m_data_mutex);
        return m_current_ntime;
    }

    bool submitShare(uint32_t nonce, const std::string& job_id);
    bool submitShare(const std::string& job_id, const std::string& en2, const std::string& ntime, uint32_t nonce);
    
    bool isConnected() const { return m_socket_fd >= 0 && m_connected.load(); }
    
    // Share tracking
    uint64_t getAcceptedCount() const { return m_shares_accepted.load(); }
    uint64_t getRejectedCount() const { return m_shares_rejected.load(); }

private:
    double m_suggested_difficulty{5000.0};
    std::string m_host;
    int m_port;
    std::string m_user;
    std::string m_pass;
    int m_socket_fd;
    std::atomic<bool> m_running;
    std::atomic<bool> m_connected{false};
    std::thread m_rx_thread;
    std::recursive_mutex m_data_mutex;
    uint8_t m_current_header[80];
    
    std::string m_job_id;
    std::string m_extranonce1;
    std::string m_extranonce2;
    std::string m_current_ntime;
    std::string m_current_nbits;
    std::string m_version;
    std::string m_prev_hash;
    std::string m_merkle_root;
    std::vector<std::string> m_merkle_branches;
    
    std::atomic<double> m_difficulty;
    std::atomic<bool> m_new_job_available;
    int m_extranonce2_size;
    uint32_t m_extranonce2_counter;
    
    // Share tracking
    std::atomic<uint64_t> m_shares_accepted{0};
    std::atomic<uint64_t> m_shares_rejected{0};

    void startReceiveThread();
    void stopReceiveThread();
    void receiveLoop();
    void parseLine(const std::string& line);
    bool sendMessage(const std::string& message);
    void buildHeader();
    std::string generateExtranonce2();
    std::vector<std::string> parseStratumParams(const std::string& params_str);
    std::vector<std::string> extractMerkleBranches(const std::string& array_str);
};

#endif