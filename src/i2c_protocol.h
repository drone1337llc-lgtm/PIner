#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// MINER COMMAND CODES
// ============================================================================
#define MINER_CMD_FEED            0xA1
#define MINER_CMD_REQUEST_RESULT  0xA9
#define MINER_CMD_SLAVE_RESULT    0xAA
#define MINER_CMD_PING            0xAB
#define MINER_CMD_RESET           0xAC

// ============================================================================
// I2C MESSAGE STRUCTURES (Must match slave exactly)
// ============================================================================
#pragma pack(push, 1)

struct JobI2cRequest {
    uint8_t     cmd;
    uint8_t     id;
    uint8_t     nonce_start_byte;
    float       difficulty;
    uint8_t     buffer[76];
    uint8_t     crc;
};

struct JobI2cResult {
    uint8_t     cmd;
    uint8_t     id;
    uint32_t    nonce;
    uint32_t    hashrate_raw;
    uint8_t     crc;
};

struct I2CStatusResponse {
    uint8_t     cmd;
    uint8_t     status;
    uint32_t    nonce;
    uint8_t     crc;
};

#pragma pack(pop)

// Structure sizes for validation
#define JOB_REQUEST_SIZE    sizeof(JobI2cRequest)
#define JOB_RESULT_SIZE     sizeof(JobI2cResult)
#define STATUS_RESPONSE_SIZE sizeof(I2CStatusResponse)

// ============================================================================
// SLAVE TRACKING
// ============================================================================
struct SlaveData {
    uint8_t     address;
    uint32_t    last_seen;
    uint32_t    shares;
    float       hashrate;
    uint32_t    hashes_processed;
};

#endif
