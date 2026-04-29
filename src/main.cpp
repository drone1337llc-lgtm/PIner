#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "i2c_protocol.h"
#include "config.h"

// --- Professional UI Color Palette (BGR Corrected) ---
#define C_BG          0x0000 // Deep Black
#define C_HEADER_BG   0xBDD7 // Light Grey for Headers
#define C_PANEL       0x2104 // Gunmetal Grey Panels
#define C_TEXT        0xFFFF // Pure White

// Status Colors
#define C_STATUS_OK    0x03E0 // Matrix Green
#define C_STATUS_WARN  0xFDA0 // Safety Orange
#define C_STATUS_CRIT  0xF800 // Power Red

// --- 1. GLOBALS & CRC8 ---
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

// --- 2. HARDWARE DRIVER ---
class LGFX_Waveshare : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Touch_FT5x06  _touch_instance; 
public:
    LGFX_Waveshare() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = VSPI_HOST; cfg.freq_write = 40000000;
            cfg.pin_sclk = 18; cfg.pin_mosi = 23; cfg.pin_miso = 19; cfg.pin_dc = 27;
            _bus_instance.config(cfg); _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = 5; cfg.panel_width = 320; cfg.panel_height = 480;
            cfg.invert = true; cfg.rgb_order = true;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.pin_int = 37; cfg.i2c_addr = 0x38; cfg.pin_sda = 21; cfg.pin_scl = 22;
            _touch_instance.config(cfg); _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};

LGFX_Waveshare tft;
LGFX_Sprite canvas(&tft);
WiFiClient poolClient;
std::vector<uint8_t> active_slaves;
bool i2c_ready = false;

struct {
    uint64_t hashes = 0;
    uint32_t accepted = 0;
    uint32_t rejected = 0;
    uint64_t start_time = 0;
    float difficulty = 0;
    String job_id = "INIT";
    uint8_t header[76];
    bool new_job = false;
    bool wifi_up = false;
} stats;

// --- 3. UTILITIES & STRATUM ---
void hexToBytes(String hex, uint8_t* bytes) {
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        bytes[i / 2] = (char)strtol(hex.substring(i, i + 2).c_str(), NULL, 16);
    }
}

void handleStratum() {
    if (!poolClient.connected()) {
        if (poolClient.connect(POOL_HOST, POOL_PORT)) {
            poolClient.print("{\"id\":1,\"method\":\"subscribe\",\"params\":[]}\n");
            poolClient.printf("{\"id\":2,\"method\":\"authorize\",\"params\":[\"%s\",\"%s\"]}\n", POOL_USER, POOL_PASS);
        }
        return;
    }
    if (poolClient.available()) {
        String line = poolClient.readStringUntil('\n');
        JsonDocument doc;
        if (deserializeJson(doc, line)) return;
        if (doc["method"] == "mining.notify") {
            stats.job_id = doc["params"][0].as<String>();
            hexToBytes(doc["params"][1].as<String>(), &stats.header[0]);
            hexToBytes(doc["params"][2].as<String>(), &stats.header[4]);
            hexToBytes(doc["params"][3].as<String>(), &stats.header[36]);
            hexToBytes(doc["params"][7].as<String>(), &stats.header[68]);
            hexToBytes(doc["params"][8].as<String>(), &stats.header[72]);
            stats.new_job = true;
        } else if (doc["method"] == "mining.set_difficulty") {
            stats.difficulty = doc["params"][0].as<float>();
        }
    }
}

