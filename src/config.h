#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// TTGO T-Display Hardware Pins
// ============================================================================
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define ADC_PIN         34
#define BUTTON1_PIN     35
#define BUTTON2_PIN     0
#define ADC_POWER_PIN   14

// ============================================================================
// WIFI SETTINGS
// ============================================================================
#define WIFI_SSID       "Patricia27680"
#define WIFI_PASS       "FluffyBentley"

// ============================================================================
// STRATUM POOL SETTINGS
// ============================================================================
#define POOL_URL        "pool.solomining.de"
#define POOL_PORT       3333
#define POOL_USER       "bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
#define POOL_PASS       "x"

// ============================================================================
// I2C CONFIGURATION
// ============================================================================
#define I2C_FREQ        100000
#define I2C_BUFFER_SIZE 128

// ============================================================================
// SLAVE CONFIGURATION
// ============================================================================
#define I2C_SCAN_START      0x10
#define I2C_SCAN_END        0x70
#define MAX_SLAVES          31
#define NONCE_RANGE_PER_SLAVE 0x20000

// ============================================================================
// DISPLAY CONFIGURATION
// ============================================================================
#define DISPLAY_UPDATE_MS 500
#define SCREEN_WIDTH      240
#define SCREEN_HEIGHT     135

// ============================================================================
// MINING CONFIGURATION
// ============================================================================
#define MINIMUM_ACCEPTABLE_DIFFICULTY 0.0001f
#define HASHRATE_UPDATE_MS 1000
#define I2C_POLL_INTERVAL_MS 100
#define I2C_SCAN_INTERVAL_MS 10000

#endif
