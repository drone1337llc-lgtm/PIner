#include "sha256_optimized.h"
#include <string.h>
#include <esp_system.h>

// Remove HARDWARE_SHA256 code - let mbedTLS handle it
// This avoids the CONFIG_MBEDTLS_HARDWARE_SHA redefinition warning

// CRC8 table (must match Pi master exactly)
DRAM_ATTR static const uint8_t CRC8_TABLE[256] = {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97,
    0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4,
    0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11,
    0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52,
    0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA,
    0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9,
    0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C,
    0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F,
    0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED,
    0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE,
    0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B,
    0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28,
    0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0,
    0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93,
    0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56,
    0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15,
    0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

// SHA256 constants
DRAM_ATTR static const uint32_t K[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define GET_UINT32_BE(data, offset) \
    (((uint32_t)(data)[offset] << 24) | ((uint32_t)(data)[offset + 1] << 16) | \
     ((uint32_t)(data)[offset + 2] << 8) | ((uint32_t)(data)[offset + 3]))

#define S0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define S1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))
#define S2(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define F0(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define F1(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))

#define P(a, b, c, d, e, f, g, h, x, k) \
    { \
        temp1 = (h) + S3(e) + F1(e, f, g) + (k) + (x); \
        temp2 = S2(a) + F0(a, b, c); \
        (d) += temp1; \
        (h) = temp1 + temp2; \
    }

// CRC8 computation
IRAM_ATTR uint8_t crc8_compute(const void* data, size_t len) {
    const uint8_t* ptr = (const uint8_t*)data;
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = CRC8_TABLE[crc ^ ptr[i]];
    }
    return crc;
}

// Calculate midstate (first 64 bytes of block header)
IRAM_ATTR void sha256_midstate(uint32_t* digest, const uint8_t* data) {
    uint32_t A[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                     0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};
    uint32_t W[64], temp1, temp2;
    
    for (int i = 0; i < 16; i++) {
        W[i] = GET_UINT32_BE(data, i * 4);
    }
    
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[0], K[0]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[1], K[1]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[2], K[2]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[3], K[3]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[4], K[4]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[5], K[5]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[6], K[6]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[7], K[7]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[8], K[8]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[9], K[9]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[10], K[10]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[11], K[11]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[12], K[12]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[13], K[13]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[14], K[14]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[15], K[15]);
    
    for (int i = 16; i < 64; i++) {
        W[i] = S1(W[i-2]) + W[i-7] + S0(W[i-15]) + W[i-16];
        P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[i], K[i]);
        uint32_t tmp = A[7];
        for (int j = 7; j > 0; j--) A[j] = A[j-1];
        A[0] = tmp;
    }
    
    for (int i = 0; i < 8; i++) {
        digest[i] = A[i] + ((i == 0) ? 0x6A09E667 : 
                           (i == 1) ? 0xBB67AE85 :
                           (i == 2) ? 0x3C6EF372 :
                           (i == 3) ? 0xA54FF53A :
                           (i == 4) ? 0x510E527F :
                           (i == 5) ? 0x9B05688C :
                           (i == 6) ? 0x1F83D9AB : 0x5BE0CD19);
    }
}

// Bake pre-calculated values for faster nonce iteration
IRAM_ATTR void sha256_bake(const uint32_t* digest, const uint8_t* data, uint32_t* bake) {
    bake[0] = GET_UINT32_BE(data, 0);
    bake[1] = GET_UINT32_BE(data, 4);
    bake[2] = GET_UINT32_BE(data, 8);
    
    uint32_t A[8], temp1, temp2;
    for (int i = 0; i < 8; i++) A[i] = digest[i];
    
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], bake[0], K[0]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], bake[1], K[1]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], bake[2], K[2]);
    
    bake[3] = S1(0) + 0 + S0(bake[1]) + bake[0];
    bake[4] = S1(640) + 0 + S0(bake[2]) + bake[1];
    
    for (int i = 0; i < 8; i++) {
        bake[5 + i] = A[i];
    }
    
    temp1 = A[4] + S3(A[1]) + F1(A[1], A[2], A[3]) + K[3];
    temp2 = S2(A[5]) + F0(A[5], A[6], A[7]);
    bake[13] = temp1;
    bake[14] = temp2;
}

// Fast double SHA256 using baked values
IRAM_ATTR bool sha256_double_baked(const uint32_t* digest, const uint8_t* data, 
                                    const uint32_t* bake, uint8_t* hash) {
    uint32_t A[8], W[64], temp1, temp2;
    
    W[0] = bake[0];
    W[1] = bake[1];
    W[2] = bake[2];
    W[3] = GET_UINT32_BE(data, 12);
    W[4] = 0x80000000;
    for (int i = 5; i < 15; i++) W[i] = 0;
    W[15] = 640;
    W[16] = bake[3];
    W[17] = bake[4];
    
    for (int i = 0; i < 8; i++) A[i] = bake[5 + i];
    
    temp1 = bake[13] + W[3];
    temp2 = bake[14];
    A[0] += temp1;
    A[4] = temp1 + temp2;
    
    for (int i = 4; i < 64; i++) {
        if (i > 17) {
            W[i] = S1(W[i-2]) + W[i-7] + S0(W[i-15]) + W[i-16];
        }
        P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[i], K[i]);
        uint32_t tmp = A[7];
        for (int j = 7; j > 0; j--) A[j] = A[j-1];
        A[0] = tmp;
    }
    
    W[0] = A[0] + digest[0];
    W[1] = A[1] + digest[1];
    W[2] = A[2] + digest[2];
    W[3] = A[3] + digest[3];
    W[4] = A[4] + digest[4];
    W[5] = A[5] + digest[5];
    W[6] = A[6] + digest[6];
    W[7] = A[7] + digest[7];
    W[8] = 0x80000000;
    for (int i = 9; i < 15; i++) W[i] = 0;
    W[15] = 256;
    
    for (int i = 0; i < 8; i++) A[i] = ((i == 0) ? 0x6A09E667 : 
                                        (i == 1) ? 0xBB67AE85 :
                                        (i == 2) ? 0x3C6EF372 :
                                        (i == 3) ? 0xA54FF53A :
                                        (i == 4) ? 0x510E527F :
                                        (i == 5) ? 0x9B05688C :
                                        (i == 6) ? 0x1F83D9AB : 0x5BE0CD19);
    
    for (int i = 0; i < 64; i++) {
        if (i > 15) {
            W[i] = S1(W[i-2]) + W[i-7] + S0(W[i-15]) + W[i-16];
        }
        P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[i], K[i]);
        uint32_t tmp = A[7];
        for (int j = 7; j > 0; j--) A[j] = A[j-1];
        A[0] = tmp;
        
        if (i == 60 && (A[7] & 0xFFFF) != 0) {
            return false;
        }
    }
    
    for (int i = 0; i < 8; i++) {
        uint32_t val = A[i] + ((i == 0) ? 0x6A09E667 : 
                               (i == 1) ? 0xBB67AE85 :
                               (i == 2) ? 0x3C6EF372 :
                               (i == 3) ? 0xA54FF53A :
                               (i == 4) ? 0x510E527F :
                               (i == 5) ? 0x9B05688C :
                               (i == 6) ? 0x1F83D9AB : 0x5BE0CD19);
        hash[i*4] = (val >> 24) & 0xFF;
        hash[i*4+1] = (val >> 16) & 0xFF;
        hash[i*4+2] = (val >> 8) & 0xFF;
        hash[i*4+3] = val & 0xFF;
    }
    
    return true;
}
