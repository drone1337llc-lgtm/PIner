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
#include <cstdio>
#include <unistd.h>

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    
    void init(const std::string& filename, bool console = false) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_console_output = console;
        m_filename = filename;
        
        if (m_logfile.is_open()) m_logfile.close();
        m_logfile.open(filename, std::ios::app);
        if (!m_logfile.is_open()) {
            std::cerr << "Failed to open log file: " << filename << std::endl;
        }
    }
    
    void log(const std::string& level, const std::string& message, bool always = false) {
        if (!always && level == "ERROR") {
            if (m_error_count.fetch_add(1, std::memory_order_relaxed) % 100 != 0) return;
        }
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer), 
            "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] %s\n",
            std::localtime(&time)->tm_year + 1900,
            std::localtime(&time)->tm_mon + 1,
            std::localtime(&time)->tm_mday,
            std::localtime(&time)->tm_hour,
            std::localtime(&time)->tm_min,
            std::localtime(&time)->tm_sec,
            (int)ms.count(),
            level.c_str(),
            message.c_str());
        
        std::string line(buffer);
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_logfile.is_open()) {
                m_logfile << line;
                m_logfile.flush();
            }
            
            if (m_console_output) {
                std::cout << line << std::flush;
            }
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
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_logfile.is_open()) return;
        
        m_logfile.flush();
        m_logfile.seekp(0, std::ios::end);
        std::streampos size = m_logfile.tellp();
        
        if (size > 5 * 1024 * 1024) {
            m_logfile.close();
            
            char archive_name[256];
            snprintf(archive_name, sizeof(archive_name), 
                "%s.%ld", m_filename.c_str(), (long)std::time(nullptr));
            
            std::rename(m_filename.c_str(), archive_name);
            m_logfile.open(m_filename, std::ios::out | std::ios::trunc);
        }
    }
    
    std::ofstream m_logfile;
    std::mutex m_mutex;
    bool m_console_output;
    std::atomic<uint64_t> m_error_count;
    std::string m_filename;
};

#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARN(msg) Logger::getInstance().warn(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg, false)
#define LOG_ERROR_ALWAYS(msg) Logger::getInstance().error(msg, true)
#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)

#endif
