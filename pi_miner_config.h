#ifndef PI_MINER_CONFIG_H
#define PI_MINER_CONFIG_H

#include <cstdint>

// 3 cores for mining (cores 1, 2, 3), 2 workers per core = 6 total workers
#define NUM_MINING_THREADS      6
#define WORKERS_PER_CORE        2
#define MINING_CORES            3
#define MINING_CORE_START       1               // Start at core 1, leave core 0 for system

#define NONCES_PER_THREAD       0x30000         // Reduced per worker (more workers share the load)
#define HASH_BATCH_SIZE         2048            // Smaller batches for faster job switching
#define HASHRATE_UPDATE_MS      1000            // Faster updates for more workers
#define HASHRATE_SAMPLE_COUNT   20
#define WDT_INTERVAL_MS         5000

#define DEFAULT_POOL_HOST       "pool.solomining.de"
#define DEFAULT_POOL_PORT       3333
#define DEFAULT_POOL_USER       "bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
#define DEFAULT_POOL_PASS       "x"
#define DEFAULT_DIFFICULTY      0.0             // Let pool set difficulty (settles at 0)
#define DEFAULT_POOL_DIFFICULTY 0.0

#define WEB_PORT                8080
#define WEB_REFRESH_MS          2000

#define SHARE_CHECK_EVERY       32              // Check more frequently with more workers

#define CACHE_LINE_SIZE         64              // A53 has 64-byte cache lines

#define THERMAL_THROTTLE_TEMP   75.0
#define CPU_FREQ_CHECK_MS       30000

#endif
