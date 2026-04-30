#ifndef CONFIG_H
#define CONFIG_H

#ifdef IdeaSparkS3
    // Onboard I2C (Wire0) - Usually handled by the board libs
    #define ONBOARD_SDA 8 
    #define ONBOARD_SCL 9

    // Cluster I2C (Wire1) - Dedicated to your slaves
    #define CLUSTER_SDA 17 
    #define CLUSTER_SCL 18
    #define I2C_FREQ 400000 // 400kHz is stable for short runs
#endif

#define MAX_SLAVES 7
#define MASTER_ADDRESS 0x01

// WiFi Settings
#define WIFI_SSID       "Patricia27680"
#define WIFI_PASS       "FluffyBentley"

// Stratum Pool Settings
#define POOL_HOST       "pool.solomining.de"
#define POOL_PORT       3333
#define POOL_USER       "bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8"
#define POOL_PASS       "x"

// I2C Settings
#define SDA_PIN         21
#define SCL_PIN         22
#ifndef I2C_FREQ
  #define I2C_FREQ 100000
#endif  // Reduced to 100kHz for stability
#define I2C_BUFFER_SIZE 128      // Essential for 84-byte payloads

// Mining Logic
#define I2C_SCAN_START        0x10
#define I2C_SCAN_END          0x40
#ifndef MAX_SLAVES
  #define MAX_SLAVES 31
#endif
#define NONCE_RANGE_PER_SLAVE 0x20000

#endif