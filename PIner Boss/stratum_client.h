#ifndef STRATUM_CLIENT_H
#define STRATUM_CLIENT_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <sys/socket.h>
#include <iomanip>
#include <sstream>

#include "config.h"

struct MiningJob {
    std::string job_id;
    std::string prev_block_hash;
    std::string coinb1;
    std::string coinb2;
    std::vector<std::string> merkle_branches;
    std::string version;
    std::string nbits;
    std::string ntime;
    bool clean_jobs = false;
};

struct MiningSubscribe {
    std::string sub_details;
    std::string extranonce1;
    int extranonce2_size = 0;
    std::string wName;
};

class StratumClient {
public:
    StratumClient(const std::string& host, int port, 
                 const std::string& user, const std::string& pass);
    ~StratumClient();
    
    // Connection
    bool connect();
    void disconnect();
    bool isConnected() const { return m_connected.load(std::memory_order_relaxed); }
    std::string getHost() const { return m_host; }
    
    // Stratum protocol
    bool subscribe();
    bool authorize();
    bool submitShare(const std::string& job_id, const std::string& extranonce2,
                    const std::string& ntime, uint32_t nonce);
    
    // Job access
    MiningJob getCurrentJob() const;
    MiningSubscribe getSubscribeInfo() const;
    double getEffectiveDifficulty() const;
    
    // Job notification
    bool hasNewJob() const { return m_has_new_job.load(std::memory_order_relaxed); }
    void clearNewJobFlag() { m_has_new_job.store(false, std::memory_order_relaxed); }
    
    // Receive thread
    void startReceiveThread();
    void stopReceiveThread();

private:
    // Configuration
    std::string m_host;
    int m_port;
    std::string m_user;
    std::string m_pass;
    
    // Socket
    int m_socket_fd;
    std::atomic<bool> m_connected;
    
    // Receive thread
    std::atomic<bool> m_running;
    std::thread m_receive_thread;
    
    // Job state (protected by mutex)
    mutable std::mutex m_mutex;
    MiningSubscribe m_subscribe_info;
    MiningJob m_current_job;
    
    // Notifications
    std::atomic<bool> m_has_new_job;
    std::atomic<double> m_difficulty;
    std::atomic<unsigned long> m_message_id;
    
    // Receive buffer (pre-allocated)
    char m_receive_buffer[8192];
    
    // Internal methods
    void receiveLoop();
    void parseLine(const std::string& line);
    bool sendMessage(const std::string& message);  // FIXED: Returns bool, not string
};

#endif // STRATUM_CLIENT_H
