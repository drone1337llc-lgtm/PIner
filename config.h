#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// I2C CONFIGURATION
// ============================================================================

#define I2C_SDA_PIN           21
#define I2C_SCL_PIN           22
#define I2C_CLOCK_SPEED       800000
#define I2C_BASE_ADDRESS      0x10

// ============================================================================
// HARDWARE PINS (Board-Specific)
// ============================================================================

#ifdef LCD
  #define LADDER_PIN          27
  #define BUTTON1_GPIO        35
  #define BUTTON2_GPIO        0
  #define TFT_BL_PIN          4
  #define DISPLAY_UPDATE_INTERVAL 2000
  #define STATS_PAGE_INTERVAL 8000
#else
  #define LADDER_PIN          34
#endif

#define BOUNCING_MS           50
#define LED_PIN               2

// ============================================================================
// I2C COMMAND PROTOCOL (MUST MATCH PI)
// ============================================================================

#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC
#define I2C_CMD_STOP            0xAD

// ============================================================================
// MINING CONFIGURATION
// ============================================================================

#define NONCES_PER_JOB        0x10000
#define HEARTBEAT_TIMEOUT_MS  10000

#ifndef MINING_STACK_SIZE
  #define MINING_STACK_SIZE   8192
#endif

// Performance tuning
#define HASH_BATCH_SIZE       4096
#define HASHRATE_UPDATE_MS    1000
#define WDT_TIMEOUT_MS        5000

// NOTE: CONFIG_MBEDTLS_HARDWARE_SHA is already defined by ESP32 framework
// Don't redefine it here

// ============================================================================
// DEBUG (DISABLED FOR SPEED)
// ============================================================================

#ifdef DEBUG_MINER
  #define DEBUG_PRINT(x)      Serial.print(x)
  #define DEBUG_PRINTLN(x)    Serial.println(x)
  #define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#endif
