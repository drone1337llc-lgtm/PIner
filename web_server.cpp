#include "web_server.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>

#define WEB_LOG_ENABLED 0

WebServer::WebServer(int port)
    : m_port(port), m_server_fd(-1), m_running(false)
{
}

WebServer::~WebServer()
{
    stop();
}

bool WebServer::start()
{
    m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        std::cerr << "[WEB] ERROR: Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    int opt = 1;
    setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct timeval timeout;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;
    setsockopt(m_server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(m_server_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);
    
    if (bind(m_server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[WEB] ERROR: Failed to bind: " << strerror(errno) << std::endl;
        close(m_server_fd);
        return false;
    }
    
    if (listen(m_server_fd, 20) < 0) {
        std::cerr << "[WEB] ERROR: Failed to listen: " << strerror(errno) << std::endl;
        close(m_server_fd);
        return false;
    }
    
    m_running = true;
    m_server_thread = std::thread(&WebServer::serverLoop, this);
    
    return true;
}

void WebServer::stop()
{
    m_running = false;
    
    if (m_server_fd >= 0) {
        shutdown(m_server_fd, SHUT_RDWR);
        close(m_server_fd);
        m_server_fd = -1;
    }
    
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
}

void WebServer::updateStats(const WebServerStats& stats)
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_current_stats = stats;
}

void WebServer::addLogMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_log_mutex);
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "] " << message;
    
    m_log_messages.push_back(ss.str());
    
    if (m_log_messages.size() > MAX_LOG_MESSAGES) {
        m_log_messages.erase(m_log_messages.begin());
    }
}

