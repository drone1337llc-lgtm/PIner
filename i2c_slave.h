#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include "config.h"

// MUST MATCH PI SIDE EXACTLY
#pragma pack(push, 1)
struct JobI2cRequest {
    uint8_t     cmd;            // 1 byte
    uint8_t     crc;            // 1 byte
    uint8_t     id;             // 1 byte
    uint8_t     reserved;       // 1 byte
    uint32_t    nonce_start;    // 4 bytes
    float       difficulty;     // 4 bytes
    uint8_t     buffer[76];     // 76 bytes
};                              // Total: 88 bytes

struct JobI2cResult {
    uint8_t     cmd;            // 1 byte
    uint8_t     crc;            // 1 byte
    uint8_t     id;             // 1 byte
    uint8_t     reserved;       // 1 byte
    uint32_t    nonce;          // 4 bytes
    uint32_t    processed_nonce;// 4 bytes - HASH COUNT!
};                              // Total: 12 bytes
#pragma pack(pop)

// Compile-time size verification
static_assert(sizeof(JobI2cRequest) == 88, "JobI2cRequest size mismatch");
static_assert(sizeof(JobI2cResult) == 12, "JobI2cResult size mismatch");

class I2CSlave {
public:
    I2CSlave(uint8_t addr, int sda, int scl);
    bool begin();
    
    bool hasNewJob() const { return m_new_job.load(std::memory_order_acquire); }
    void clearNewJob() { m_new_job.store(false, std::memory_order_release); }
    const JobI2cRequest& getCurrentJob() const { return m_incoming_job; }
    
    void setFoundNonce(uint32_t nonce) { m_found_nonce.store(nonce); }
    
    // Track actual hashes computed
    void addHashes(uint32_t count) { m_hashes_computed.fetch_add(count); }
    uint32_t getAndResetHashes() { return m_hashes_computed.exchange(0); }
    uint32_t getTotalHashes() const { return m_hashes_computed.load(); }
    
    static uint8_t calculateCRC8(const void* data, size_t len);

private:
    static void onReceive(int len);
    static void onRequest();
    static I2CSlave* s_instance;
    
    uint8_t m_addr;
    int m_sda, m_scl;
    std::atomic<bool> m_new_job;
    std::atomic<uint32_t> m_found_nonce;
    std::atomic<uint32_t> m_hashes_computed{0};
    JobI2cRequest m_incoming_job;
    JobI2cResult m_result;
    
    static const uint8_t s_crc8_table[256];
};

#endif // I2C_SLAVE_H
