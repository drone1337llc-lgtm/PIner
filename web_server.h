#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

struct WebServerStats {
    double hashrate;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t total_nonces;
    uint64_t uptime_seconds;
    double difficulty;
    std::string pool_host;
    bool pool_connected;
    uint8_t esp32_count;
    std::vector<std::string> esp32_workers;
};

class WebServer {
public:
    WebServer(int port = 8080);
    ~WebServer();
    
    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    
    void updateStats(const WebServerStats& stats);
    void addLogMessage(const std::string& message);
    
private:
    int m_port;
    int m_server_fd;
    std::atomic<bool> m_running;
    std::thread m_server_thread;
    
    WebServerStats m_current_stats;
    std::mutex m_stats_mutex;
    
    std::vector<std::string> m_log_messages;
    std::mutex m_log_mutex;
    static const size_t MAX_LOG_MESSAGES = 100;  // Keep this, remove from config
    
    void serverLoop();
    void handleClient(int client_fd);
    std::string generateHTML();
    std::string getStatsJSON();
};

#endif // WEB_SERVER_H
