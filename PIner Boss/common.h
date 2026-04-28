#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <cstdint>

struct DisplayStats {
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t total_nonces;
    uint64_t uptime_seconds;
    double hashrate;
    double difficulty;
    std::string pool_host;
    std::string pool_user;
    bool pool_connected;
    uint8_t esp32_count;
    std::vector<uint8_t> esp32_addresses;
    std::string current_job_id;
    uint32_t pending_shares;
};

#endif // COMMON_H
