#include "sha256_miner.h"
#include "pi_miner_config.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

static const uint32_t K[64] __attribute__((aligned(64))) = {
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

#define ROTR_NEON(x, n) veorq_u32(vshrq_n_u32(x, n), vshlq_n_u32(x, 32 - n))

SHA256Miner::SHA256Miner() {
    m_ctx.job_valid.store(false, std::memory_order_release);
    m_hash_count.store(0, std::memory_order_release);
    m_ctx.last_hash_time.store(0, std::memory_order_release);
    std::memset(&m_ctx, 0, sizeof(m_ctx));
}

SHA256Miner::~SHA256Miner() {}

void SHA256Miner::prepare_job_data_neon(const uint8_t* header) {
    uint32_t state[8] = {
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
    };

    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)header[i*4] << 24) | ((uint32_t)header[i*4+1] << 16) | 
               ((uint32_t)header[i*4+2] << 8) | ((uint32_t)header[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ((w[i-15] >> 7) | (w[i-15] << 25)) ^ 
                      ((w[i-15] >> 18) | (w[i-15] << 14)) ^ (w[i-15] >> 3);
        uint32_t s1 = ((w[i-2] >> 17) | (w[i-2] << 15)) ^ 
                      ((w[i-2] >> 19) | (w[i-2] << 13)) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a=state[0], b=state[1], c=state[2], d=state[3], 
             e=state[4], f=state[5], g=state[6], h=state[7];
    
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;

    std::memcpy(m_ctx.midstate, state, 32);
    std::memset(m_ctx.block2_w, 0, sizeof(m_ctx.block2_w));
    
    uint32_t w0 = ((uint32_t)header[64] << 24) | ((uint32_t)header[65] << 16) | 
                  ((uint32_t)header[66] << 8) | header[67];
    uint32_t w1 = ((uint32_t)header[68] << 24) | ((uint32_t)header[69] << 16) | 
                  ((uint32_t)header[70] << 8) | header[71];
    uint32_t w2 = ((uint32_t)header[72] << 24) | ((uint32_t)header[73] << 16) | 
                  ((uint32_t)header[74] << 8) | header[75];
    
    m_ctx.block2_w[0] = w0;
    m_ctx.block2_w[1] = w1;
    m_ctx.block2_w[2] = w2;
    m_ctx.block2_w[4] = 0x80000000;
    m_ctx.block2_w[14] = 0x00000280;
}

void SHA256Miner::setJob(const uint8_t* header, uint32_t nonce_start, uint32_t nonce_end, 
                         double difficulty, uint64_t job_version) {
    if (nonce_start % 4 != 0) nonce_start += (4 - (nonce_start % 4));
    if (nonce_end % 4 != 0) nonce_end += (4 - (nonce_end % 4));
    
    prepare_job_data_neon(header);
    m_ctx.nonce_start = nonce_start;
    m_ctx.nonce_end = nonce_end;
    difficultyToTarget(difficulty, m_ctx.target);
    
    std::atomic_thread_fence(std::memory_order_release);
    m_ctx.job_version.store(job_version, std::memory_order_release);
    m_ctx.job_valid.store(true, std::memory_order_release);
    m_hash_count.store(0, std::memory_order_relaxed);
}

static inline bool checkTargetNEON(uint32x4_t hash0, uint32x4_t hash1, 
                                   uint32x4_t target0, uint32x4_t target1) {
    uint32_t h[8], t[8];
    vst1q_u32(h, hash0);
    vst1q_u32(h + 4, hash1);
    vst1q_u32(t, target0);
    vst1q_u32(t + 4, target1);
    
    for (int i = 7; i >= 0; i--) {
        if (h[i] < t[i]) return true;
        if (h[i] > t[i]) return false;
    }
    return true;
}

void SHA256Miner::mine_batch_neon(uint32_t start, uint32_t end, 
                                   std::atomic<uint32_t>& found_nonce, uint64_t& count) {
    uint32x4_t state0 = vdupq_n_u32(m_ctx.midstate[0]);
    uint32x4_t state1 = vdupq_n_u32(m_ctx.midstate[1]);
    uint32x4_t state2 = vdupq_n_u32(m_ctx.midstate[2]);
    uint32x4_t state3 = vdupq_n_u32(m_ctx.midstate[3]);
    uint32x4_t state4 = vdupq_n_u32(m_ctx.midstate[4]);
    uint32x4_t state5 = vdupq_n_u32(m_ctx.midstate[5]);
    uint32x4_t state6 = vdupq_n_u32(m_ctx.midstate[6]);
    uint32x4_t state7 = vdupq_n_u32(m_ctx.midstate[7]);

    uint32x4_t w0 = vdupq_n_u32(m_ctx.block2_w[0]);
    uint32x4_t w1 = vdupq_n_u32(m_ctx.block2_w[1]);
    uint32x4_t w2 = vdupq_n_u32(m_ctx.block2_w[2]);
    uint32x4_t w4 = vdupq_n_u32(m_ctx.block2_w[4]);
    uint32x4_t w14 = vdupq_n_u32(m_ctx.block2_w[14]);
    uint32x4_t zero = vdupq_n_u32(0);

    uint32x4_t target0 = vld1q_u32(&m_ctx.target[0]);
    uint32x4_t target1 = vld1q_u32(&m_ctx.target[4]);

    uint32_t nonce_vals[4] = {start, start+1, start+2, start+3};
    uint32x4_t nonce_vec = vld1q_u32(nonce_vals);
    uint32x4_t increment = vdupq_n_u32(4);

    for (uint32_t n = start; n < end; n += 4) {
        if (!m_ctx.job_valid.load(std::memory_order_acquire)) break;
        
        uint32x4_t nonce_be = vreinterpretq_u32_u8(
            vrev32q_u8(vreinterpretq_u8_u32(nonce_vec)));

        uint32x4_t W[16];
        W[0] = w0; W[1] = w1; W[2] = w2; W[3] = nonce_be;
        W[4] = w4; 
        W[5] = zero; W[6] = zero; W[7] = zero;
        W[8] = zero; W[9] = zero; W[10] = zero; W[11] = zero;
        W[12] = zero; W[13] = zero; W[14] = w14; W[15] = zero;

        uint32x4_t W_regs[64];
        for(int i=0; i<16; i++) W_regs[i] = W[i];

        for (int i = 16; i < 64; i++) {
            uint32x4_t s0 = veorq_u32(veorq_u32(ROTR_NEON(W_regs[i-15], 7), 
                              ROTR_NEON(W_regs[i-15], 18)), vshrq_n_u32(W_regs[i-15], 3));
            uint32x4_t s1 = veorq_u32(veorq_u32(ROTR_NEON(W_regs[i-2], 17), 
                              ROTR_NEON(W_regs[i-2], 19)), vshrq_n_u32(W_regs[i-2], 10));
            W_regs[i] = vaddq_u32(vaddq_u32(W_regs[i-16], s0), vaddq_u32(W_regs[i-7], s1));
        }

        uint32x4_t a = state0, b = state1, c = state2, d = state3;
        uint32x4_t e = state4, f = state5, g = state6, h = state7;

        for (int i = 0; i < 64; i++) {
            uint32x4_t k_vec = vld1q_u32(&K[i]);
            uint32x4_t w_vec = W_regs[i];
            
            uint32x4_t S1 = veorq_u32(veorq_u32(ROTR_NEON(e, 6), ROTR_NEON(e, 11)), ROTR_NEON(e, 25));
            uint32x4_t ch = veorq_u32(vandq_u32(e, f), vandq_u32(vmvnq_u32(e), g));
            uint32x4_t temp1 = vaddq_u32(vaddq_u32(h, S1), vaddq_u32(ch, vaddq_u32(k_vec, w_vec)));
            uint32x4_t S0 = veorq_u32(veorq_u32(ROTR_NEON(a, 2), ROTR_NEON(a, 13)), ROTR_NEON(a, 22));
            uint32x4_t maj = veorq_u32(veorq_u32(vandq_u32(a, b), vandq_u32(a, c)), vandq_u32(b, c));
            uint32x4_t temp2 = vaddq_u32(S0, maj);
            
            h = g; g = f; f = e; 
            e = vaddq_u32(d, temp1);
            d = c; c = b; b = a; 
            a = vaddq_u32(temp1, temp2);
        }

        state0 = vaddq_u32(state0, a); state1 = vaddq_u32(state1, b);
        state2 = vaddq_u32(state2, c); state3 = vaddq_u32(state3, d);
        state4 = vaddq_u32(state4, e); state5 = vaddq_u32(state5, f);
        state6 = vaddq_u32(state6, g); state7 = vaddq_u32(state7, h);

        uint32x4_t hash0 = vaddq_u32(vdupq_n_u32(0x6A09E667), a);
        uint32x4_t hash1 = vaddq_u32(vdupq_n_u32(0xBB67AE85), b);

        if (checkTargetNEON(hash0, hash1, target0, target1)) {
            uint32_t lanes[4];
            vst1q_u32(lanes, nonce_vec);
            for(int i=0; i<4; i++) {
                uint32_t expected = 0xFFFFFFFF;
                found_nonce.compare_exchange_strong(expected, lanes[i], 
                    std::memory_order_acq_rel);
            }
        }

        nonce_vec = vaddq_u32(nonce_vec, increment);
    }
    
    count += (end - start);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_ctx.last_hash_time.store(now, std::memory_order_relaxed);
}

void SHA256Miner::mine_continuous(std::atomic<uint32_t>& found_nonce, 
                                   std::atomic<bool>& running) {
    uint64_t local_version = 0;
    uint64_t stall_counter = 0;

    while (running.load(std::memory_order_relaxed)) {
        if (!m_ctx.job_valid.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }

        uint64_t current_version = m_ctx.job_version.load(std::memory_order_acquire);
        if (current_version != local_version) {
            local_version = current_version;
            stall_counter = 0;
            continue;
        }

        uint32_t nonce = m_ctx.nonce_start;
        uint32_t batch_end = nonce + HASH_BATCH_SIZE;
        if (batch_end % 4 != 0) batch_end += (4 - (batch_end % 4));
        if (batch_end > m_ctx.nonce_end) batch_end = m_ctx.nonce_end;

        if (nonce < m_ctx.nonce_end) {
            uint64_t batch_count = 0;
            mine_batch_neon(nonce, batch_end, found_nonce, batch_count);
            
            m_hash_count.fetch_add(batch_count, std::memory_order_relaxed);
            m_ctx.nonce_start = batch_end;
            stall_counter = 0;
        } else {
            m_ctx.nonce_start = m_ctx.nonce_end;
            stall_counter++;
            
            if (stall_counter > 5) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            } else {
                std::this_thread::yield();
            }
        }
    }
}

bool SHA256Miner::isJobComplete() const {
    return m_ctx.nonce_start >= m_ctx.nonce_end;
}

void SHA256Miner::difficultyToTarget(double difficulty, uint32_t* target) {
    const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
    double target_double = (difficulty > 0) ? (truediffone / difficulty) : truediffone;
    
    for (int i = 0; i < 8; i++) target[i] = 0;
    
    for (int i = 7; i >= 0 && target_double > 0; --i) {
        target[i] = static_cast<uint32_t>(static_cast<uint64_t>(target_double) & 0xFFFFFFFF);
        target_double /= 4294967296.0;
    }
}