void WebServer::serverLoop()
{
    while (m_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(m_server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (m_running && errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "[WEB] Accept failed: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        handleClient(client_fd);
        close(client_fd);
    }
}

void WebServer::handleClient(int client_fd)
{
    char buffer[4096];
    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) return;
    
    buffer[bytes_read] = '\0';
    std::string request(buffer);
    
    std::string content_type = "text/html";
    std::string content;
    
    if (request.find("GET /stats.json") != std::string::npos) {
        content_type = "application/json";
        content = getStatsJSON();
    }
    else if (request.find("GET /logs") != std::string::npos) {
        content_type = "application/json";
        content = "[]";
    }
    else {
        content = generateHTML();
    }
    
    std::stringstream header;
    header << "HTTP/1.1 200 OK\r\n";
    header << "Content-Type: " << content_type << "; charset=utf-8\r\n";
    header << "Content-Length: " << content.length() << "\r\n";
    header << "Connection: close\r\n";
    header << "Access-Control-Allow-Origin: *\r\n";
    header << "Cache-Control: no-cache\r\n";
    header << "\r\n";
    
    std::string full_response = header.str() + content;
    
    size_t total_sent = 0;
    while (total_sent < full_response.length()) {
        ssize_t sent = write(client_fd, full_response.c_str() + total_sent, 
                            full_response.length() - total_sent);
        if (sent < 0) break;
        total_sent += (size_t)sent;
    }
}

std::string WebServer::generateHTML()
{
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Pi Bitcoin Miner</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #eee;
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1400px; margin: 0 auto; }
        h1 { 
            text-align: center; 
            color: #f39c12; 
            margin-bottom: 30px;
            font-size: clamp(1.5rem, 4vw, 2.5rem);
        }
        
        /* Main Stats Grid */
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        
        .stat-card {
            background: rgba(255,255,255,0.1);
            border-radius: 15px;
            padding: 25px;
            text-align: center;
            border: 1px solid rgba(255,255,255,0.2);
            backdrop-filter: blur(10px);
        }
        
        .stat-value { 
            font-size: clamp(1.5rem, 5vw, 3rem); 
            font-weight: bold; 
            color: #f39c12;
            word-break: break-all;
            line-height: 1.2;
        }
        
        .stat-label { 
            font-size: clamp(0.7rem, 2vw, 0.9rem); 
            color: #aaa; 
            text-transform: uppercase;
            margin-top: 10px;
            letter-spacing: 1px;
        }
        
        /* Header Stats Row */
        .header-stats {
            display: flex;
            justify-content: space-around;
            flex-wrap: wrap;
            gap: 15px;
            margin-bottom: 30px;
            padding: 20px;
            background: rgba(255,255,255,0.05);
            border-radius: 15px;
        }
        
        .header-stat { 
            text-align: center; 
            min-width: 120px;
        }
        
        .header-stat-value { 
            font-size: clamp(1.2rem, 3vw, 2rem); 
            font-weight: bold; 
            color: #f39c12;
            word-break: break-word;
        }
        
        .header-stat-label { 
            font-size: clamp(0.6rem, 1.5vw, 0.8rem); 
            color: #888;
            margin-top: 5px;
        }
        
        /* Workers Section */
        .workers-section {
            background: rgba(255,255,255,0.1);
            border-radius: 15px;
            padding: 25px;
            margin-bottom: 30px;
        }
        
        .workers-section h2 {
            margin-bottom: 20px;
            font-size: clamp(1.2rem, 3vw, 1.5rem);
        }
        
        .worker-list {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
        }
        
        .worker-item {
            background: rgba(39,174,96,0.1);
            border: 1px solid #27ae60;
            border-radius: 10px;
            padding: 15px;
            text-align: center;
        }
        
        .worker-status { 
            font-weight: bold; 
            color: #27ae60;
            font-size: clamp(0.8rem, 2vw, 1rem);
        }
        
        /* Pool Info */
        .pool-section {
            background: rgba(255,255,255,0.1);
            border-radius: 15px;
            padding: 25px;
            margin-bottom: 30px;
        }
        
        .pool-info {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
        }
        
        .pool-item {
            background: rgba(0,0,0,0.2);
            padding: 15px;
            border-radius: 10px;
        }
        
        .pool-item-label {
            font-size: clamp(0.7rem, 2vw, 0.85rem);
            color: #aaa;
            margin-bottom: 5px;
        }
        
        .pool-item-value {
            font-size: clamp(0.9rem, 2.5vw, 1.1rem);
            color: #fff;
            word-break: break-all;
            font-family: monospace;
        }
        
        /* Status Indicator */
        .status-indicator {
            position: fixed;
            top: 10px;
            right: 10px;
            padding: 8px 20px;
            border-radius: 20px;
            font-size: clamp(0.7rem, 2vw, 0.9rem);
            font-weight: bold;
            z-index: 100;
        }
        .status-indicator.connected { 
            background: #27ae60;
            box-shadow: 0 0 10px rgba(39,174,96,0.5);
        }
        .status-indicator.disconnected { 
            background: #e74c3c;
            box-shadow: 0 0 10px rgba(231,76,60,0.5);
        }
        
        /* Responsive adjustments */
        @media (max-width: 768px) {
            .container { padding: 10px; }
            .stat-card { padding: 15px; }
            .header-stats { padding: 15px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 Pi Bitcoin Miner</h1>
        
        <div id="status" class="status-indicator connected">● Connected</div>
        
        <!-- Main Stats -->
        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-value" id="hashrate">0 H/s</div>
                <div class="stat-label">Hash Rate</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" id="total-nonces">0</div>
                <div class="stat-label">Total Hashes</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" id="accepted">0</div>
                <div class="stat-label">Accepted Shares</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" id="rejected">0</div>
                <div class="stat-label">Rejected Shares</div>
            </div>
        </div>
        
        <!-- Secondary Stats -->
        <div class="header-stats">
            <div class="header-stat">
                <div class="header-stat-value" id="success-rate">0%</div>
                <div class="header-stat-label">Success Rate</div>
            </div>
            <div class="header-stat">
                <div class="header-stat-value" id="difficulty">0</div>
                <div class="header-stat-label">Difficulty</div>
            </div>
            <div class="header-stat">
                <div class="header-stat-value" id="uptime">00:00:00</div>
                <div class="header-stat-label">Uptime</div>
            </div>
            <div class="header-stat">
                <div class="header-stat-value" id="worker-count">3</div>
                <div class="header-stat-label">Workers</div>
            </div>
        </div>
        
        <!-- Pool Info -->
        <div class="pool-section">
            <h2 style="margin-bottom: 15px; font-size: clamp(1.2rem, 3vw, 1.5rem);">📡 Pool Connection</h2>
            <div class="pool-info">
                <div class="pool-item">
                    <div class="pool-item-label">Pool Host</div>
                    <div class="pool-item-value" id="pool-host">-</div>
                </div>
                <div class="pool-item">
                    <div class="pool-item-label">Connection Status</div>
                    <div class="pool-item-value" id="pool-status">-</div>
                </div>
                <div class="pool-item">
                    <div class="pool-item-label">Active Workers</div>
                    <div class="pool-item-value" id="active-workers">3 cores</div>
                </div>
            </div>
        </div>
        
        <!-- Workers -->
        <div class="workers-section">
            <h2>⚡ Mining Workers</h2>
            <div class="worker-list" id="worker-list">
                <div class="worker-item">
                    <div style="font-size: clamp(0.9rem, 2vw, 1rem);">Core 0</div>
                    <div class="worker-status">● Active</div>
                </div>
                <div class="worker-item">
                    <div style="font-size: clamp(0.9rem, 2vw, 1rem);">Core 0</div>
                    <div class="worker-status">● Active</div>
                </div>
                <div class="worker-item">
                    <div style="font-size: clamp(0.9rem, 2vw, 1rem);">Core 0</div>
                    <div class="worker-status">● Active</div>
                </div>
                <div class="worker-item">
                    <div style="font-size: clamp(0.9rem, 2vw, 1rem);">Core 0</div>
                    <div class="worker-status">● Active</div>
                </div>
            </div>
        </div>
    </div>
    
    <script>
        let connected = true;
        
        function formatHashrate(h) {
            if (!h || h <= 0 || h > 1e15) return '0 H/s';
            
            if (h >= 1e9) return (h/1e9).toFixed(2) + ' GH/s';
            if (h >= 1e6) return (h/1e6).toFixed(2) + ' MH/s';
            if (h >= 1e3) return (h/1e3).toFixed(0) + ' kH/s';
            return h.toFixed(0) + ' H/s';
        }
        
        function formatNumber(n) {
            if (n >= 1e9) return (n/1e9).toFixed(2) + 'B';
            if (n >= 1e6) return (n/1e6).toFixed(2) + 'M';
            if (n >= 1e3) return (n/1e3).toFixed(1) + 'K';
            return n.toLocaleString();
        }
        
        function formatUptime(s) {
            const h = Math.floor(s/3600);
            const m = Math.floor((s%3600)/60);
            const sec = s % 60;
            return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');
        }
        
        function setStatus(online) {
            connected = online;
            const el = document.getElementById('status');
            el.className = 'status-indicator ' + (online ? 'connected' : 'disconnected');
            el.textContent = (online ? '● ' : '○ ') + (online ? 'Connected' : 'Disconnected');
            
            const poolStatus = document.getElementById('pool-status');
            poolStatus.textContent = online ? '● Connected' : '○ Disconnected';
            poolStatus.style.color = online ? '#27ae60' : '#e74c3c';
        }
        
        async function updateStats() {
            try {
                const r = await fetch('/stats.json', {cache: 'no-cache'});
                const d = await r.json();
                
                document.getElementById('hashrate').textContent = formatHashrate(d.hashrate);
                document.getElementById('accepted').textContent = formatNumber(d.shares_accepted);
                document.getElementById('rejected').textContent = formatNumber(d.shares_rejected);
                document.getElementById('uptime').textContent = formatUptime(d.uptime_seconds);
                document.getElementById('difficulty').textContent = d.difficulty >= 1000 ? 
                    (d.difficulty/1000).toFixed(0) + 'K' : d.difficulty.toFixed(0);
                document.getElementById('total-nonces').textContent = formatNumber(d.total_nonces);
                
                const t = d.shares_accepted + d.shares_rejected;
                const rate = t > 0 ? ((d.shares_accepted/t)*100).toFixed(1) : 100;
                document.getElementById('success-rate').textContent = rate + '%';
                document.getElementById('worker-count').textContent = d.esp32_count > 0 ? d.esp32_count : 4;
                document.getElementById('pool-host').textContent = d.pool_host;
                
                setStatus(d.pool_connected);
            } catch (e) {
                setStatus(false);
                console.error('Stats error:', e);
            }
        }
        
        updateStats();
        setInterval(updateStats, 2000);
    </script>
</body>
</html>
)rawliteral";
}

std::string WebServer::getStatsJSON()
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    
    std::stringstream ss;
    ss << "{";
    ss << "\"hashrate\":" << m_current_stats.hashrate << ",";
    ss << "\"shares_accepted\":" << m_current_stats.shares_accepted << ",";
    ss << "\"shares_rejected\":" << m_current_stats.shares_rejected << ",";
    ss << "\"total_nonces\":" << m_current_stats.total_nonces << ",";
    ss << "\"uptime_seconds\":" << m_current_stats.uptime_seconds << ",";
    ss << "\"difficulty\":" << m_current_stats.difficulty << ",";
    ss << "\"pool_host\":\"" << m_current_stats.pool_host << "\",";
    ss << "\"pool_connected\":" << (m_current_stats.pool_connected ? "true" : "false") << ",";
    ss << "\"esp32_count\":" << (int)m_current_stats.esp32_count << ",";
    ss << "\"esp32_workers\":[]";
    ss << "}";
    
    return ss.str();
}
