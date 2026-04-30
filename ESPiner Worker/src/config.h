#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// I2C CONFIGURATION
// ============================================================================
#define I2C_SDA_PIN           21
#define I2C_SCL_PIN           22
#define I2C_CLOCK_SPEED       100000   // Reduced to 100kHz for bus stability
#define I2C_BASE_ADDRESS      0x10
#define I2C_BUFFER_SIZE       128      // Must match Master's buffer

// ============================================================================
// I2C COMMAND PROTOCOL
// ============================================================================
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC
#define I2C_CMD_STOP            0xAD

// Hardware & Mining
#define LADDER_PIN            34
#define LED_PIN               2
#define HEARTBEAT_TIMEOUT_MS  10000
#define MINING_STACK_SIZE     8192
#define HASH_BATCH_SIZE       4096
#define HASHRATE_UPDATE_MS    1000

#endif