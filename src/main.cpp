#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "config.h"
#include "i2c_protocol.h"

// Globals
WiFiClient poolClient;
WebServer server(8080);
std::vector<uint8_t> slaves;

// Stats
struct {
    uint64_t total_hashes = 0;
    uint32_t shares_accepted = 0;
    uint32_t crc_errors = 0;
    uint64_t start_time = 0;
    float current_diff = 0;
    String current_job_id = "";
    uint8_t header[76];
    bool new_job_available = false;
} stats;

// --- UTILS ---
uint8_t calculateCRC8(const void* data, size_t len) {
    uint8_t crc = 0xff;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return crc;
}

// --- WEB DASHBOARD ---
void handleRoot() {
    uint64_t uptime = (millis() - stats.start_time) / 1000;
    float hr = (uptime > 0) ? (stats.total_hashes / (float)uptime) / 1000.0 : 0;

    String html = "<html><head><meta http-equiv='refresh' content='5'></head>";
    html += "<body style='font-family:monospace; background:#111; color:#0f0; padding:20px;'>";
    html += "<h1>Piner ESP32 Master</h1><hr>";
    html += "<p>Uptime: " + String(uptime) + "s</p>";
    html += "<p>Hashrate: <span style='color:#fff'>" + String(hr, 2) + " kH/s</span></p>";
    html += "<p>Shares Accepted: " + String(stats.shares_accepted) + "</p>";
    html += "<p>CRC Errors: <span style='color:red'>" + String(stats.crc_errors) + "</span></p>";
    html += "<p>Slaves Online: " + String(slaves.size()) + "</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// --- CORE 1: I2C MANAGEMENT ---
void i2cTask(void* pvParameters) {
    while (true) {
        if (slaves.empty()) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }

        // 1. Feed Slaves if new job
        if (stats.new_job_available) {
            for (size_t i = 0; i < slaves.size(); i++) {
                JobI2cRequest req;
                req.cmd = I2C_CMD_FEED;
                req.id = 1;
                req.nonce_start = i * NONCE_RANGE_PER_SLAVE;
                req.difficulty = stats.current_diff;
                memcpy(req.buffer, stats.header, 76);
                req.crc = 0;
                req.crc = calculateCRC8(&req, sizeof(req));

                Wire.beginTransmission(slaves[i]);
                Wire.write((uint8_t*)&req, sizeof(req));
                Wire.endTransmission();
                delay(2);
            }
            stats.new_job_available = false;
        }

        // 2. Harvest Results
        for (uint8_t addr : slaves) {
            uint8_t cmd = I2C_CMD_REQUEST_RESULT;
            Wire.beginTransmission(addr);
            Wire.write(cmd);
            Wire.endTransmission();
            
            delay(2);

            if (Wire.requestFrom(addr, (uint8_t)sizeof(JobI2cResult)) == sizeof(JobI2cResult)) {
                JobI2cResult res;
                Wire.readBytes((uint8_t*)&res, sizeof(res));
                
                uint8_t received_crc = res.crc;
                res.crc = 0;
                if (calculateCRC8(&res, sizeof(res)) == received_crc) {
                    stats.total_hashes += res.processed_nonce;
                    if (res.nonce != 0xFFFFFFFF) {
                        // In a real scenario, you'd push this to a queue for Core 0 to submit
                        Serial.printf("[I2C] Found Nonce 0x%08X from Slave 0x%02X\n", res.nonce, addr);
                    }
                } else {
                    stats.crc_errors++;
                }
            }
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// --- STRATUM PROTOCOL ---
void updateStratum() {
    if (!poolClient.connected()) {
        if (poolClient.connect(POOL_HOST, POOL_PORT)) {
            poolClient.print("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}\n");
            poolClient.printf("{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n", POOL_USER, POOL_PASS);
        }
    }

    if (poolClient.available()) {
        String line = poolClient.readStringUntil('\n');
        JsonDocument doc;
        deserializeJson(doc, line);

        if (doc["method"] == "mining.notify") {
            // Placeholder: Extracting dummy header for example
            // In Duino-Coin/BTC you'd parse the specific job parameters here
            memset(stats.header, 0xAA, 76); 
            stats.current_diff = 1.0f;
            stats.new_job_available = true;
            Serial.println("[Stratum] New Job Received");
        }
    }
}

// --- CORE 0: NETWORK & WEB ---
void setup() {
    Serial.begin(115200);
    stats.start_time = millis();

    // WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi Connected");

    // I2C Init
    Wire.begin(SDA_PIN, SCL_PIN, I2C_FREQ);
    
    // Scan Bus
    Serial.println("Scanning I2C Bus...");
    for (uint8_t i = 1; i < 127; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            slaves.push_back(i);
            Serial.printf("Found Slave at 0x%02X\n", i);
        }
    }

    // Web Server
    server.on("/", handleRoot);
    server.begin();

    // Start I2C Task on Core 1
    xTaskCreatePinnedToCore(i2cTask, "I2CTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
    updateStratum();
    server.handleClient();
    delay(10);
}