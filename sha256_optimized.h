#ifndef SHA256_OPTIMIZED_H
#define SHA256_OPTIMIZED_H

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>
#include <mbedtls/sha256.h>

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

#ifdef __cplusplus
extern "C" {
#endif

// Simple double SHA256 (mbedTLS uses hardware automatically on ESP32)
void sha256_double(const uint8_t* data, size_t len, uint8_t* hash);

// CRC8 for I2C
uint8_t crc8_compute(const void* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SHA256_OPTIMIZED_H
