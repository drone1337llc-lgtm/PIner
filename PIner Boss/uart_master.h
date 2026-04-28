#ifndef UART_MASTER_H
#define UART_MASTER_H

#include <stdint.h>
#include <vector>
#include <string>
#include <functional>
#include <thread>

struct UartJobRequest {
    uint8_t job_id;
    float difficulty;
    uint32_t nonce_start;
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
};

struct UartJobResult {
    uint8_t job_id;
    uint32_t nonce;
    uint32_t hashes_processed;
    bool share_found;
};

class UartMaster {
public:
    UartMaster(const std::string& device = "/dev/serial0", int baud_rate = 115200);
    ~UartMaster();
    
    bool start();
    void stop();
    
    void sendJob(const UartJobRequest& job);
    bool receiveResult(UartJobResult& result, int timeout_ms = 100);
    
    void setJobRequestCallback(std::function<void()> callback);
    void setResultCallback(std::function<void(const UartJobResult&)> callback);
    
    bool isConnected() const { return m_connected; }
    std::string getDevice() const { return m_device; }

private:
    int m_uart_fd;
    std::string m_device;
    int m_baud_rate;
    bool m_connected;
    bool m_running;
    
    std::function<void()> m_job_request_callback;
    std::function<void(const UartJobResult&)> m_result_callback;
    
    std::thread m_receive_thread;
    
    bool openUart();
    void closeUart();
    void receiveLoop();
    
    static uint8_t calculateCRC8(const void* data, size_t len);
    static const uint8_t s_crc8_table[256];
};

#endif // UART_MASTER_H
