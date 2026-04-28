#include "web_server.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>

// Disable verbose web logging (causes UI corruption)
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
    
    // Set socket timeouts
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
    
#if WEB_LOG_ENABLED
    std::cerr << "[WEB] Server started on port " << m_port << std::endl;
#endif
    
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
#if WEB_LOG_ENABLED
    std::cerr << "[WEB] Server loop running" << std::endl;
#endif
    
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
        
#if WEB_LOG_ENABLED
        std::cerr << "[WEB] Client: " << inet_ntoa(client_addr.sin_addr) << std::endl;
#endif
        
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
    
    // Declare variables at the start
    std::string content_type = "text/html";
    std::string content;
    
    if (request.find("GET /stats.json") != std::string::npos) {
        content_type = "application/json";
        content = getStatsJSON();
    }
    else if (request.find("GET /logs") != std::string::npos) {
        content_type = "application/json";
        
        std::lock_guard<std::mutex> lock(m_log_mutex);
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < m_log_messages.size(); i++) {
            if (i > 0) ss << ",";
            std::string escaped;
            for (char c : m_log_messages[i]) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else if (c == '\n') escaped += "\\n";
                else if (c == '\r') escaped += "\\r";
                else escaped += c;
            }
            ss << "\"" << escaped << "\"";
        }
        ss << "]";
        content = ss.str();
    }
    else {
        content = generateHTML();
    }
    
    // Build HTTP response
    std::stringstream header;
    header << "HTTP/1.1 200 OK\r\n";
    header << "Content-Type: " << content_type << "; charset=utf-8\r\n";
    header << "Content-Length: " << content.length() << "\r\n";
    header << "Connection: close\r\n";
    header << "Access-Control-Allow-Origin: *\r\n";
    header << "Cache-Control: no-cache\r\n";
    header << "\r\n";
    
    std::string full_response = header.str() + content;
    
    // Send all data
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
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #eee;
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        h1 { text-align: center; color: #f39c12; margin-bottom: 30px; }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .stat-card {
            background: rgba(255,255,255,0.1);
            border-radius: 15px;
            padding: 25px;
            text-align: center;
            border: 1px solid rgba(255,255,255,0.2);
            overflow: hidden; /* Keeps the card tidy while content inside can scroll */
        }
        .stat-value { font-size: 2.5em; font-weight: bold; color: #f39c12; }
        
        /* Specific override for the Pool Host display */
        #pool-host { 
            font-size: 1.2em; /* Smaller font as requested */
            white-space: nowrap; /* Forces text to stay on one line */
            overflow-x: auto; /* Allows horizontal scrolling if too long */
            display: block;
            padding-bottom: 5px;
        }
        /* Custom scrollbar for the pool host if it overflows */
        #pool-host::-webkit-scrollbar { height: 4px; }
        #pool-host::-webkit-scrollbar-thumb { background: #f39c12; border-radius: 10px; }

        .stat-label { font-size: 0.9em; color: #aaa; text-transform: uppercase; }
        .workers-section {
            background: rgba(255,255,255,0.1);
            border-radius: 15px;
            padding: 25px;
            margin-bottom: 30px;
        }
        .worker-list {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
        }
        .worker-item {
            background: rgba(0,255,0,0.1);
            border: 1px solid #27ae60;
            border-radius: 10px;
            padding: 15px;
            text-align: center;
        }
        .worker-status { font-weight: bold; color: #27ae60; }
        .terminal-section {
            background: #1e1e1e;
            border-radius: 15px;
            padding: 20px;
        }
        .terminal-output {
            background: #000;
            border-radius: 10px;
            padding: 15px;
            height: 300px;
            overflow-y: auto;
            font-family: monospace;
            font-size: 0.85em;
            color: #0f0;
        }
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
        .header-stat { text-align: center; }
        .header-stat-value { font-size: 1.8em; font-weight: bold; color: #f39c12; }
        .header-stat-label { font-size: 0.8em; color: #888; }
        .status-indicator {
            position: fixed;
            top: 10px;
            right: 10px;
            padding: 5px 15px;
            border-radius: 20px;
            font-size: 0.8em;
        }
        .status-indicator.connected { background: #27ae60; }
        .status-indicator.disconnected { background: #e74c3c; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 Pi Bitcoin Miner Dashboard</h1>
        
        <div id="status" class="status-indicator connected">● Connected</div>
        
        <div class="header-stats">
            <div class="header-stat">
                <div class="header-stat-value" id="hashrate">0 H/s</div>
                <div class="header-stat-label">Hash Rate</div>
            </div>
            <div class="header-stat">
                <div class="header-stat-value" id="accepted">0</div>
                <div class="header-stat-label">Accepted</div>
            </div>
            <div class="header-stat">
                <div class="header-stat-value" id="rejected">0</div>
                <div class="header-stat-label">Rejected</div>
            </div>
            <div class="header-stat">
                <div class="header-stat-value" id="uptime">00:00:00</div>
                <div class="header-stat-label">Uptime</div>
            </div>
            <div class="header-stat">
                <div class="header-stat-value" id="difficulty">0</div>
                <div class="header-stat-label">Difficulty</div>
            </div>
        </div>
        
        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-value" id="total-nonces">0</div>
                <div class="stat-label">Total Nonces</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" id="success-rate">0%</div>
                <div class="stat-label">Success Rate</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" id="worker-count">0</div>
                <div class="stat-label">Workers</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" id="pool-host">-</div>
                <div class="stat-label">Pool</div>
            </div>
        </div>
        
        <div class="workers-section">
            <h2>⚡ ESP32 Workers</h2>
            <div class="worker-list" id="worker-list">
                <div class="worker-item"><div>No workers</div></div>
            </div>
        </div>
        
        <div class="terminal-section">
            <h2>📋 Log</h2>
            <div class="terminal-output" id="terminal-output"></div>
        </div>
    </div>
    
    <script>
        let connected = true;
        
        function formatHashrate(h) {
            if (h >= 1e6) return (h/1e6).toFixed(2) + ' MH/s';
            if (h >= 1e3) return (h/1e3).toFixed(2) + ' KH/s';
            return h.toFixed(1) + ' H/s';
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
        }
        
        async function updateStats() {
            try {
                const r = await fetch('/stats.json', {cache: 'no-cache'});
                const d = await r.json();
                
                document.getElementById('hashrate').textContent = formatHashrate(d.hashrate);
                document.getElementById('accepted').textContent = d.shares_accepted;
                document.getElementById('rejected').textContent = d.shares_rejected;
                document.getElementById('uptime').textContent = formatUptime(d.uptime_seconds);
                document.getElementById('difficulty').textContent = d.difficulty.toFixed(0);
                document.getElementById('total-nonces').textContent = d.total_nonces.toLocaleString();
                
                const t = d.shares_accepted + d.shares_rejected;
                const rate = t > 0 ? ((d.shares_accepted/t)*100).toFixed(1) : 0;
                document.getElementById('success-rate').textContent = rate + '%';
                document.getElementById('worker-count').textContent = d.esp32_count;
                document.getElementById('pool-host').textContent = d.pool_host;
                
                setStatus(d.pool_connected);
                
                const wl = document.getElementById('worker-list');
                if (d.esp32_workers && d.esp32_workers.length > 0) {
                    wl.innerHTML = d.esp32_workers.map(w => 
                        '<div class="worker-item"><div>'+w+'</div><div class="worker-status">● Active</div></div>'
                    ).join('');
                } else {
                    wl.innerHTML = '<div class="worker-item"><div>No workers</div></div>';
                }
            } catch (e) {
                setStatus(false);
                console.error('Stats error:', e);
            }
        }
        
        async function updateLogs() {
            try {
                const logs = await fetch('/logs', {cache: 'no-cache'});
                const data = await logs.json();
                const out = document.getElementById('terminal-output');
                if (data && data.length > 0) {
                    out.innerHTML = data.map(l => '<div>'+l+'</div>').join('');
                    out.scrollTop = out.scrollHeight;
                }
            } catch (e) {
                console.error('Logs error:', e);
            }
        }
        
        updateStats();
        updateLogs();
        setInterval(() => {
            updateStats();
            updateLogs();
        }, 2000);
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
    ss << "\"esp32_workers\":[";
    for (size_t i = 0; i < m_current_stats.esp32_workers.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << m_current_stats.esp32_workers[i] << "\"";
    }
    ss << "]";
    ss << "}";
    
    return ss.str();
}
