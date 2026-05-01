#ifndef SHA256_OPTIMIZED_H
#define SHA256_OPTIMIZED_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE     64
#define SHA256_DIGEST_SIZE    32
#define SHA256_MIDSTATE_SIZE  32

#ifdef __cplusplus
extern "C" {
#endif

uint8_t crc8_compute(const void* data, size_t len);
void sha256_midstate(uint32_t* digest, const uint8_t* data);
void sha256_bake(const uint32_t* digest, const uint8_t* data, uint32_t* bake);
bool sha256_double_baked(const uint32_t* digest, const uint8_t* data, 
                         const uint32_t* bake, uint8_t* hash);

#ifdef __cplusplus
}
#endif

#endif
