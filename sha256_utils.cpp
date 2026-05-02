#include "sha256_utils.h"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cmath>

// ============================================================================
// CRC8 TABLE (Single shared instance)
// ============================================================================

const uint8_t CryptoUtils::s_crc8_table[256] = {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97, 0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4, 0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11, 0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52, 0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA, 0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9, 0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C, 0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F, 0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED, 0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE, 0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B, 0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0, 0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93, 0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15, 0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

// ============================================================================
// HEX CONVERSION (Optimized with lookup table)
// ============================================================================

static const char hex_chars[] = "0123456789abcdef";

static inline uint8_t hexCharToValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

std::vector<uint8_t> SHA256Utils::hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);
    
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        uint8_t byte = (hexCharToValue(hex[i]) << 4) | hexCharToValue(hex[i + 1]);
        bytes.push_back(byte);
    }
    return bytes;
}

std::string SHA256Utils::bytesToHex(const uint8_t* bytes, size_t len) {
    std::string result;
    result.reserve(len * 2);
    
    for (size_t i = 0; i < len; i++) {
        result += hex_chars[(bytes[i] >> 4) & 0x0F];
        result += hex_chars[bytes[i] & 0x0F];
    }
    return result;
}

std::string SHA256Utils::bytesToHex(const std::vector<uint8_t>& bytes) {
    return bytesToHex(bytes.data(), bytes.size());
}

// ============================================================================
// SHA256 OPERATIONS
// ============================================================================

std::vector<uint8_t> SHA256Utils::sha256(const uint8_t* data, size_t len) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data, len, hash.data());
    return hash;
}

std::vector<uint8_t> SHA256Utils::sha256d(const uint8_t* data, size_t len) {
    uint8_t first_hash[SHA256_DIGEST_LENGTH];
    SHA256(data, len, first_hash);
    
    std::vector<uint8_t> result(SHA256_DIGEST_LENGTH);
    SHA256(first_hash, SHA256_DIGEST_LENGTH, result.data());
    return result;
}

// ============================================================================
// BYTE ORDER OPERATIONS
// ============================================================================

std::vector<uint8_t> SHA256Utils::reverseBytes(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> reversed = bytes;
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

void SHA256Utils::reverseBytesInPlace(uint8_t* data, size_t len) {
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t temp = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = temp;
    }
}

// ============================================================================
// BLOCK HEADER (Zero-allocation version)
// ============================================================================

size_t SHA256Utils::buildBlockHeader(const std::string& version,
                                     const std::string& prev_hash,
                                     const std::string& merkle_root,
                                     const std::string& ntime,
                                     const std::string& nbits,
                                     uint32_t nonce,
                                     uint8_t* output_buffer) {
    size_t offset = 0;
    
    // Version (4 bytes, little-endian)
    for (size_t i = 0; i < 4 && i + 1 < version.length(); i += 2) {
        output_buffer[offset++] = (hexCharToValue(version[i]) << 4) | hexCharToValue(version[i + 1]);
    }
    std::reverse(output_buffer, output_buffer + 4);
    
    // Previous hash (32 bytes, word-swapped little-endian)
    for (size_t i = 0; i < 32 && i * 2 + 1 < prev_hash.length(); i++) {
        output_buffer[offset++] = (hexCharToValue(prev_hash[i * 2]) << 4) | 
                                  hexCharToValue(prev_hash[i * 2 + 1]);
    }
    // Word-swap (4-byte chunks)
    for (size_t i = 0; i < 8; i++) {
        std::reverse(output_buffer + 4 + i * 4, output_buffer + 4 + i * 4 + 4);
    }
    
    // Merkle root (32 bytes, word-swapped little-endian)
    for (size_t i = 0; i < 32 && i * 2 + 1 < merkle_root.length(); i++) {
        output_buffer[offset++] = (hexCharToValue(merkle_root[i * 2]) << 4) | 
                                  hexCharToValue(merkle_root[i * 2 + 1]);
    }
    for (size_t i = 0; i < 8; i++) {
        std::reverse(output_buffer + 36 + i * 4, output_buffer + 36 + i * 4 + 4);
    }
    
    // nTime (4 bytes, little-endian)
    for (size_t i = 0; i < 4 && i + 1 < ntime.length(); i += 2) {
        output_buffer[offset++] = (hexCharToValue(ntime[i]) << 4) | hexCharToValue(ntime[i + 1]);
    }
    std::reverse(output_buffer + 68, output_buffer + 72);
    
    // nBits (4 bytes, little-endian)
    for (size_t i = 0; i < 4 && i + 1 < nbits.length(); i += 2) {
        output_buffer[offset++] = (hexCharToValue(nbits[i]) << 4) | hexCharToValue(nbits[i + 1]);
    }
    std::reverse(output_buffer + 72, output_buffer + 76);
    
    // Nonce (4 bytes, little-endian)
    output_buffer[76] = nonce & 0xFF;
    output_buffer[77] = (nonce >> 8) & 0xFF;
    output_buffer[78] = (nonce >> 16) & 0xFF;
    output_buffer[79] = (nonce >> 24) & 0xFF;
    
    return 80;
}

// ============================================================================
// MERKLE ROOT CALCULATION
// ============================================================================

std::string SHA256Utils::calculateMerkleRoot(const std::string& coinbase1,
                                             const std::string& extranonce1,
                                             const std::string& extranonce2,
                                             const std::string& coinbase2,
                                             const std::vector<std::string>& merkle_branches) {
    // Build coinbase transaction
    std::string coinbase = coinbase1 + extranonce1 + extranonce2 + coinbase2;
    std::vector<uint8_t> coinbase_bytes = hexToBytes(coinbase);
    
    // Double SHA256 of coinbase
    std::vector<uint8_t> merkle_root = sha256d(coinbase_bytes.data(), coinbase_bytes.size());
    
    // Hash with each merkle branch
    std::vector<uint8_t> concatenated(64);
    
    for (const auto& branch : merkle_branches) {
        std::vector<uint8_t> branch_bytes = hexToBytes(branch);
        
        // Copy merkle root and branch
        memcpy(concatenated.data(), merkle_root.data(), 32);
        memcpy(concatenated.data() + 32, branch_bytes.data(), 32);
        
        // Double SHA256
        merkle_root = sha256d(concatenated.data(), 64);
    }
    
    return bytesToHex(merkle_root);
}

// ============================================================================
// DIFFICULTY VERIFICATION
// ============================================================================

std::vector<uint8_t> SHA256Utils::difficultyToTarget(double difficulty) {
    const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
    double target_double = truediffone / difficulty;
    
    std::vector<uint8_t> target(32, 0);
    
    // Convert to 256-bit target
    for (int i = 31; i >= 0 && target_double > 0; --i) {
        target[i] = static_cast<uint8_t>(static_cast<uint64_t>(target_double) & 0xFF);
        target_double /= 256.0;
    }
    
    return target;
}

bool SHA256Utils::verifyShare(const uint8_t* hash, double difficulty) {
    std::vector<uint8_t> target = difficultyToTarget(difficulty);
    
    // Compare hash to target (little-endian comparison)
    for (int i = 31; i >= 0; --i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true; // Equal
}
