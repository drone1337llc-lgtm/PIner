#ifndef SHA256_MINER_H
#define SHA256_MINER_H

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <arm_neon.h>

#define CACHE_LINE_SIZE 64

struct alignas(64) MiningContext {
    uint32_t midstate[8];
    uint32_t block2_w[16];
    uint32_t target[8];
    uint32_t nonce_start;
    uint32_t nonce_end;
    std::atomic<uint64_t> job_version{0};
    std::atomic<bool> job_valid{false};
    std::atomic<uint64_t> last_hash_time{0};
    char padding[CACHE_LINE_SIZE];            // Prevent false sharing
};

class SHA256Miner {
public:
    SHA256Miner();
    ~SHA256Miner();
    
    void setJob(const uint8_t* header, uint32_t nonce_start, uint32_t nonce_end, 
                double difficulty, uint64_t job_version);
    void mine_continuous(std::atomic<uint32_t>& found_nonce, std::atomic<bool>& running);
    
    uint64_t getHashCount() const { return m_hash_count.load(std::memory_order_relaxed); }
    void resetHashCount() { m_hash_count.store(0, std::memory_order_relaxed); }
    bool isJobComplete() const;
    uint64_t getLastHashTime() const { return m_ctx.last_hash_time.load(std::memory_order_relaxed); }

private:
    alignas(64) MiningContext m_ctx;
    alignas(64) std::atomic<uint64_t> m_hash_count{0};    // Separate cache line
    
    void prepare_job_data_neon(const uint8_t* header);
    void mine_batch_neon(uint32_t start, uint32_t end, std::atomic<uint32_t>& found_nonce, 
                         uint64_t& count);
    static void difficultyToTarget(double difficulty, uint32_t* target);
};

#endif
