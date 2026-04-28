#include "stratum_client.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iomanip>
#include <sstream>

// Include nlohmann/json.hpp if you're using it
#include "json.hpp"
using json = nlohmann::json;

StratumClient::StratumClient(const std::string& host, int port, const std::string& user, const std::string& pass)
    : m_host(host), m_port(port), m_user(user), m_pass(pass), m_socket_fd(-1), 
      m_connected(false), m_running(false), m_has_new_job(false), 
      m_difficulty(1.0), m_message_id(1) {
    std::memset(m_receive_buffer, 0, sizeof(m_receive_buffer));
}

StratumClient::~StratumClient() { 
    stopReceiveThread(); 
    disconnect(); 
}

bool StratumClient::connect() {
    struct hostent* server = gethostbyname(m_host.c_str());
    if (server == nullptr) {
        std::cerr << "[STRATUM] DNS lookup failed for " << m_host << std::endl;
        return false;
    }

    m_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd < 0) {
        std::cerr << "[STRATUM] Socket creation failed: " << strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(m_port);

    if (::connect(m_socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "[STRATUM] Connection failed to " << m_host << ":" << m_port << std::endl;
        close(m_socket_fd);
        m_socket_fd = -1;
        return false;
    }

    m_connected.store(true, std::memory_order_relaxed);
    std::cout << "[STRATUM] Connected to " << m_host << ":" << m_port << std::endl;
    return true;
}

void StratumClient::disconnect() {
    if (m_socket_fd >= 0) {
        close(m_socket_fd);
        m_socket_fd = -1;
    }
    m_connected.store(false, std::memory_order_relaxed);
}

void StratumClient::startReceiveThread() {
    if (m_running.load()) return;
    m_running.store(true, std::memory_order_relaxed);
    m_receive_thread = std::thread(&StratumClient::receiveLoop, this);
}

void StratumClient::stopReceiveThread() {
    m_running.store(false, std::memory_order_relaxed);
    if (m_receive_thread.joinable()) {
        m_receive_thread.join();
    }
}

// FIXED: Returns bool instead of std::string
bool StratumClient::sendMessage(const std::string& message) {
    if (!m_connected.load(std::memory_order_relaxed)) {
        std::cerr << "[STRATUM] Not connected, cannot send message" << std::endl;
        return false;
    }
    
    std::string full_msg = message + "\n";
    ssize_t sent = send(m_socket_fd, full_msg.c_str(), full_msg.length(), 0);
    
    if (sent < 0) {
        std::cerr << "[STRATUM] Send failed: " << strerror(errno) << std::endl;
        m_connected.store(false, std::memory_order_relaxed);
        return false;
    }
    
    return static_cast<size_t>(sent) == full_msg.length();
}

bool StratumClient::subscribe() {
    json request = {
        {"id", m_message_id.fetch_add(1)},
        {"method", "mining.subscribe"},
        {"params", json::array({ "PiMiner/1.0" })}
    };
    
    std::cout << "[STRATUM] Subscribing..." << std::endl;
    bool success = sendMessage(request.dump());
    if (success) {
        std::cout << "[STRATUM] Subscribe message sent" << std::endl;
    }
    return success;
}

bool StratumClient::authorize() {
    json request = {
        {"id", m_message_id.fetch_add(1)},
        {"method", "mining.authorize"},
        {"params", {m_user, m_pass}}
    };
    
    std::cout << "[STRATUM] Authorizing user: " << m_user << std::endl;
    bool success = sendMessage(request.dump());
    if (success) {
        std::cout << "[STRATUM] Authorize message sent" << std::endl;
    }
    return success;
}

bool StratumClient::submitShare(const std::string& job_id, const std::string& extranonce2, 
                               const std::string& ntime, uint32_t nonce) {
    std::stringstream ss;
    uint32_t le_nonce = ((nonce & 0xFF) << 24) | 
                        ((nonce & 0xFF00) << 8) | 
                        ((nonce & 0xFF0000) >> 8) | 
                        ((nonce >> 24) & 0xFF);
    ss << std::hex << std::setfill('0') << std::setw(8) << le_nonce;
    
    json request = {
        {"id", m_message_id.fetch_add(1)},
        {"method", "mining.submit"},
        {"params", {m_user, job_id, extranonce2, ntime, ss.str()}}
    };
    
    bool success = sendMessage(request.dump());
    if (success) {
        std::cout << "[STRATUM] Share submitted: nonce=0x" << ss.str() << std::endl;
    } else {
        std::cerr << "[STRATUM] Failed to submit share" << std::endl;
    }
    return success;
}

void StratumClient::receiveLoop() {
    char chunk[4096];
    std::string tcp_buffer; 

    while (m_running.load(std::memory_order_relaxed)) {
        int bytes = recv(m_socket_fd, chunk, sizeof(chunk) - 1, 0);
        
        if (bytes <= 0) {
            if (bytes < 0) {
                std::cerr << "[STRATUM] Receive error: " << strerror(errno) << std::endl;
            } else {
                std::cout << "[STRATUM] Connection closed by server" << std::endl;
            }
            m_connected.store(false, std::memory_order_relaxed);
            m_running.store(false, std::memory_order_relaxed);
            break;
        }

        chunk[bytes] = '\0';
        tcp_buffer += chunk;

        size_t pos;
        while ((pos = tcp_buffer.find('\n')) != std::string::npos) {
            std::string line = tcp_buffer.substr(0, pos);
            tcp_buffer.erase(0, pos + 1);
            
            if (!line.empty()) {
                parseLine(line);
            }
        }
    }
}

void StratumClient::parseLine(const std::string& line) {
    try {
        auto j = json::parse(line);
        
        // Handle Notifications (Difficulty, Jobs)
        if (j.contains("method")) {
            std::string method = j["method"];
            
            if (method == "mining.set_difficulty") {
                m_difficulty.store(j["params"][0].get<double>(), std::memory_order_relaxed);
                std::cout << "[STRATUM] New difficulty: " << j["params"][0].get<double>() << std::endl;
            } 
            else if (method == "mining.notify") {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto p = j["params"];
                m_current_job.job_id = p[0].get<std::string>();
                m_current_job.prev_block_hash = p[1].get<std::string>();
                m_current_job.coinb1 = p[2].get<std::string>();
                m_current_job.coinb2 = p[3].get<std::string>();
                m_current_job.merkle_branches = p[4].get<std::vector<std::string>>();
                m_current_job.version = p[5].get<std::string>();
                m_current_job.nbits = p[6].get<std::string>();
                m_current_job.ntime = p[7].get<std::string>();
                m_current_job.clean_jobs = p[8].get<bool>();
                
                m_has_new_job.store(true, std::memory_order_relaxed);
                std::cout << "[STRATUM] New job received: " << m_current_job.job_id << std::endl;
            }
        }
        // Handle RPC Responses (Subscribe/Authorize success)
        else if (j.contains("result") && !j["result"].is_null()) {
            if (j.contains("id")) {
                unsigned long id = j["id"].get<unsigned long>();
                if (id == 1) { // Likely Subscribe Response
                    auto res = j["result"];
                    if (res.is_array() && res.size() >= 3) {
                        m_subscribe_info.extranonce1 = res[1].get<std::string>();
                        m_subscribe_info.extranonce2_size = res[2].get<int>();
                        std::cout << "[STRATUM] Subscribe OK, extranonce1: " << m_subscribe_info.extranonce1 << std::endl;
                    }
                }
            }
        }
        // Handle errors
        else if (j.contains("error") && !j["error"].is_null()) {
            std::cerr << "[STRATUM] Error: " << j["error"].dump() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[STRATUM] Parse error: " << e.what() << " for line: " << line << std::endl;
    }
}

MiningJob StratumClient::getCurrentJob() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_job;
}

MiningSubscribe StratumClient::getSubscribeInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_subscribe_info;
}

double StratumClient::getEffectiveDifficulty() const {
    return m_difficulty.load(std::memory_order_relaxed);
}
