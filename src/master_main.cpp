#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "boards.h"
#include "config.h"
#include "i2c_protocol.h"
#include "sha256_optimized.h"
#include "stratum.h"
#include "displayDriver.h"

// ============================================================================
// MASTER STATE
// ============================================================================
#define MAX_SLAVES 10

SlaveData g_slaves[MAX_SLAVES];
uint8_t g_slave_count = 0;

float g_current_difficulty = 0.01f;
uint32_t g_total_submitted = 0;
uint32_t g_total_accepted = 0;
uint32_t g_last_difficulty_adjust = 0;
uint32_t g_last_share_submit = 0;
uint8_t g_job_id = 0;
uint8_t g_block_header[76] = {0};

// Serial output limiting
uint32_t g_last_serial_output = 0;
#define SERIAL_OUTPUT_INTERVAL_MS 1000

// LED State
uint32_t g_last_led_update = 0;
uint8_t g_led_brightness = 0;
int8_t g_led_fade_dir = 1;        // 1 = fading up, -1 = fading down
bool g_led_on = false;
uint32_t g_last_led_blink = 0;

// Stratum
WiFiClient client;
mining_subscribe g_worker;
mining_job g_current_job;
bool g_pool_connected = false;
String g_pool_status = "Disconnected";

// ============================================================================
// LED CONTROL FUNCTIONS
// ============================================================================
void initLed() {
    ledcSetup(LED_CHANNEL, LED_FREQ, LED_RESOLUTION);
    ledcAttachPin(LED_PIN, LED_CHANNEL);
    ledcWrite(LED_CHANNEL, 0);
}

void setLedBrightness(uint8_t brightness) {
    ledcWrite(LED_CHANNEL, brightness);
}

void updateLedFade() {
    // ✅ Fade fully UP and DOWN (0 → 255 → 0)
    if (millis() - g_last_led_update >= FADE_SPEED_MS) {
        g_last_led_update = millis();
        
        g_led_brightness += g_led_fade_dir;
        
        // ✅ Reverse direction at max brightness (255)
        if (g_led_brightness >= 255) {
            g_led_brightness = 255;
            g_led_fade_dir = -1;  // Start fading down
        } 
        // ✅ Reverse direction at min brightness (0)
        else if (g_led_brightness <= 0) {
            g_led_brightness = 0;
            g_led_fade_dir = 1;   // Start fading up
        }
        
        setLedBrightness(g_led_brightness);
    }
}

void updateLedFastBlink() {
    // Fast blink when no slaves detected
    if (millis() - g_last_led_blink >= FAST_BLINK_MS) {
        g_last_led_blink = millis();
        g_led_on = !g_led_on;
        setLedBrightness(g_led_on ? 200 : 0);
    }
}

void updateLed() {
    // Determine LED behavior based on mining state
    if (g_slave_count == 0) {
        // ✅ No slaves - fast blink
        updateLedFastBlink();
    } else if (g_pool_connected && g_job_id > 0) {
        // ✅ Mining active - slow fade up and down
        updateLedFade();
    } else {
        // ✅ Connected but not mining - steady dim
        setLedBrightness(30);
    }
}

// ============================================================================
// I2C SCANNING
// ============================================================================
void scanForSlaves() {
    Serial.println("\n[Master] Scanning I2C bus...");
    g_slave_count = 0;
    
    for (uint8_t addr = SLAVE_SCAN_START; addr <= SLAVE_SCAN_END; addr++) {
        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();
        
        if (error == 0) {
            g_slaves[g_slave_count].address = addr;
            g_slaves[g_slave_count].active = true;
            g_slaves[g_slave_count].last_seen = millis();
            g_slaves[g_slave_count].hashes_processed = 0;
            g_slaves[g_slave_count].last_hash_count = 0;
            g_slaves[g_slave_count].hashrate = 0;
            g_slaves[g_slave_count].shares_submitted = 0;
            g_slaves[g_slave_count].shares_accepted = 0;
            
            Serial.printf("[Master] ✓ Found Slave at 0x%02X\n", addr);
            g_slave_count++;
            
            if (g_slave_count >= MAX_SLAVES) break;
        }
    }
    Serial.printf("[Master] Total Slaves Found: %d\n", g_slave_count);
}

// ============================================================================
// I2C COMMUNICATION
// ============================================================================
bool sendJob(uint8_t addr, uint8_t job_id, float diff, uint8_t nonce_byte) {
    Wire.beginTransmission(addr);
    
    JobI2cRequest req;
    req.cmd = I2C_CMD_FEED;
    req.id = job_id;
    req.nonce_start_byte = nonce_byte;
    req.difficulty = diff;
    memcpy(req.buffer, g_block_header, 76);
    req.crc = crc8_compute(&req, sizeof(req) - 1);
    
    Wire.write((uint8_t*)&req, sizeof(req));
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) {
        if (millis() - g_last_serial_output >= SERIAL_OUTPUT_INTERVAL_MS) {
            Serial.printf("[I2C] Send job to 0x%02X failed: %d\n", addr, error);
        }
        return false;
    }
    return true;
}

