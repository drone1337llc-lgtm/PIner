#ifndef CONFIG_H
#define CONFIG_H

// WiFi Settings
#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASS       "YOUR_PASSWORD"

// Stratum Pool Settings
#define POOL_HOST       "192.168.68.28"
#define POOL_PORT       3333
#define POOL_USER       "bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
#define POOL_PASS       "x"

// I2C Pins (Master)
#define SDA_PIN         21
#define SCL_PIN         22
#define I2C_FREQ        100000 

// Mining Logic
#define I2C_BASE_ADDRESS      0x10
#define MAX_SLAVES            31
#define NONCE_RANGE_PER_SLAVE 0x20000

#endif