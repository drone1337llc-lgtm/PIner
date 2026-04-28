#include "terminal_ui.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <sys/ioctl.h>
#include <unistd.h>
#include <mutex>
#include <deque>

static std::mutex g_ui_mutex;
static std::deque<std::string> g_log_buffer;  // Buffer for log messages
static const size_t MAX_LOG_LINES = 6;

TerminalUI::TerminalUI() : m_initialized(false), m_screen_height(24) {}
TerminalUI::~TerminalUI() { cleanup(); }

void TerminalUI::init() { 
    std::lock_guard<std::mutex> lock(g_ui_mutex);
    hideCursor(); 
    std::cout << "\033[2J\033[H";
    m_initialized = true; 
}

void TerminalUI::cleanup() { 
    if(m_initialized) { 
        std::lock_guard<std::mutex> lock(g_ui_mutex);
        showCursor(); 
        std::cout << "\033[24;1H\n" << std::endl;
    } 
    m_initialized = false; 
}

void TerminalUI::hideCursor() { std::cout << "\033[?25l"; }
void TerminalUI::showCursor() { std::cout << "\033[?25h"; }

// FIXED: Add log message to buffer (called from coordinator)
void TerminalUI::addLogMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_ui_mutex);
    g_log_buffer.push_back(msg);
    if (g_log_buffer.size() > MAX_LOG_LINES * 2) {
        g_log_buffer.pop_front();
    }
}

void TerminalUI::update(const DisplayStats& s) {
    if(!m_initialized) return;
    
    std::lock_guard<std::mutex> lock(g_ui_mutex);
    
    std::stringstream buffer;
    buffer << "\033[H";  // Move cursor to home
    
    buffer << drawHeader();
    buffer << drawStats(s);
    buffer << drawWorkers(s);
    buffer << drawPool(s);
    buffer << drawFooter();
    
    // Draw log section at fixed position
    buffer << "\033[22;1H\033[33m--- System Logs ---\033[0m\n";
    
    // Show only recent logs (last MAX_LOG_LINES)
    size_t start = g_log_buffer.size() > MAX_LOG_LINES ? 
                   g_log_buffer.size() - MAX_LOG_LINES : 0;
    for (size_t i = start; i < g_log_buffer.size(); i++) {
        buffer << "\033[90m" << g_log_buffer[i] << "\033[0m\n";
    }
    
    // Clear remaining lines to prevent artifacts
    for (size_t i = g_log_buffer.size(); i < MAX_LOG_LINES; i++) {
        buffer << "\033[2K\n";  // Clear line
    }
    
    std::cout << buffer.str() << std::flush;
}

std::string TerminalUI::drawHeader() {
    std::stringstream ss;
    ss << "\033[36m┌────────────────────────────────────────────────┐\033[0m\n";
    ss << "\033[36m│\033[0m\033[1;37m        🚀 Pi Bitcoin Miner Dashboard         \033[0m\033[36m│\033[0m\n";
    ss << "\033[36m└────────────────────────────────────────────────┘\033[0m\n";
    return ss.str();
}

std::string TerminalUI::drawStats(const DisplayStats& s) {
    std::stringstream ss;
    uint64_t t = s.shares_accepted + s.shares_rejected;
    double r = t > 0 ? (double)s.shares_accepted * 100.0 / t : 0.0;
    
    ss << "\033[33mMining Stats:\033[0m\n";
    ss << "\033[36m┌────────────────────────────────────────────────┐\033[0m\n";
    ss << "\033[36m│\033[0m Hash: \033[1;32m" << std::left << std::setw(12) << fmtRate(s.hashrate) << "\033[0m";
    ss << " Diff: \033[1;34m" << std::setw(15) << (int)s.difficulty << "\033[0m\033[36m│\033[0m\n";
    
    ss << "\033[36m│\033[0m Acc: \033[32m" << std::setw(6) << s.shares_accepted << "\033[0m";
    ss << " Rej: \033[31m" << std::setw(6) << s.shares_rejected << "\033[0m";
    ss << " Tot: \033[33m" << std::setw(10) << s.total_nonces << "\033[0m \033[36m│\033[0m\n";
    
    ss << "\033[36m│\033[0m Rate: " << drawBar(r, 30) << " \033[36m│\033[0m\n";
    ss << "\033[36m│\033[0m Uptime: \033[35m" << std::left << std::setw(38) << fmtTime(s.uptime_seconds) << "\033[0m\033[36m│\033[0m\n";
    ss << "\033[36m└────────────────────────────────────────────────┘\033[0m\n";
    return ss.str();
}

std::string TerminalUI::drawWorkers(const DisplayStats& s) {
    std::stringstream ss;
    ss << "\033[33mESP32 Workers: \033[1;32m" << (int)s.esp32_count << "\033[0m\n";
    ss << "\033[36m┌────────────────────────────────────────────────┐\033[0m\n";
    
    if(s.esp32_count == 0) {
        ss << "\033[36m│\033[0m \033[31m[!] No I2C workers detected! Check wiring     \033[0m\033[36m│\033[0m\n";
    } else {
        ss << "\033[36m│\033[0m ";
        for(size_t i = 0; i < s.esp32_addresses.size() && i < 4; i++) {
            ss << "\033[32m0x" << std::hex << (int)s.esp32_addresses[i] << std::dec << "\033[0m ";
        }
        int remaining = 48 - 2 - (int)(s.esp32_addresses.size() * 5);
        if(remaining > 0) ss << std::string(remaining, ' ');
        ss << "\033[36m│\033[0m\n";
    }
    
    ss << "\033[36m└────────────────────────────────────────────────┘\033[0m\n";
    return ss.str();
}

std::string TerminalUI::drawPool(const DisplayStats& s) {
    std::stringstream ss;
    std::string status = s.pool_connected ? "\033[1;32mConnected\033[0m" : "\033[1;31mDisconnected\033[0m";
    ss << "Pool: " << status << " (" << s.pool_host << ")\n";
    return ss.str();
}

std::string TerminalUI::drawFooter() {
    return "\033[90mCtrl+C to stop | Logs will appear below\033[0m\n";
}

std::string TerminalUI::drawBar(double p, int w) {
    std::stringstream ss;
    ss << "\033[36m[\033[0m";
    int f = (int)(p * w / 100.0);
    for(int i = 0; i < w; i++) {
        if(i < f) ss << "\033[32m#\033[0m";
        else ss << "\033[90m-\033[0m";
    }
    ss << "\033[36m]\033[0m \033[1;33m" << (int)p << "%\033[0m";
    return ss.str();
}

std::string TerminalUI::fmtRate(double r) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if(r >= 1000000.0) ss << (r/1000000.0) << " MH/s";
    else if(r >= 1000.0) ss << (r/1000.0) << " KH/s";
    else ss << std::setprecision(1) << r << " H/s";
    return ss.str();
}

std::string TerminalUI::fmtTime(uint64_t s) {
    uint64_t d = s/86400, h = (s%86400)/3600, m = (s%3600)/60, sec = s%60;
    std::ostringstream ss;
    if(d > 0) ss << d << "d ";
    ss << std::setfill('0') << std::setw(2) << h << ":" << std::setw(2) << m << ":" << std::setw(2) << sec;
    return ss.str();
}