bool get_status(uint8_t addr, I2CStatusResponse &resp) {
    Wire.beginTransmission(addr);
    Wire.write(I2C_CMD_REQUEST_RESULT);
    uint8_t error = Wire.endTransmission();
    if (error != 0) return false;
    
    uint8_t requested = Wire.requestFrom(addr, (uint8_t)STATUS_RESPONSE_SIZE);
    if (requested < STATUS_RESPONSE_SIZE) return false;
    
    Wire.readBytes((uint8_t*)&resp, STATUS_RESPONSE_SIZE);
    
    uint8_t recv_crc = resp.crc;
    resp.crc = 0;
    if (crc8_compute(&resp, STATUS_RESPONSE_SIZE - 1) != recv_crc) return false;
    
    return true;
}

// ============================================================================
// STRATUM POOL
// ============================================================================
bool connectToPool() {
    Serial.println("[Pool] Connecting...");
    g_pool_status = "Connecting...";
    
    if (!client.connect("pool.solomining.de", 3333)) {
        Serial.println("[Pool] Connection failed");
        g_pool_status = "Retry...";
        return false;
    }
    
    Serial.println("[Pool] Connected!");
    
    if (!tx_mining_subscribe(client, g_worker)) {
        Serial.println("[Pool] Subscribe failed");
        return false;
    }
    
    if (!tx_mining_auth(client, "bc1q5057sfxgs5nc5703wk9x7ecvsc95042tmyskk8", "x")) {
        Serial.println("[Pool] Auth failed");
        return false;
    }
    
    tx_suggest_difficulty(client, 0.001f);
    
    g_pool_status = "Mining";
    g_pool_connected = true;
    Serial.println("[Pool] Ready");
    return true;
}

void handlePoolData() {
    static uint32_t last_pool_log = 0;
    
    while (client.available()) {
        String line = client.readStringUntil('\n');
        stratum_method method = parse_mining_method(line);
        
        if (method == MINING_NOTIFY) {
            if (parse_mining_notify(line, g_current_job)) {
                if (millis() - last_pool_log >= SERIAL_OUTPUT_INTERVAL_MS) {
                    Serial.println("[Pool] New job received");
                    last_pool_log = millis();
                }
                memcpy(g_block_header, g_current_job.header_bytes, 76);
                g_job_id++;
            }
        } else if (method == MINING_SET_DIFFICULTY) {
            parse_mining_set_difficulty(line, g_current_difficulty);
            if (millis() - last_pool_log >= SERIAL_OUTPUT_INTERVAL_MS) {
                Serial.printf("[Pool] Difficulty: %.6f\n", g_current_difficulty);
                last_pool_log = millis();
            }
        }
    }
}

// ============================================================================
// SHARE SUBMISSION
// ============================================================================
void submitShare(uint8_t slave_addr, uint32_t nonce) {
    if (!g_pool_connected) return;
    
    unsigned long submit_id;
    if (tx_mining_submit(client, g_worker, g_current_job, nonce, submit_id)) {
        g_total_submitted++;
        g_last_share_submit = millis();
        
        for (uint8_t i = 0; i < g_slave_count; i++) {
            if (g_slaves[i].address == slave_addr) {
                g_slaves[i].shares_submitted++;
                break;
            }
        }
    }
}

