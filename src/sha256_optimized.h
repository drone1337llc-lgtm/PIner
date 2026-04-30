#ifndef SHA256_OPTIMIZED_H
#define SHA256_OPTIMIZED_H

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

#if defined(HARDWARE_SHA256)
  #include <sha/sha_parallel_engine.h>
  #include <hal/sha_hal.h>
  #include <hal/sha_ll.h>
#endif

#define SHA256_BLOCK_SIZE     64
#define SHA256_DIGEST_SIZE    32
#define SHA256_MIDSTATE_SIZE  32

// Optimized SHA256 context with baked midstate
typedef struct {
    uint32_t digest[8];
    uint32_t bake[15];      // Pre-calculated values for faster hashing
    uint8_t buffer[64];
} sha256_context_opt;

#ifdef __cplusplus
extern "C" {
#endif

// Fast double SHA256 with midstate optimization
void sha256_midstate(uint32_t* digest, const uint8_t* data);
void sha256_bake(const uint32_t* digest, const uint8_t* data, uint32_t* bake);
bool sha256_double_baked(const uint32_t* digest, const uint8_t* data, 
                         const uint32_t* bake, uint8_t* hash);

// Hardware accelerated SHA256 (ESP32 only)
#ifdef HARDWARE_SHA256
  void sha256_hw_init(void);
  bool sha256_hw_double(const uint8_t* data, uint8_t* hash);
#endif

// CRC8 for I2C
uint8_t crc8_compute(const void* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
