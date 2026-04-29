#ifndef CONFIG_H
#define CONFIG_H

// I2C Configuration
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_CLOCK_SPEED 800000
#define I2C_CMD_FEED            0xA1
#define I2C_CMD_REQUEST_RESULT  0xA9
#define I2C_CMD_SLAVE_RESULT    0xAA
#define I2C_CMD_PING            0xAB
#define I2C_CMD_RESET           0xAC
#define I2C_CMD_STOP            0xAD
#define I2C_BASE_ADDRESS        0x10
#define HEARTBEAT_TIMEOUT_MS    5000

#define MINING_STACK_SIZE 10240    
#define MINING_TASK_PRIORITY 2
#define NONCES_PER_JOB          0x10000

#ifdef LCD
// Display Configuration
#define DISPLAY_UPDATE_INTERVAL 3000
#define STATS_PAGE_INTERVAL 6000
// Button Configuration
#define BUTTON1_GPIO 35
#define BUTTON2_GPIO 0
#define BOUNCING_MS 50
// LED Configuration
#define TFT_BL_PIN 4
#define LED_PIN 2
#endif

// LED Configuration
#define LED_PIN 2

// Watchdog Configuration
#define WDT_TIMEOUT 10

// Debug Configuration
#define DEBUG_ENABLED 1

// I2C Commands
#define I2C_CMD_FEED 0xA1
#define I2C_CMD_REQUEST_RESULT 0xA9
#define I2C_CMD_SLAVE_RESULT 0xAA

#endif // CONFIG_H
