#include "stratum_client.h"
#include "sha256_utils.h"
#include "logger.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <sched.h>

StratumClient::StratumClient() 
    : m_socket_fd(-1), m_running(false), m_connected(false), m_difficulty(4.0),
      m_new_job_available(false), m_extranonce2_size(4), m_extranonce2_counter(0),
      m_shares_accepted(0), m_shares_rejected(0) {
    std::memset(m_current_header, 0, 80);
}

StratumClient::StratumClient(const std::string& host, int port, const std::string& user, 
                              const std::string& pass, double difficulty)
    : m_host(host), m_port(port), m_user(user), m_pass(pass), m_suggested_difficulty(difficulty),
      m_socket_fd(-1), m_running(false), m_connected(false), m_difficulty(1.0),
      m_new_job_available(false), m_extranonce2_size(4), m_extranonce2_counter(0),
      m_shares_accepted(0), m_shares_rejected(0) {
    std::memset(m_current_header, 0, 80);
}

StratumClient::~StratumClient() { disconnect(); }

bool StratumClient::connect() {
    LOG_INFO("Connecting to " + m_host + ":" + std::to_string(m_port) + "...");
    
    struct hostent* server = gethostbyname(m_host.c_str());
    if (!server) {
        LOG_ERROR_ALWAYS("Failed to resolve hostname: " + m_host);
        return false;
    }

    m_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd < 0) {
        LOG_ERROR_ALWAYS("Failed to create socket");
        return false;
    }
    
    int keepalive = 1;
    setsockopt(m_socket_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    
    int tcp_nodelay = 1;
    setsockopt(m_socket_fd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));
    
    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(m_port);

    int connect_result = ::connect(m_socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (connect_result < 0) {
        LOG_ERROR_ALWAYS("Connection failed: " + std::string(strerror(errno)));
        close(m_socket_fd);
        m_socket_fd = -1;
        return false;
    }

    LOG_INFO("Connected!");
    m_connected.store(true, std::memory_order_release);
    m_running = true;
    startReceiveThread();
    usleep(100000);
    
    LOG_INFO("Sending subscribe...");
    sendMessage("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}\n");
    usleep(200000);
    
    LOG_INFO("Sending authorize...");
    std::string auth = "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"" + 
                       m_user + "\",\"" + m_pass + "\"]}\n";
    sendMessage(auth);
    usleep(200000);
    
    std::ostringstream diff_request;
    diff_request << "{\"id\":3,\"method\":\"mining.suggest_difficulty\",\"params\":[" 
                 << m_suggested_difficulty << "]}\n";
    sendMessage(diff_request.str());
    
    return true;
}

bool StratumClient::reconnect() {
    LOG_INFO("Attempting reconnection...");
    disconnect();
    usleep(1000000);
    return connect();
}

void StratumClient::disconnect() {
    m_connected.store(false, std::memory_order_release);
    m_running = false;
    stopReceiveThread();
    if (m_socket_fd >= 0) {
        shutdown(m_socket_fd, SHUT_RDWR);
        close(m_socket_fd);
        m_socket_fd = -1;
    }
    LOG_INFO("Disconnected");
}

void StratumClient::startReceiveThread() {
    m_rx_thread = std::thread(&StratumClient::receiveLoop, this);
    
    struct sched_param sp;
    sp.sched_priority = 15;
    pthread_setschedparam(m_rx_thread.native_handle(), SCHED_FIFO, &sp);
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(m_rx_thread.native_handle(), sizeof(cpuset), &cpuset);
}

void StratumClient::stopReceiveThread() {
    if (m_rx_thread.joinable()) m_rx_thread.join();
}

bool StratumClient::sendMessage(const std::string& message) {
    if (m_socket_fd < 0 || !m_connected.load(std::memory_order_acquire)) return false;
    ssize_t sent = send(m_socket_fd, message.c_str(), message.length(), MSG_NOSIGNAL);
    if (sent < 0) {
        m_connected.store(false, std::memory_order_release);
        return false;
    }
    return (sent > 0);
}

void StratumClient::receiveLoop() {
    char buffer[4096];
    std::string current_line;
    
    while (m_running) {
        int bytes = recv(m_socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            current_line += buffer;
            
            size_t pos;
            while ((pos = current_line.find('\n')) != std::string::npos) {
                std::string line = current_line.substr(0, pos);
                current_line.erase(0, pos + 1);
                
                if (!line.empty() && line[0] == '{') {
                    parseLine(line);
                }
            }
        } else if (bytes < 0) {
            LOG_ERROR_ALWAYS("Receive error: " + std::string(strerror(errno)));
            m_connected.store(false, std::memory_order_release);
            break;
        } else if (bytes == 0) {
            LOG_ERROR_ALWAYS("Connection closed by peer");
            m_connected.store(false, std::memory_order_release);
            break;
        }
        usleep(1000);
    }
}

