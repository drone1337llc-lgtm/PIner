#ifndef PI_MINER_CONFIG_H
#define PI_MINER_CONFIG_H

#include <cstdint>

#define NUM_MINING_THREADS      3
#define NONCES_PER_THREAD       0x100000        
#define HASH_BATCH_SIZE         2048            
#define HASHRATE_UPDATE_MS      1000      
#define WDT_INTERVAL_MS         5000    

#define DEFAULT_POOL_HOST       "pool.solomining.de"
#define DEFAULT_POOL_PORT       3333
#define DEFAULT_POOL_USER       "bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
#define DEFAULT_POOL_PASS       "x"
#define DEFAULT_DIFFICULTY      50.0  
#define DEFAULT_POOL_DIFFICULTY  50.0

#define WEB_PORT                8080
#define WEB_REFRESH_MS          2000

#define SHARE_CHECK_EVERY       64             

#endif