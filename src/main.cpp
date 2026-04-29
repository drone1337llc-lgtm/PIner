#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "i2c_protocol.h"
#include "config.h" // Ensure WiFi/Pool credentials are in here

// --- CRC8 Table from Gist ---
const uint8_t s_crc8_table[256] = {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97, 0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4, 0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11, 0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52, 0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA, 0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9, 0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C, 0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F, 0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED, 0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE, 0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B, 0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0, 0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93, 0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15, 0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

uint8_t CommandCrc8(const void* data, size_t len) {
    const uint8_t* ptr = (const uint8_t*)data;
    uint8_t crc = 0xFF;
    crc = s_crc8_table[crc ^ ptr[0]];
    for (size_t n = 2; n < len; ++n) crc = s_crc8_table[crc ^ ptr[n]];
    return crc;
}

// --- Waveshare 3.5 LCD Config ---
class LGFX_Waveshare : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Touch_FT5x06  _touch_instance;
public:
    LGFX_Waveshare() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = HSPI_HOST; cfg.spi_mode = 0; cfg.freq_write = 40000000;
            cfg.pin_sclk = 14; cfg.pin_mosi = 13; cfg.pin_miso = 12; cfg.pin_dc = 2;
            _bus_instance.config(cfg); _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = 15; cfg.pin_rst = -1; cfg.panel_width = 320; cfg.panel_height = 480;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.pin_int = 33; cfg.bus_shared = true; cfg.i2c_port = 0; cfg.i2c_addr = 0x38;
            cfg.pin_sda = 21; cfg.pin_scl = 22; cfg.freq = 400000;
            _touch_instance.config(cfg); _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};

LGFX_Waveshare tft;
LGFX_Sprite canvas(&tft);

// --- Stats & State ---
WiFiClient poolClient;
WebServer server(8080);
std::vector<uint8_t> slaves;

struct {
    uint64_t total_hashes = 0;
    uint32_t shares_accepted = 0;
    uint32_t crc_errors = 0;
    uint64_t start_time = 0;
    float difficulty = 0.0001;
    String job_id = "None";
    uint8_t header[76];
    bool new_job = false;
} stats;

void initIOExpander() {
    Wire.beginTransmission(0x20); Wire.write(0x03); Wire.write(0xFC); Wire.endTransmission();
    Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x03); Wire.endTransmission();
}

void hexToBytes(String hex, uint8_t* bytes) {
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        bytes[i / 2] = (char)strtol(hex.substring(i, i + 2).c_str(), NULL, 16);
    }
}

// --- Stratum Logic ---
void handleStratum() {
    if (!poolClient.connected()) {
        if (poolClient.connect(POOL_HOST, POOL_PORT)) {
            poolClient.print("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}\n");
            poolClient.printf("{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n", POOL_USER, POOL_PASS);
        }
        return;
    }
    if (poolClient.available()) {
        String line = poolClient.readStringUntil('\n');
        JsonDocument doc;
        if (deserializeJson(doc, line)) return;
        if (doc["method"] == "mining.notify") {
            stats.job_id = doc["params"][0].as<String>();
            hexToBytes(doc["params"][1].as<String>(), &stats.header[0]);  // Version
            hexToBytes(doc["params"][2].as<String>(), &stats.header[4]);  // PrevHash
            hexToBytes(doc["params"][3].as<String>(), &stats.header[36]); // Merkle
            hexToBytes(doc["params"][7].as<String>(), &stats.header[68]); // NTime
            hexToBytes(doc["params"][8].as<String>(), &stats.header[72]); // NBits
            stats.new_job = true;
        } else if (doc["method"] == "mining.set_difficulty") {
            stats.difficulty = doc["params"][0].as<float>();
        }
    }
}

// --- I2C Mining Core (Core 1) ---
void i2cTask(void* pvParameters) {
    while (true) {
        if (slaves.empty()) { vTaskDelay(1000); continue; }
        
        // 1. FEED
        if (stats.new_job) {
            uint8_t nonce_offset = 0x20; 
            for (uint8_t addr : slaves) {
                JobI2cRequest req;
                req.cmd = I2C_CMD_FEED;
                req.id = 1;
                req.nonce_start_byte = nonce_offset;
                nonce_offset += 0x10;
                req.difficulty = stats.difficulty;
                memcpy(req.buffer, stats.header, 76);
                req.crc = CommandCrc8(&req, sizeof(req));

                Wire.beginTransmission(addr);
                Wire.write((uint8_t*)&req, sizeof(req));
                Wire.endTransmission();
                vTaskDelay(5);
            }
            stats.new_job = false;
        }

        // 2. HARVEST (Gist Logic)
        uint8_t harvest_req[2];
        harvest_req[0] = I2C_CMD_REQUEST_RESULT;
        harvest_req[1] = CommandCrc8(harvest_req, 2);

        for (uint8_t addr : slaves) {
            Wire.beginTransmission(addr);
            Wire.write(harvest_req, 2);
            Wire.endTransmission();
            vTaskDelay(5);

            if (Wire.requestFrom(addr, (uint8_t)sizeof(JobI2cResult)) == sizeof(JobI2cResult)) {
                JobI2cResult res;
                Wire.readBytes((uint8_t*)&res, sizeof(res));
                if (CommandCrc8(&res, sizeof(res)) == res.crc) {
                    stats.total_hashes += res.processed_nonce;
                    if (res.nonce != 0xFFFFFFFF) {
                        poolClient.printf("{\"id\":4,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"00000000\",\"%08x\"]}\n", 
                            POOL_USER, stats.job_id.c_str(), res.nonce);
                        stats.shares_accepted++;
                    }
                } else { stats.crc_errors++; }
            }
        }
        vTaskDelay(500);
    }
}

// --- UI Dashboard ---
void displayTask(void* pvParameters) {
    tft.init(); tft.setRotation(0); canvas.createSprite(320, 480);
    while (true) {
        canvas.fillSprite(TFT_BLACK);
        canvas.setTextColor(TFT_GREEN); canvas.setTextSize(2);
        canvas.drawString("ESPINER MASTER", 10, 10);
        uint64_t uptime = (millis() - stats.start_time) / 1000;
        float hr = (uptime > 0) ? (stats.total_hashes / (float)uptime) / 1000.0 : 0;
        canvas.setCursor(10, 60); canvas.printf("Hashrate: %.2f kH/s", hr);
        canvas.setCursor(10, 90); canvas.printf("Accepted: %u", stats.shares_accepted);
        canvas.setCursor(10, 120); canvas.printf("Workers: %d", slaves.size());
        canvas.pushSprite(0, 0);
        vTaskDelay(500);
    }
}

void setup() {
    Serial.begin(115200); stats.start_time = millis();
    Wire.begin(21, 22, 100000); initIOExpander();
    for (uint8_t i = 0x10; i <= 0x77; i++) {
        Wire.beginTransmission(i); if (Wire.endTransmission() == 0 && i != 0x20 && i != 0x38) slaves.push_back(i);
    }
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    xTaskCreatePinnedToCore(displayTask, "UI", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(i2cTask, "Mining", 4096, NULL, 1, NULL, 1);
}

void loop() { handleStratum(); server.handleClient(); vTaskDelay(10); }