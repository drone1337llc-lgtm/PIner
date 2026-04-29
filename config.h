#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>
// I2C Pins (adjust for your board)
#define I2C_SDA_PIN           21
#define I2C_SCL_PIN           22
#define I2C_CLOCK_SPEED       800000

// ============================================================================
// HARDWARE & ADDRESSING
// ============================================================================
#ifdef LCD
  #define LADDER_PIN          27    
  #define BUTTON1_GPIO        35   
  #define BUTTON2_GPIO        0    
  #define TFT_BL_PIN          4     
  #define BOUNCING_MS         50    
  #define DISPLAY_UPDATE_INTERVAL 2000
  #define STATS_PAGE_INTERVAL 8000  
#elif defined(ARDUINO_ESP32_DEV) || defined(esp32dev)
  #define LADDER_PIN          34    
#else
  #define LADDER_PIN          34    
#endif

#define I2C_BASE_ADDRESS    0x10  
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define I2C_CLOCK_SPEED     800000 

// ============================================================================
// I2C COMMAND PROTOCOL (MUST MATCH PI MASTER EXACTLY)
// ============================================================================
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC
#define I2C_CMD_STOP            0xAD

// ============================================================================
// MINING CONFIGURATION (MUST MATCH PI MASTER)
// ============================================================================
#define NONCES_PER_JOB      0x100000     // MUST MATCH: NONCE_RANGE_PER_SLAVE in config.h (PI)
#define HEARTBEAT_TIMEOUT_MS 10000
#define LED_PIN             2

#ifndef MINING_STACK_SIZE
  #define MINING_STACK_SIZE 12000
#endif

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
#define DEBUG_SERIAL          Serial
#define DEBUG_PRINT(x)        // Disabled
#define DEBUG_PRINTLN(x)      // Disabled
#define DEBUG_PRINTF(...)     // Disabled

#if DEBUG_SERIAL
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTF(...)
  #define DEBUG_PRINTLN(...)
#endif

#endif
