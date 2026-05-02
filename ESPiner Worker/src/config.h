#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// ⚙️ USER CONFIGURATION
// ============================================================================

// --- Master Settings ---
#define SLAVE_SCAN_START        0x10
#define SLAVE_SCAN_END          0x70
#define POLL_INTERVAL_MS        100
#define DIFFICULTY_ADJUST_MS    60000
#define TARGET_SUCCESS_RATE     0.90f
#define MIN_DIFFICULTY          0.001f
#define MAX_DIFFICULTY          3.0f
#define DIFFICULTY_STEP         0.5f
#define SHARE_SUBMIT_TIMEOUT_MS 5000

// --- Hardware Pins ---
#ifdef esp32dev
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22
#define LED_PIN                 2
#endif
#ifdef esp32c3
#define I2C_SDA_PIN             10
#define I2C_SCL_PIN             9
#define LED_PIN                 8
#endif

#define LADDER_PIN              34

// --- Performance ---
#define I2C_CLOCK_SPEED         250000
#define I2C_BUFFER_SIZE         256
#define I2C_PORT                I2C_NUM_0
#define HASH_BATCH_SIZE         64
#define MINING_STACK_SIZE       12000
#define HEARTBEAT_TIMEOUT_MS    10000
#define HASHRATE_UPDATE_MS      1000
#define LED_UPDATE_INTERVAL_MS  10

// --- Debug ---
#define DEBUG_ENABLED           1
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)

// --- Protocol ---
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB

#endif
