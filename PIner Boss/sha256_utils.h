#ifndef SHA256_UTILS_H
#define SHA256_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <memory>

// ============================================================================
// SHARED CRC8 TABLE (Used across all modules)
// ============================================================================

class CryptoUtils {
public:
    // CRC8 computation (shared across I2C, UART, etc.)
    static inline uint8_t crc8(const void* data, size_t len) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        uint8_t crc = 0;
        for (size_t i = 0; i < len; i++) {
            crc = s_crc8_table[crc ^ ptr[i]];
        }
        return crc;
    }
    
    // Verify CRC
    static inline bool verifyCrc8(const void* data, size_t len, uint8_t expected_crc) {
        uint8_t* mutable_data = const_cast<uint8_t*>(static_cast<const uint8_t*>(data));
        uint8_t original_crc = mutable_data[1]; // Assuming CRC at offset 1
        mutable_data[1] = 0; // Zero out CRC field for calculation
        uint8_t calculated = crc8(data, len);
        mutable_data[1] = original_crc; // Restore
        return calculated == expected_crc;
    }

private:
    static const uint8_t s_crc8_table[256];
};

// ============================================================================
// SHA256 UTILITIES
// ============================================================================

class SHA256Utils {
public:
    // Hex conversion (optimized with pre-allocated buffers)
    static std::vector<uint8_t> hexToBytes(const std::string& hex);
    static std::string bytesToHex(const uint8_t* bytes, size_t len);
    static std::string bytesToHex(const std::vector<uint8_t>& bytes);
    
    // SHA256 operations (OpenSSL backend)
    static std::vector<uint8_t> sha256(const uint8_t* data, size_t len);
    static std::vector<uint8_t> sha256d(const uint8_t* data, size_t len);
    
    // Bitcoin-specific operations
    static std::vector<uint8_t> reverseBytes(const std::vector<uint8_t>& bytes);
    static void reverseBytesInPlace(uint8_t* data, size_t len);
    
    // Block header operations (zero-allocation version)
    static size_t buildBlockHeader(const std::string& version,
                                   const std::string& prev_hash,
                                   const std::string& merkle_root,
                                   const std::string& ntime,
                                   const std::string& nbits,
                                   uint32_t nonce,
                                   uint8_t* output_buffer);
    
    // Merkle root calculation
    static std::string calculateMerkleRoot(const std::string& coinbase1,
                                          const std::string& extranonce1,
                                          const std::string& extranonce2,
                                          const std::string& coinbase2,
                                          const std::vector<std::string>& merkle_branches);
    
    // Difficulty verification
    static bool verifyShare(const uint8_t* hash, double difficulty);
    static std::vector<uint8_t> difficultyToTarget(double difficulty);
    
    // Fast difficulty check (leading zeros)
    static inline bool quickDifficultyCheck(const uint8_t* hash, uint8_t required_zeros) {
        for (uint8_t i = 0; i < required_zeros; i++) {
            if (hash[i] != 0) return false;
        }
        return true;
    }
};

#endif // SHA256_UTILS_H
