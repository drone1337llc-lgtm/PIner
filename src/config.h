#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_task_wdt.h>

// ============================================================================
// ⚙️ USER CONFIGURATION
// ============================================================================

// --- Master Settings ---
#define MASTER_I2C_ADDRESS      0x08
#define SLAVE_SCAN_START        0x10
#define SLAVE_SCAN_END          0x70
#define POLL_INTERVAL_MS        1000
#define DIFFICULTY_ADJUST_MS    30000
#define TARGET_SUCCESS_RATE     0.95f
#define MIN_DIFFICULTY          1.0f
#define MAX_DIFFICULTY          100.0f
#define DIFFICULTY_STEP         0.5f

// --- Slave Settings ---
// #define IS_SLAVE              1  // Uncomment for slave builds
#define SLAVE_I2C_ADDRESS       0x16  // Change per slave board (0x10-0x70)

// --- Hardware Pins ---
#ifdef esp32c3
#define I2C_SDA_PIN             8
#define I2C_SCL_PIN             9
#define LADDER_PIN              0
#define LED_PIN                 8
#else
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22
#define LADDER_PIN              34
#define LED_PIN                 2
#endif

// --- Performance ---
#define I2C_CLOCK_SPEED         400000
#define I2C_BUFFER_SIZE         256
#define I2C_PORT                I2C_NUM_0
#define HASH_BATCH_SIZE         256
#define MINING_STACK_SIZE       12000
#define HEARTBEAT_TIMEOUT_MS    10000
#define HASHRATE_UPDATE_MS      1000
#define LED_UPDATE_INTERVAL_MS  10
#define DISPLAY_UPDATE_INTERVAL 500

// --- Debug ---
#define DEBUG_ENABLED           1
#ifndef DEBUG_PRINTF
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#endif

// --- Protocol ---
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB

#endif
