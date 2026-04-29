#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <Arduino.h>
#include <Wire.h>
#include <atomic>

// I2C Command Protocol (MUST MATCH PI)
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC
#define I2C_CMD_STOP            0xAD

// I2C Protocol Structures (MUST MATCH PI EXACTLY)
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

static_assert(sizeof(JobI2cRequest) == 88, "JobI2cRequest size mismatch");
static_assert(sizeof(JobI2cResult) == 12, "JobI2cResult size mismatch");

class I2CSlave {
public:
    I2CSlave(uint8_t addr, int sda, int scl);
    bool begin();
    
    bool hasNewJob() const { return m_new_job.load(std::memory_order_acquire); }
    JobI2cRequest getCurrentJob() const { return m_incoming_job; }
    void clearNewJob() { m_new_job.store(false, std::memory_order_release); }
    
    void addHashes(uint32_t hashes) { 
        m_hashes_computed.fetch_add(hashes, std::memory_order_relaxed); 
    }
    uint32_t getAndResetHashes() {
        return m_hashes_computed.exchange(0, std::memory_order_relaxed);
    }
    
    void setFoundNonce(uint32_t nonce) { 
        m_found_nonce.store(nonce, std::memory_order_release); 
    }
    uint32_t getFoundNonce() const { 
        return m_found_nonce.load(std::memory_order_acquire); 
    }

private:
    static I2CSlave* s_instance;
    static void onReceive(int len);
    static void onRequest();
    
    static uint8_t calculateCRC8(const void* data, size_t len);
    
    uint8_t m_addr;
    int m_sda;
    int m_scl;
    
    std::atomic<bool> m_new_job;
    JobI2cRequest m_incoming_job;
    JobI2cResult m_result;
    
    std::atomic<uint32_t> m_found_nonce;
    std::atomic<uint32_t> m_hashes_computed;
};

#endif
