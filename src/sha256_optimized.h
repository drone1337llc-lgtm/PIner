#ifndef SHA256_OPTIMIZED_H
#define SHA256_OPTIMIZED_H

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

#define SHA256_BLOCK_SIZE     64
#define SHA256_DIGEST_SIZE    32
#define SHA256_MIDSTATE_SIZE  32

typedef struct {
    uint32_t digest[8];
    uint32_t bake[15];
    uint8_t buffer[64];
} sha256_context_opt;

// ✅ C++ linkage (matches i2c_protocol.h)
uint8_t crc8_compute(const void* data, size_t len);
void sha256_midstate(uint32_t* digest, const uint8_t* data);
void sha256_bake(const uint32_t* digest, const uint8_t* data, uint32_t* bake);
bool sha256_double_baked(const uint32_t* digest, const uint8_t* data, 
                         const uint32_t* bake, uint8_t* hash, float difficulty);

#endif
