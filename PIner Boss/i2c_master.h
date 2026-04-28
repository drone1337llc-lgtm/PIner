#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include "config.h"
#include "sha256_utils.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

// ============================================================================
// I2C PROTOCOL STRUCTURES (Must match ESP32 slave side)
// ============================================================================

#pragma pack(push, 1)

struct JobI2cRequest {
    uint8_t     cmd;            // 1 byte - I2C_CMD_FEED
    uint8_t     crc;            // 1 byte - CRC8 checksum
    uint8_t     id;             // 1 byte - Job ID
    uint8_t     reserved;       // 1 byte - Alignment
    uint32_t    nonce_start;    // 4 bytes - Starting nonce
    float       difficulty;     // 4 bytes - Mining difficulty
    uint8_t     buffer[76];     // 76 bytes - Block header (excluding nonce)
};                              // Total: 88 bytes

struct JobI2cResult {
    uint8_t     cmd;            // 1 byte - I2C_CMD_SLAVE_RESULT
    uint8_t     crc;            // 1 byte - CRC8 checksum
    uint8_t     id;             // 1 byte - Job ID
    uint8_t     reserved;       // 1 byte - Alignment
    uint32_t    nonce;          // 4 bytes - Found nonce (0xFFFFFFFF = none)
    uint32_t    processed_nonce;// 4 bytes - Number of nonces processed
};                              // Total: 12 bytes

#pragma pack(pop)

// Compile-time size checks
static_assert(sizeof(JobI2cRequest) == 88, "JobI2cRequest size mismatch");
static_assert(sizeof(JobI2cResult) == 12, "JobI2cResult size mismatch");

// ============================================================================
// I2C MASTER CLASS
// ============================================================================

class I2CMaster {
public:
    explicit I2CMaster(int bus = I2C_BUS);
    ~I2CMaster();
    
    // Initialization
    bool start();
    bool isInitialized() const { return m_initialized.load(); }
    
    // Device scanning
    std::vector<uint8_t> scan(uint8_t start_addr = I2C_BASE_ADDRESS, 
                             uint8_t end_addr = I2C_MAX_ADDRESS);
    
    // Bulk operations (optimized for mining)
    bool feedSlavesWithJob(const std::vector<uint8_t>& slave_addresses,
                          uint8_t job_id,
                          uint32_t nonce_start,
                          float difficulty,
                          const uint8_t* header_buffer,
                          size_t header_size = 76);
    
    std::vector<uint32_t> harvestSlaves(const std::vector<uint8_t>& slave_addresses,
                                       uint8_t job_id,
                                       uint32_t& total_processed_nonce);
    
    // Individual operations
    bool writeBytes(uint8_t addr, const uint8_t* data, size_t len);
    bool readBytes(uint8_t addr, uint8_t* data, size_t len);
    
    // Statistics
    struct I2CStats {
        uint64_t transactions;
        uint64_t errors;
        uint64_t retries;
        uint32_t last_error_code;
    };
    
    I2CStats getStats() const;
    void resetStats();

private:
    int m_i2c_fd;
    int m_bus;
    std::atomic<bool> m_initialized;
    
    // Statistics (atomic for lock-free access)
    mutable std::atomic<uint64_t> m_transaction_count;
    mutable std::atomic<uint64_t> m_error_count;
    mutable std::atomic<uint64_t> m_retry_count;
    mutable std::atomic<uint32_t> m_last_error_code;
    
    // DMA-capable buffers (aligned for optimal performance)
    alignas(64) uint8_t m_tx_buffer[256];
    alignas(64) uint8_t m_rx_buffer[256];
    
    // Mutex for thread-safe operations
    mutable std::mutex m_i2c_mutex;
    
    // Internal methods
    bool openDevice();
    void closeDevice();
    bool transaction(uint8_t addr, uint8_t* tx_data, size_t tx_len, 
                    uint8_t* rx_data, size_t rx_len);
};

#endif // I2C_MASTER_H
