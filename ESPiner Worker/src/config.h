#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
#ifndef DEBUG_PRINTF
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#endif

#define DEBUG_ENABLED 1

// ============================================================================
// I2C CONFIGURATION
// ============================================================================
#ifdef esp32dev
#define I2C_SDA_PIN           21
#define I2C_SCL_PIN           22
#endif
#ifdef spark
#define I2C_SDA_PIN           11
#define I2C_SCL_PIN           14
#endif
#ifdef esp32c3
#define I2C_SDA_PIN           6
#define I2C_SCL_PIN           7
#endif
#define I2C_CLOCK_SPEED       100000
#define I2C_BASE_ADDRESS      0x10
#define I2C_MAX_ADDRESS       0x70
#define I2C_BUFFER_SIZE       128

// ============================================================================
// I2C COMMAND PROTOCOL
// ============================================================================
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC
#define I2C_CMD_STOP            0xAD

// ============================================================================
// HARDWARE PINS
// ============================================================================
#define LADDER_PIN            34
#define LED_PIN               2

// ============================================================================
// TIMING & PERFORMANCE
// ============================================================================
#define HEARTBEAT_TIMEOUT_MS  10000
#define MINING_STACK_SIZE     12000
#define HASH_BATCH_SIZE       256
#define HASHRATE_UPDATE_MS    1000
#define DISPLAY_UPDATE_INTERVAL 500
#define LED_UPDATE_INTERVAL_MS 10

// ============================================================================
// DISPLAY CONFIGURATION (Optional)
// ============================================================================
// #define LCD  // Uncomment to enable display
#ifdef LCD
  #define TFT_BL_PIN 10
  #define BUTTON1_GPIO 11
#endif

#endif
