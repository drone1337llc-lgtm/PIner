#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <Arduino.h>

#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA

struct SlaveData {
    uint8_t address;
    uint32_t last_seen;
    uint32_t shares;
    float hashrate;
};

// Update stats to track real pool performance
struct {
    float difficulty = 0;
    uint8_t header[76];
    bool new_job = false;
    uint32_t total_shares = 0;
    uint32_t rejected_shares = 0; 
    String pool_status = "Connecting...";
} stats;

#pragma pack(push, 1)
struct JobI2cRequest {
    uint8_t     cmd;
    uint8_t     crc;
    uint8_t     id;
    uint8_t     nonce_start_byte; // From your Gist: uses uint8_t for start offset
    float       difficulty;
    uint8_t     buffer[76];
};

struct JobI2cResult {
    uint8_t     cmd;
    uint8_t     crc;
    uint8_t     id;
    uint32_t    nonce;
    uint32_t    processed_nonce;
};
#pragma pack(pop)

#endif