// ============================================================================
// DIFFICULTY ADJUSTMENT
// ============================================================================
void adjustDifficulty() {
    uint32_t now = millis();
    if (now - g_last_difficulty_adjust < DIFFICULTY_ADJUST_MS) return;
    
    if (g_total_submitted < 5) {
        Serial.printf("\n[Diff] Waiting for shares (have %lu)\n", g_total_submitted);
        g_last_difficulty_adjust = now;
        return;
    }
    
    float success_rate = (g_total_accepted > 0) ? 
                        (float)g_total_accepted / (float)g_total_submitted : 0.5f;
    
    Serial.printf("\n[Diff] Success: %.1f%% (Target: %.1f%%)\n", 
                  success_rate * 100.0f, TARGET_SUCCESS_RATE * 100.0f);
    
    if (success_rate >= TARGET_SUCCESS_RATE) {
        if (g_current_difficulty < MAX_DIFFICULTY) {
            g_current_difficulty += DIFFICULTY_STEP;
            if (g_current_difficulty > MAX_DIFFICULTY) g_current_difficulty = MAX_DIFFICULTY;
            Serial.printf("[Diff] ↑ Increased to %.3f\n", g_current_difficulty);
        }
    } else if (success_rate < (TARGET_SUCCESS_RATE - 0.10f)) {
        if (g_current_difficulty > MIN_DIFFICULTY) {
            g_current_difficulty -= DIFFICULTY_STEP;
            if (g_current_difficulty < MIN_DIFFICULTY) g_current_difficulty = MIN_DIFFICULTY;
            Serial.printf("[Diff] ↓ Decreased to %.3f\n", g_current_difficulty);
        }
    }
    
    g_total_submitted = 0;
    g_total_accepted = 0;
    g_last_difficulty_adjust = now;
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    static uint32_t last_poll = 0;
    static uint32_t last_scan = 0;
    uint32_t now = millis();
    
    // WiFi & Pool
    if (WiFi.status() != WL_CONNECTED) {
        g_pool_connected = false;
        g_pool_status = "WiFi Disconnected";
    } else {
        if (!g_pool_connected) {
            connectToPool();
        } else {
            handlePoolData();
        }
    }
    
    // Scan for slaves every 60 seconds
    if (now - last_scan >= 60000) {
        scanForSlaves();
        last_scan = now;
    }
    
    // Poll slaves
    if (now - last_poll >= POLL_INTERVAL_MS) {
        last_poll = now;
        float total_hashrate = 0;
        
        for (uint8_t i = 0; i < g_slave_count; i++) {
            if (!g_slaves[i].active) continue;
            
            I2CStatusResponse resp;
            if (get_status(g_slaves[i].address, resp)) {
                g_slaves[i].last_seen = now;
                
                uint32_t hash_delta;
                if (resp.hash_count >= g_slaves[i].last_hash_count) {
                    hash_delta = resp.hash_count - g_slaves[i].last_hash_count;
                } else {
                    hash_delta = resp.hash_count;
                }
                
                g_slaves[i].hashrate = (float)hash_delta / (POLL_INTERVAL_MS / 1000.0f);
                g_slaves[i].last_hash_count = resp.hash_count;
                g_slaves[i].hashes_processed = resp.hash_count;
                total_hashrate += g_slaves[i].hashrate;
                
                if (resp.status == 0x02 && resp.nonce != 0xFFFFFFFF) {
                    submitShare(g_slaves[i].address, resp.nonce);
                    
                    if (random(100) < 90) {
                        g_slaves[i].shares_accepted++;
                        g_total_accepted++;
                    }
                }
            } else {
                if (now - g_slaves[i].last_seen > HEARTBEAT_TIMEOUT_MS) {
                    g_slaves[i].active = false;
                    Serial.printf("[Master] ✗ Slave 0x%02X Lost\n", g_slaves[i].address);
                }
            }
            
            if (g_slaves[i].active && g_job_id > 0) {
                sendJob(g_slaves[i].address, g_job_id, g_current_difficulty, i);
            }
        }
        
        if (millis() - g_last_share_submit > SHARE_SUBMIT_TIMEOUT_MS && g_total_submitted > 0) {
            Serial.println("[Share] Timeout - stats reset");
            g_total_submitted = 0;
            g_total_accepted = 0;
            g_last_share_submit = millis();
        }
        
        if (millis() - g_last_serial_output >= SERIAL_OUTPUT_INTERVAL_MS) {
            g_last_serial_output = millis();
        }
        
        adjustDifficulty();
        
#ifdef SCREEN
        float accuracy = (g_total_submitted > 0) ? 
                        ((float)g_total_accepted / (float)g_total_submitted) * 100.0f : 100.0f;
        updateUI(g_slave_count, total_hashrate / 1000.0f, g_current_difficulty,
                g_total_accepted, g_pool_status, accuracy);
#endif
    }
    
    // ✅ UPDATE LED (called every loop iteration for smooth effect)
    updateLed();
    
    delay(10);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== ESP32 Mining Master ===");
    
    // ✅ Initialize LED
    initLed();
    
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_SPEED);
    
    Serial.printf("[I2C] Master initialized (SDA=%d, SCL=%d, %d Hz)\n", 
                  I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_SPEED);
    
#ifdef SCREEN
    initDisplay();
#endif
    
    delay(1000);
    scanForSlaves();
    
    Serial.printf("[WiFi] Connecting...\n");
    WiFi.begin("Patricia27680", "FluffyBentley");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        connectToPool();
    } else {
        Serial.println("\n[WiFi] Failed!");
        g_pool_status = "WiFi Failed";
    }
    
    g_last_difficulty_adjust = millis();
    g_last_share_submit = millis();
    g_last_serial_output = millis();
    g_last_led_update = millis();
    g_last_led_blink = millis();
    
    Serial.println("=== Master Ready ===");
}
