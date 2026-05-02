#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#pragma pack(push, 1)

struct JobI2cRequest {
    uint8_t     cmd;
    uint8_t     id;
    uint8_t     nonce_start_byte;
    float       difficulty;
    uint8_t     buffer[76];
    uint8_t     crc;
};

struct I2CStatusResponse {
    uint8_t     cmd;
    uint8_t     status;
    uint32_t    nonce;
    uint32_t    hash_count;
    uint8_t     crc;
};

#pragma pack(pop)

#define JOB_REQUEST_SIZE        sizeof(JobI2cRequest)
#define STATUS_RESPONSE_SIZE    sizeof(I2CStatusResponse)

struct SlaveData {
    uint8_t     address;
    uint32_t    last_seen;
    uint32_t    shares_submitted;
    uint32_t    shares_accepted;
    float       hashrate;
    uint32_t    hashes_processed;
    uint32_t    last_hash_count;
    bool        active;
};

uint8_t crc8_compute(const void* data, size_t len);

#endif