// --- 4. TASKS ---
void i2cTask(void* pv) {
    while (!i2c_ready) vTaskDelay(100 / portTICK_PERIOD_MS);
    while (1) {
        active_slaves.clear();
        for (uint8_t i = 0x10; i <= 0x77; i++) {
            Wire.beginTransmission(i);
            if (Wire.endTransmission() == 0 && i != 0x20 && i != 0x38) active_slaves.push_back(i);
        }
        
        if (!active_slaves.empty() && stats.new_job) {
            uint8_t n_offset = 0x10;
            for (uint8_t addr : active_slaves) {
                JobI2cRequest req; req.cmd = 0xA1; req.nonce_start_byte = n_offset;
                n_offset += 0x10; req.difficulty = stats.difficulty;
                memcpy(req.buffer, stats.header, 76);
                req.crc = CommandCrc8(&req, sizeof(req));
                Wire.beginTransmission(addr); Wire.write((uint8_t*)&req, sizeof(req)); Wire.endTransmission();
            }
            stats.new_job = false;
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void uiTask(void* pv) {
    tft.init(); tft.setRotation(0);
    canvas.setPsram(true); canvas.setColorDepth(8);
    canvas.createSprite(320, 480);
    
    while (1) {
        canvas.fillSprite(C_BG);
        canvas.setTextDatum(middle_center);

        // --- 1. HEADER (Size 3) ---
        canvas.fillRect(0, 0, 320, 65, C_HEADER_BG);
        canvas.setTextColor(C_BG); canvas.setTextSize(3);
        canvas.drawString("Piner Monitor", 130, 32);
        canvas.fillCircle(295, 32, 10, stats.wifi_up ? C_STATUS_OK : C_STATUS_CRIT);

        // --- 2. HASHRATE HERO (Large Size 6) ---
        uint64_t uptime_sec = (millis() - stats.start_time) / 1000;
        float hr = (uptime_sec > 0) ? (stats.hashes / (float)uptime_sec) / 1000.0 : 0.00;
        canvas.setTextColor(0x7BEF); canvas.setTextSize(2); 
        canvas.drawString("GLOBAL HASHRATE", 160, 90); // Moved label
        
        canvas.setTextColor(hr > 0 ? C_STATUS_OK : C_STATUS_WARN); canvas.setTextSize(6); 
        canvas.setCursor(100, 150); // Lowered digits by 20px
        canvas.printf("%.2f", hr);
        canvas.setTextSize(2); canvas.drawString(" kH/s", 270, 160);

        // --- 3. METRICS PANEL (Centered) ---
        canvas.fillRect(10, 200, 300, 105, C_PANEL);
        canvas.setTextColor(0x7BEF); canvas.setTextSize(2);
        canvas.drawString("UPTIME", 80, 220);
        canvas.drawString("DIFFICULTY", 240, 220);
        canvas.drawString("ACCEPTED", 80, 270);
        canvas.drawString("SUCCESS", 240, 270);

        canvas.setTextColor(C_TEXT);
        int h = uptime_sec / 3600; int m = (uptime_sec % 3600) / 60; int s = uptime_sec % 60;
        canvas.drawString(String(h) + ":" + String(m) + ":" + String(s), 80, 242); // Lowered by ~2px
        canvas.drawString(String(stats.difficulty, 4), 240, 242);
        
        canvas.setTextColor(C_STATUS_OK);
        canvas.drawString(String(stats.accepted), 80, 292);
        float success = (stats.accepted + stats.rejected > 0) ? 
                        (stats.accepted * 100.0 / (stats.accepted + stats.rejected)) : 100.0;
        canvas.drawString(String(success, 1) + "%", 240, 292);

        // --- 4. 3x4 TOPOLOGY GRID ---
        canvas.fillRect(0, 315, 320, 35, C_HEADER_BG);
        canvas.setTextColor(C_BG); canvas.setTextSize(2);
        canvas.drawString("NETWORK TOPOLOGY", 160, 333);

        for (int i = 0; i < 12; i++) {
            int col = i % 3;
            int row = i / 3;
            int x = 20 + (col * 95); 
            int y = 360 + (row * 40);

            bool active = (i < active_slaves.size());
            canvas.fillRoundRect(x, y, 90, 35, 4, active ? C_STATUS_OK : C_PANEL);
            canvas.setTextColor(active ? C_BG : 0x7BEF);
            canvas.setTextSize(2);
            if (active) {
                canvas.drawString("0x" + String(active_slaves[i], HEX), x + 45, y + 17);
            } else {
                canvas.drawString("---", x + 45, y + 17);
            }
        }
        canvas.pushSprite(0, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// --- 5. SETUP & LOOP ---
void setup() {
    Serial.begin(115200); delay(2000);
    stats.start_time = millis();
    Wire.begin(21, 22, 100000);
    
    // LCD Power sequence
    Wire.beginTransmission(0x20); Wire.write(0x03); Wire.write(0xFC); Wire.endTransmission();
    Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x01); Wire.endTransmission();
    delay(20);
    Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x00); Wire.endTransmission();
    delay(20);
    Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x03); Wire.endTransmission();
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);

    i2c_ready = true;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(i2cTask, "I2C", 4096, NULL, 1, NULL, 1);
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        stats.wifi_up = true;
        handleStratum();
    } else {
        stats.wifi_up = false;
    }
    vTaskDelay(10);
}