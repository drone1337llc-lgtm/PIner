#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <cstring>

// I2C Commands
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC
#define I2C_CMD_STOP            0xAD

// I2C Protocol Structures
#pragma pack(push, 1)

struct JobI2cRequest {
    uint8_t     cmd;
    uint8_t     crc;
    uint8_t     id;
    uint8_t     reserved;
    uint32_t    nonce_start;
    float       difficulty;
    uint8_t     buffer[76];
};

struct JobI2cResult {
    uint8_t     cmd;
    uint8_t     crc;
    uint8_t     id;
    uint8_t     reserved;
    uint32_t    nonce;
    uint32_t    processed_nonce;
};

#pragma pack(pop)

// I2C Slave Class
class I2CSlave {
public:
    I2CSlave(uint8_t addr);
    ~I2CSlave();
    
    void begin(uint8_t address);
    
    // Job access
    bool hasNewJob() const { return m_new_job_available.load(); }
    JobI2cRequest getCurrentJob() const { return m_current_job; }
    void clearNewJob() { m_new_job_available.store(false); }
    
    // Hash counting
    void addHashes(uint32_t hashes) { 
        m_hashes_since_poll.fetch_add(hashes); 
    }
    
    // Nonce handling
    void setFoundNonce(uint32_t nonce) { 
        m_found_nonce.store(nonce); 
    }
    uint32_t getFoundNonce() const { 
        return m_found_nonce.load(); 
    }
    void clearFoundNonce() {
        m_found_nonce.store(0xFFFFFFFF);
    }

    // Public state
    volatile bool m_mining_enabled;
    volatile unsigned long m_last_heartbeat;
    uint32_t m_jobs_received;
    uint32_t m_crc_errors;
    uint32_t m_i2c_requests;

private:
    static I2CSlave* s_instance;
    
    static void handleReceive(int len);
    static void handleRequest();
    static uint8_t calculateCRC8(const void* data, size_t len);
    static bool verifyCRC(void* data, size_t len);
    
    uint8_t m_addr;
    std::atomic<bool> m_new_job_available;
    std::atomic<uint32_t> m_hashes_since_poll;
    std::atomic<uint32_t> m_found_nonce;
    JobI2cRequest m_current_job;
};

#endif
