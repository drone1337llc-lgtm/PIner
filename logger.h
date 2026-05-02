#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <string>
#include <atomic>

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    
    void init(const std::string& filename, bool console = false) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_console_output = console;
        m_logfile.open(filename, std::ios::app);
        if (!m_logfile.is_open()) {
            std::cerr << "Failed to open log file: " << filename << std::endl;
        }
    }
    
    void log(const std::string& level, const std::string& message, bool always = false) {
        // Rate limit ERROR messages (only log 1 in 100)
        if (!always && level == "ERROR") {
            if (m_error_count++ % 100 != 0) return;
        }
        
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";
        ss << " [" << level << "] " << message << std::endl;
        
        std::string line = ss.str();
        
        if (m_logfile.is_open()) {
            m_logfile << line;
            m_logfile.flush();
        }
        
        if (m_console_output) {
            std::cout << line << std::flush;
        }
        
        rotateLogIfNeeded();
    }
    
    void info(const std::string& msg) { log("INFO", msg); }
    void warn(const std::string& msg) { log("WARN", msg); }
    void error(const std::string& msg, bool always = false) { log("ERROR", msg, always); }
    void debug(const std::string& msg) { log("DEBUG", msg); }
    
    void close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logfile.is_open()) {
            m_logfile.close();
        }
    }
    
private:
    Logger() : m_console_output(false), m_error_count(0) {}
    ~Logger() { close(); }
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void rotateLogIfNeeded() {
        try {
            m_logfile.seekp(0, std::ios::end);
            std::streampos size = m_logfile.tellp();
            if (size > 5 * 1024 * 1024) {
                m_logfile.close();
                std::string archive_name = "miner.log." + 
                    std::to_string(std::time(nullptr));
                std::rename("miner.log", archive_name.c_str());
                m_logfile.open("miner.log", std::ios::out | std::ios::trunc);
            }
        } catch (...) {}
    }
    
    std::ofstream m_logfile;
    std::mutex m_mutex;
    bool m_console_output;
    std::atomic<uint64_t> m_error_count{0};
};

// Fixed macros - no default parameters in preprocessor
#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARN(msg) Logger::getInstance().warn(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg, false)
#define LOG_ERROR_ALWAYS(msg) Logger::getInstance().error(msg, true)
#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)

#endif