std::vector<std::string> StratumClient::parseStratumParams(const std::string& params_str) {
    std::vector<std::string> elements;
    size_t pos = 0;
    int bracket_depth = 0;
    bool in_string = false;
    size_t element_start = 0;
    
    while (pos < params_str.length()) {
        char c = params_str[pos];
        
        if (c == '\\' && in_string) { pos++; continue; }
        if (c == '"') in_string = !in_string;
        if (c == '[' && !in_string) {
            bracket_depth++;
            if (bracket_depth == 1) element_start = pos;
        } else if (c == ']' && !in_string) {
            bracket_depth--;
            if (bracket_depth == 0) {
                elements.push_back(params_str.substr(element_start, pos - element_start + 1));
                pos++;
                continue;
            }
        }
        if (c == ',' && bracket_depth == 0 && !in_string) {
            if (pos > element_start) {
                std::string elem = params_str.substr(element_start, pos - element_start);
                size_t start = elem.find_first_not_of(" \t");
                size_t end = elem.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    elem = elem.substr(start, end - start + 1);
                    if (elem.length() >= 2 && elem[0] == '"' && elem[elem.length()-1] == '"') {
                        elem = elem.substr(1, elem.length() - 2);
                    }
                    if (!elem.empty()) elements.push_back(elem);
                }
            }
            element_start = pos + 1;
        }
        pos++;
    }
    
    if (element_start < params_str.length()) {
        std::string elem = params_str.substr(element_start);
        size_t start = elem.find_first_not_of(" \t");
        size_t end = elem.find_last_not_of(" \t");
        if (start != std::string::npos) {
            elem = elem.substr(start, end - start + 1);
            if (elem.length() >= 2 && elem[0] == '"' && elem[elem.length()-1] == '"') {
                elem = elem.substr(1, elem.length() - 2);
            }
            if (!elem.empty()) elements.push_back(elem);
        }
    }
    
    return elements;
}

std::vector<std::string> StratumClient::extractMerkleBranches(const std::string& array_str) {
    std::vector<std::string> result;
    if (array_str.empty() || array_str[0] != '[') return result;
    
    size_t pos = 1;
    while (pos < array_str.length()) {
        while (pos < array_str.length() && (array_str[pos] == ' ' || array_str[pos] == ',')) pos++;
        if (pos >= array_str.length() || array_str[pos] == ']') break;
        
        if (array_str[pos] == '"') {
            size_t end = array_str.find('"', pos + 1);
            if (end == std::string::npos) break;
            result.push_back(array_str.substr(pos + 1, end - pos - 1));
            pos = end + 1;
        } else {
            pos++;
        }
    }
    return result;
}

void StratumClient::parseLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    
    if (line.find("\"id\":1") != std::string::npos && line.find("\"result\"") != std::string::npos) {
        size_t pos = line.find("]]");
        if (pos != std::string::npos) {
            pos += 2;
            while (pos < line.length() && (line[pos] == ',' || line[pos] == ' ' || line[pos] == '"')) {
                if (line[pos] == '"') { pos++; break; }
                pos++;
            }
            
            size_t en1_end = line.find('"', pos);
            if (en1_end != std::string::npos && en1_end > pos) {
                m_extranonce1 = line.substr(pos, en1_end - pos);
                pos = en1_end + 1;
                while (pos < line.length() && (line[pos] == ',' || line[pos] == ' ')) pos++;
                
                size_t size_start = pos;
                while (pos < line.length() && line[pos] >= '0' && line[pos] <= '9') pos++;
                
                if (pos > size_start) {
                    m_extranonce2_size = std::stoi(line.substr(size_start, pos - size_start));
                }
            }
        }
        LOG_INFO("Subscribe: extranonce1=" + m_extranonce1 + " size=" + std::to_string(m_extranonce2_size));
        return;
    }
    
    if (line.find("\"method\"") != std::string::npos && line.find("\"mining.notify\"") != std::string::npos) {
        if (m_extranonce1.empty()) {
            LOG_ERROR_ALWAYS("extranonce1 not set!");
            return;
        }
        
        size_t params_start = line.find("\"params\"");
        if (params_start == std::string::npos) return;
        
        size_t array_start = line.find('[', params_start);
        if (array_start == std::string::npos) return;
        
        int bracket_depth = 1;
        size_t array_end = array_start + 1;
        bool in_string = false;
        while (array_end < line.length() && bracket_depth > 0) {
            char c = line[array_end];
            if (c == '"' && (array_end == 0 || line[array_end-1] != '\\')) in_string = !in_string;
            else if (!in_string) {
                if (c == '[') bracket_depth++;
                else if (c == ']') bracket_depth--;
            }
            array_end++;
        }
        
        std::string params = line.substr(array_start + 1, array_end - array_start - 2);
        std::vector<std::string> elements = parseStratumParams(params);
        
        if (elements.size() >= 8) {
            try {
                m_job_id = elements[0];
                m_prev_hash = elements[1];
                std::string coinb1 = elements[2];
                std::string coinb2 = elements[3];
                
                m_merkle_branches.clear();
                if (elements.size() > 4 && elements[4][0] == '[') {
                    m_merkle_branches = extractMerkleBranches(elements[4]);
                }
                
                m_extranonce2 = generateExtranonce2();
                std::string merkle = SHA256Utils::calculateMerkleRoot(
                    coinb1, m_extranonce1, m_extranonce2, coinb2, m_merkle_branches);
                m_merkle_root = merkle;
                
                m_version = elements[5];
                m_current_nbits = elements[6];
                m_current_ntime = elements[7];
                
                buildHeader();
                m_new_job_available.store(true, std::memory_order_release);
                
                LOG_INFO("New job: " + m_job_id + " en2=" + m_extranonce2 + " ntime=" + m_current_ntime);
            } catch (const std::exception& e) {
                LOG_ERROR_ALWAYS("Parse error: " + std::string(e.what()));
            }
        }
        return;
    }
    
    if (line.find("\"mining.set_difficulty\"") != std::string::npos) {
        size_t diff_start = line.find('[', 0);
        if (diff_start != std::string::npos) {
            size_t diff_end = line.find(']', diff_start);
            if (diff_end != std::string::npos) {
                std::string diff_str = line.substr(diff_start + 1, diff_end - diff_start - 1);
                try { m_difficulty.store(std::stod(diff_str)); } catch (...) {}
            }
        }
        return;
    }
    
    if (line.find("\"id\":4") != std::string::npos) {
        if (line.find("\"error\"") != std::string::npos && line.find("null") == std::string::npos) {
            m_shares_rejected.fetch_add(1, std::memory_order_relaxed);
            LOG_ERROR_ALWAYS("Share REJECTED: " + line);
        } else if (line.find("\"result\"") != std::string::npos) {
            m_shares_accepted.fetch_add(1, std::memory_order_relaxed);
            LOG_INFO("Share ACCEPTED!");
        }
        return;
    }
}

