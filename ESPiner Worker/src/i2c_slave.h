#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <cstring>
#include "config.h"

#pragma pack(push, 1)
struct JobI2cRequest {
    uint8_t  cmd;
    uint8_t  nonce_start_byte; // Matching Master nonce offset logic
    float    difficulty;
    uint8_t  buffer[76];
    uint8_t  crc;
};

struct JobI2cResult {
    uint8_t  cmd;
    uint8_t  id;
    uint32_t nonce;
    uint32_t hashrate_raw;
    uint8_t  crc;
};
#pragma pack(pop)

class I2CSlave {
public:
    I2CSlave(uint8_t addr);
    void begin(uint8_t address);
    
    bool hasNewJob();
    void getJob(JobI2cRequest &dest);
    void clearNewJob();
    
    void addHashes(uint32_t hashes) { m_hashes_since_poll.fetch_add(hashes); }
    void setFoundNonce(uint32_t nonce) { m_found_nonce.store(nonce); }

    volatile bool m_mining_enabled;
    uint32_t m_crc_errors = 0;

private:
    static I2CSlave* s_instance;
    static void handleReceive(int len);
    static void handleRequest();
    
    uint8_t m_addr;
    SemaphoreHandle_t m_jobMutex;
    bool m_new_job_available;
    JobI2cRequest m_current_job;
    
    std::atomic<uint32_t> m_hashes_since_poll;
    std::atomic<uint32_t> m_found_nonce;
};

#endif