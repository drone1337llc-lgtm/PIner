#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>

// ============================================================================
// MINING CONFIGURATION
// ============================================================================

#define INITIAL_DIFFICULTY      4.0f
#define MAX_DIFFICULTY          16.0f
#define MIN_DIFFICULTY          1.0f
#define DIFF_ADJUST_INTERVAL    30000UL

// Nonce distribution
#define NONCE_INCREMENT         0x2000
#define NONCE_RANGE_PER_SLAVE   0x10000
#define MAX_NONCE_BATCH         0x100000

// ============================================================================
// I2C CONFIGURATION - 800kHz
// ============================================================================

#define I2C_BUS                 1
#define I2C_BASE_ADDRESS        0x08
#define I2C_MAX_ADDRESS         0x77
#define I2C_CLOCK_SPEED         800000
#define I2C_MAX_MINERS          16

#define I2C_WRITE_TIMEOUT_MS    50
#define I2C_READ_TIMEOUT_MS     50
#define I2C_INTER_TRANSACTION_US 500

#define JOB_FEED_INTERVAL_MS    50
#define RESULT_HARVEST_INTERVAL_MS 25
#define I2C_RETRY_COUNT         3

// ============================================================================
// STRATUM POOL CONFIGURATION
// ============================================================================

#define DEFAULT_POOL_HOST       "192.168.68.28"
#define DEFAULT_POOL_PORT       3333
#define DEFAULT_POOL_USER       "bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
#define DEFAULT_POOL_PASS       "x"

#define STRATUM_RECONNECT_MS    2000UL
#define STRATUM_HEARTBEAT_MS    30000UL
#define SHARE_SUBMIT_TIMEOUT_MS 5000UL
#define MAX_PENDING_SHARES      32

// ============================================================================
// BUFFER & MEMORY CONFIGURATION
// ============================================================================

#define BUFFER_SIZE             2048
#define MAX_MERKLE_BRANCHES     32
#define COINBASE_BUFFER_SIZE    512
#define HEADER_BUFFER_SIZE      80

#define MAIN_LOOP_INTERVAL_MS   100
#define WEB_UPDATE_INTERVAL_MS  2000
#define STATS_UPDATE_INTERVAL_MS 1000
#define I2C_LOOP_INTERVAL_MS    50

// ============================================================================
// I2C COMMAND PROTOCOL
// ============================================================================

#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC

// NOTE: JobI2cRequest and JobI2cResult structs are defined in i2c_master.h

#endif // CONFIG_H
