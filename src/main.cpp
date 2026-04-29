#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <algorithm> // For sorting
#include "i2c_protocol.h"
#include "config.h"

// --- Professional UI Color Palette ---
#define C_BG          0x0000 
#define C_HEADER_BG   0xBDD7 
#define C_PANEL       0x2104 
#define C_TEXT        0xFFFF 
#define C_STATUS_OK    0x03E0 
#define C_STATUS_WARN  0xFDA0 
#define C_STATUS_CRIT  0xF800 

// --- GLOBALS ---
const uint8_t s_crc8_table[256] = { /* ... (Same as your previous code) ... */ };
uint8_t CommandCrc8(const void* data, size_t len) { /* ... (Same as your previous code) ... */ }

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
bool i2c_ready = false;
int current_page = 0; // 0 = Dashboard, 1 = Leaderboard

struct SlaveData {
    uint8_t address;
    float hashrate;
    uint32_t accepted;
    uint32_t rejected;
};
std::vector<SlaveData> slave_list;

struct {
    uint64_t hashes = 0;
    uint32_t accepted = 0;
    uint32_t rejected = 0;
    uint64_t start_time = 0;
    float difficulty = 0;
    bool new_job = false;
    bool wifi_up = false;
} stats;

// --- Helper: Get Success Color ---
uint16_t getSuccessColor(float rate) {
    if (rate >= 90.0) return C_STATUS_OK;
    if (rate >= 70.0) return C_STATUS_WARN;
    return C_STATUS_CRIT;
}

// --- TASKS & LOGIC ---
void i2cTask(void* pv) {
    while (!i2c_ready) vTaskDelay(100);
    while (1) {
        // Discovery & Stats Gathering (Live testing placeholders for hashrate)
        slave_list.clear();
        for (uint8_t i = 0x10; i <= 0x77; i++) {
            Wire.beginTransmission(i);
            if (Wire.endTransmission() == 0 && i != 0x20 && i != 0x38) {
                // In live testing, you'll replace these 0.0s with actual I2C read calls
                slave_list.push_back({i, 0.0, 0, 0}); 
            }
        }
        
        // Sort slaves by hashrate (Fastest on top)
        std::sort(slave_list.begin(), slave_list.end(), [](const SlaveData& a, const SlaveData& b) {
            return a.hashrate > b.hashrate;
        });

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void uiTask(void* pv) {
    tft.init(); tft.setRotation(0);
    canvas.setPsram(true); canvas.setColorDepth(8);
    canvas.createSprite(320, 480);
    
    uint16_t tx, ty;

    while (1) {
        if (tft.getTouch(&tx, &ty)) {
            current_page = (current_page == 0) ? 1 : 0;
            vTaskDelay(300 / portTICK_PERIOD_MS); // Debounce
        }

        canvas.fillSprite(C_BG);
        canvas.setTextDatum(middle_center);

        // --- COMMON HEADER ---
        canvas.fillRect(0, 0, 320, 65, C_HEADER_BG);
        canvas.setTextColor(C_BG); canvas.setTextSize(3);
        canvas.drawString(current_page == 0 ? "Piner Monitor" : "Worker Stats", 140, 32);
        canvas.fillCircle(295, 32, 10, stats.wifi_up ? C_STATUS_OK : C_STATUS_CRIT);

        if (current_page == 0) {
            // --- PAGE 0: DASHBOARD ---
            uint64_t uptime_sec = (millis() - stats.start_time) / 1000;
            float hr = (uptime_sec > 0) ? (stats.hashes / (float)uptime_sec) / 1000.0 : 0.00;
            
            canvas.setTextColor(0x7BEF); canvas.setTextSize(2);
            canvas.drawString("GLOBAL HASHRATE", 160, 90);
            canvas.setTextColor(hr > 0 ? C_STATUS_OK : C_STATUS_WARN); canvas.setTextSize(6);
            canvas.setCursor(100, 150); canvas.printf("%.2f", hr);
            canvas.setTextSize(2); canvas.drawString(" kH/s", 270, 160);

            // Metrics Panel
            canvas.fillRect(10, 200, 300, 105, C_PANEL);
            canvas.setTextColor(0x7BEF); canvas.setTextSize(2);
            canvas.drawString("UPTIME", 80, 220); canvas.drawString("DIFFICULTY", 240, 220);
            canvas.drawString("ACCEPTED", 80, 270); canvas.drawString("SUCCESS", 240, 270);

            canvas.setTextColor(C_TEXT);
            int h = uptime_sec / 3600; int m = (uptime_sec % 3600) / 60; int s = uptime_sec % 60;
            canvas.drawString(String(h) + ":" + String(m) + ":" + String(s), 80, 242);
            canvas.drawString(String(stats.difficulty, 4), 240, 242);
            
            float success = (stats.accepted + stats.rejected > 0) ? 
                            (stats.accepted * 100.0 / (stats.accepted + stats.rejected)) : 100.0;
            canvas.setTextColor(C_STATUS_OK); canvas.drawString(String(stats.accepted), 80, 292);
            canvas.setTextColor(getSuccessColor(success)); canvas.drawString(String(success, 1) + "%", 240, 292);

            // Topology Map (3x4)
            canvas.fillRect(0, 315, 320, 35, C_HEADER_BG);
            canvas.setTextColor(C_BG); canvas.drawString("NETWORK TOPOLOGY", 160, 333);
            for (int i = 0; i < 12; i++) {
                int x = 20 + (i % 3) * 95; int y = 360 + (i / 3) * 40;
                bool active = (i < slave_list.size());
                canvas.fillRoundRect(x, y, 90, 35, 4, active ? C_STATUS_OK : C_PANEL);
                canvas.setTextColor(active ? C_BG : 0x7BEF);
                canvas.drawString(active ? "0x" + String(slave_list[i].address, HEX) : "---", x + 45, y + 17);
            }
        } else {
            // --- PAGE 1: WORKER LEADERBOARD ---
            canvas.setTextColor(0x7BEF); canvas.setTextSize(2);
            canvas.drawString("ADDR", 45, 85);
            canvas.drawString("HASHRATE", 145, 85);
            canvas.drawString("SUCCESS", 260, 85);
            canvas.drawFastHLine(10, 100, 300, 0x7BEF);

            for (int i = 0; i < slave_list.size() && i < 8; i++) {
                int y = 125 + (i * 45);
                canvas.fillRect(10, y - 20, 300, 40, C_PANEL);
                
                canvas.setTextColor(C_STATUS_OK);
                canvas.setCursor(20, y - 7); canvas.printf("0x%02X", slave_list[i].address);
                
                canvas.setTextColor(C_TEXT);
                canvas.setCursor(110, y - 7); canvas.printf("%.2f kH/s", slave_list[i].hashrate);
                
                float s_rate = (slave_list[i].accepted + slave_list[i].rejected > 0) ? 
                               (slave_list[i].accepted * 100.0 / (slave_list[i].accepted + slave_list[i].rejected)) : 100.0;
                canvas.setTextColor(getSuccessColor(s_rate));
                canvas.setCursor(240, y - 7); canvas.printf("%.0f%%", s_rate);
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