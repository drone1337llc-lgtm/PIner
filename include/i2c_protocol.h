#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <Arduino.h>

#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA

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

#endif