#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <cstring>
#include "config.h"
#include "i2c_protocol.h"
#include "sha256_optimized.h"

class I2CSlave {
public:
    static I2CSlave& getInstance();
    
    void begin(uint8_t address);
    uint8_t getAddress() const { return m_addr; }
    
    bool hasNewJob() const { return m_new_job_available.load(); }
    void getJob(JobI2cRequest &dest);
    void clearNewJob();
    
    void addHashes(uint32_t hashes) { m_hashes_since_poll.fetch_add(hashes); }
    void setFoundNonce(uint32_t nonce);
    uint32_t getFoundNonce();
    void clearFoundNonce();
    
    uint32_t getHashesSincePoll() { return m_hashes_since_poll.exchange(0); }
    uint32_t getCrcErrors() const { return m_crc_errors; }
    
    volatile bool m_mining_enabled;

private:
    I2CSlave();
    I2CSlave(const I2CSlave&) = delete;
    I2CSlave& operator=(const I2CSlave&) = delete;
    
    static I2CSlave* s_instance;
    static void handleReceive(int len);
    static void handleRequest();
    
    uint8_t m_addr;
    std::atomic<bool> m_new_job_available{false};
    JobI2cRequest m_current_job;
    
    std::atomic<uint32_t> m_hashes_since_poll{0};
    std::atomic<uint32_t> m_found_nonce{0xFFFFFFFF};
    std::atomic<uint32_t> m_crc_errors{0};
    
    SemaphoreHandle_t m_jobMutex;
};

#define I2C_SLAVE I2CSlave::getInstance()

#endif