std::string StratumClient::generateExtranonce2() {
    char buffer[17];
    snprintf(buffer, sizeof(buffer), "%08x", m_extranonce2_counter++);
    
    std::string result = buffer;
    int hex_len = m_extranonce2_size * 2;
    if ((int)result.length() > hex_len) {
        result = result.substr(0, hex_len);
    } else if ((int)result.length() < hex_len) {
        result = std::string(hex_len - result.length(), '0') + result;
    }
    return result;
}

void StratumClient::buildHeader() {
    std::memset(m_current_header, 0, 80);
    
    std::vector<uint8_t> version_bytes = SHA256Utils::hexToBytes(m_version);
    if (version_bytes.size() >= 4) {
        SHA256Utils::reverseBytesInPlace(version_bytes.data(), 4);
        std::memcpy(m_current_header, version_bytes.data(), 4);
    }
    
    std::vector<uint8_t> prev_bytes = SHA256Utils::hexToBytes(m_prev_hash);
    if (prev_bytes.size() >= 32) {
        for (int i = 0; i < 8; i++) {
            std::memcpy(m_current_header + 4 + i * 4, prev_bytes.data() + i * 4, 4);
            SHA256Utils::reverseBytesInPlace(m_current_header + 4 + i * 4, 4);
        }
    }
    
    std::vector<uint8_t> merkle_bytes = SHA256Utils::hexToBytes(m_merkle_root);
    if (merkle_bytes.size() >= 32) {
        for (int i = 0; i < 8; i++) {
            std::memcpy(m_current_header + 36 + i * 4, merkle_bytes.data() + i * 4, 4);
            SHA256Utils::reverseBytesInPlace(m_current_header + 36 + i * 4, 4);
        }
    }
    
    std::vector<uint8_t> ntime_bytes = SHA256Utils::hexToBytes(m_current_ntime);
    if (ntime_bytes.size() >= 4) {
        SHA256Utils::reverseBytesInPlace(ntime_bytes.data(), 4);
        std::memcpy(m_current_header + 68, ntime_bytes.data(), 4);
    }
    
    std::vector<uint8_t> nbits_bytes = SHA256Utils::hexToBytes(m_current_nbits);
    if (nbits_bytes.size() >= 4) {
        SHA256Utils::reverseBytesInPlace(nbits_bytes.data(), 4);
        std::memcpy(m_current_header + 72, nbits_bytes.data(), 4);
    }
    
    std::memset(m_current_header + 76, 0, 4);
}

void StratumClient::update() {
    if (!m_connected.load(std::memory_order_acquire)) {
        LOG_INFO("Attempting reconnect...");
        reconnect();
    }
}

bool StratumClient::submitShare(uint32_t nonce, const std::string& job_id) {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    if (!m_connected.load(std::memory_order_acquire)) return false;
    return submitShare(job_id, m_extranonce2, m_current_ntime, nonce);
}

bool StratumClient::submitShare(const std::string& job_id, const std::string& en2, 
                                 const std::string& ntime, uint32_t nonce) {
    char nonce_str[9];
    snprintf(nonce_str, sizeof(nonce_str), "%08x", nonce);
    
    std::string msg = "{\"id\":4,\"method\":\"mining.submit\",\"params\":[\"" + 
                      m_user + "\",\"" + job_id + "\",\"" + en2 + "\",\"" + 
                      ntime + "\",\"" + nonce_str + "\"]}\n";
    
    return sendMessage(msg);
}
