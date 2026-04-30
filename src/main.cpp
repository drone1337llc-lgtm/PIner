#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <vector>
#include "i2c_protocol.h"
#include "config.h"
#include "displayDriver.h" // Added UI Driver

float globalHashrate = 0.0f;
float globalDiff = 0.0f;
uint32_t lastUptime = 0;

// Global UI Objects
LGFX_Master tft;
LGFX_Sprite canvas(&tft);

uint8_t crc8_compute(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= bytes[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }
    return crc;
}

#define CLUSTER_SDA 17
#define CLUSTER_SCL 16
TwoWire ClusterBus = TwoWire(1);

struct SlaveData
{
    uint8_t address;
    uint32_t last_seen;
    uint32_t shares;
    float last_hashrate_raw; // Add this line!
};
std::vector<SlaveData> slave_list;

struct
{
    float difficulty = 0;
    uint8_t header[76];
    bool new_job = false;
    uint32_t total_shares = 0;
    String pool_status = "Connecting...";
} stats;

void uiTask(void *pvParameters)
{
    while (1)
    {
        float totalH = 0;

        // Sum up shares or dummy hashrate for now since SlaveData
        // doesn't have a 'last_hashrate_raw' field yet.
        for (auto &slave : slave_list)
        {
            // Placeholder: If you add hashrate to SlaveData later, use that.
            // For now, we'll just show the slave count impact.
            totalH += 12.5f; // Example: assume each slave is doing 12.5 KH/s
        }

        globalHashrate = totalH;
        uint32_t uptimeSecs = millis() / 1000;

        // Use the global variables we defined at the top of main.cpp
        updateUI(
            slave_list.size(),
            globalHashrate,
            stats.difficulty, // Use 'stats.difficulty' from your struct
            uptimeSecs,
            stats.pool_status // Use 'stats.pool_status' for the footer
        );

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void i2cTask(void *pv)
{
    while (1)
    {
        // 1. SCAN
        for (uint8_t i = I2C_SCAN_START; i <= I2C_SCAN_END; i++)
        {
            ClusterBus.beginTransmission(i);
            if (ClusterBus.endTransmission() == 0)
            {
                bool exists = false;
                for (auto &s : slave_list)
                    if (s.address == i)
                        exists = true;
                if (!exists)
                    slave_list.push_back({i, millis(), 0});
            }
        }

        // 2. DISPATCH & 3. POLL (Integrated Logic)
        if (!slave_list.empty())
        {
            for (auto &slave : slave_list)
            {
                if (stats.new_job)
                {
                    JobI2cRequest req;
                    req.cmd = I2C_CMD_FEED;
                    req.difficulty = stats.difficulty;
                    memcpy(req.buffer, stats.header, 76);
                    req.crc = crc8_compute(&req.id, sizeof(req) - 2);

                    ClusterBus.beginTransmission(slave.address);
                    ClusterBus.write((uint8_t *)&req, sizeof(req));
                    ClusterBus.endTransmission();
                }

                // Poll for results
                ClusterBus.requestFrom(slave.address, (uint8_t)5);
                if (ClusterBus.available() >= 5)
                {
                    uint8_t status = ClusterBus.read();
                    if (status == 0x02)
                    {
                        uint32_t nonce;
                        ClusterBus.readBytes((uint8_t *)&nonce, 4);
                        slave.shares++;
                        stats.total_shares++;
                        Serial.printf("[!] Share from 0x%02X: %08X\n", slave.address, nonce);
                    }
                }
            }
            stats.new_job = false;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// --- UI IMPLEMENTATION ---
void initDisplay()
{
    tft.init();
    tft.setRotation(1); // Adjust 1 or 3 for your physical mounting

    // Use 8-bit color to save RAM and prevent flickering
    canvas.setColorDepth(8);
    canvas.createSprite(tft.width(), tft.height());

    canvas.fillSprite(TFT_BLACK);
    canvas.pushSprite(0, 0);
}

void updateUI(int slaveCount, float totalHashrate, float diff, uint32_t uptime, String status)
{
    canvas.fillSprite(TFT_BLACK);

    // --- 1. TOP BANNER: Explicit Dark Grey ---
    // Using hex to force grey: 0x4208 is a solid dark grey
    canvas.fillRect(0, 0, 320, 25, 0x4208); 
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    canvas.setCursor(10, 5); 
    canvas.print("ESPiner Master");
    
    canvas.setCursor(236, 5);
    canvas.printf("D:%.1f", diff);

    // --- 2. PERFORMANCE: Centered in 80px stats area ---
    int statsWidth = 85; 
    
    // Hashrate Logic: Red (0) -> Yellow (1-100) -> Green (>100)
    uint16_t hashColor;
    if (totalHashrate <= 0.01f) hashColor = TFT_RED;
    else if (totalHashrate <= 100.0f) hashColor = TFT_YELLOW;
    else hashColor = TFT_GREEN;

    canvas.setTextColor(hashColor);
    canvas.setTextSize(4); 
    // Manual center for ~4-5 characters in 80px:
    canvas.setCursor(80, 50); 
    canvas.printf("%.2f", totalHashrate);
    
    canvas.setTextSize(2);
    canvas.setCursor(80, 80);
    canvas.print("KH/s");

    // Shares: Moved down slightly, space removed
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    canvas.setCursor(10, 105);
    canvas.printf("S:%lu", stats.total_shares); // Removed space

    // ACC: Moved lower, text closer
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.setCursor(10, 125);
    canvas.print("ACC:100%"); // Removed space

    // --- 3. THE GRID: Right Side (Shifted up to clear footer) ---
    int boxW = 68;    
    int boxH = 24;    // Slightly shorter to prevent bottom cutoff
    int startX = 176; 
    int startY = 34;  // Shifted up 2px
    int padX = 4;
    int padY = 4;     // Tighter padding

    for (int i = 0; i < 8; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = startX + (col * (boxW + padX));
        int y = startY + (row * (boxH + padY));

        if (i < slave_list.size()) {
            // OCCUPIED: Grey fill, White outline, White address
            canvas.fillRoundRect(x, y, boxW, boxH, 3, 0x7BEF); // Solid Grey
            canvas.drawRoundRect(x, y, boxW, boxH, 3, TFT_WHITE);
            canvas.setTextColor(TFT_WHITE);
            canvas.setTextSize(1);
            // Centering address (approx 30px wide)
            canvas.setCursor(x + (boxW - 30) / 2, y + 8);
            canvas.printf("0x%02X", slave_list[i].address);
        } else {
            // EMPTY: Grey fill, Red outline, White dashes
            canvas.fillRoundRect(x, y, boxW, boxH, 3, 0x4208); // Darker Grey
            canvas.drawRoundRect(x, y, boxW, boxH, 3, TFT_RED);
            canvas.setTextColor(TFT_WHITE); // Dashes are now white
            canvas.setTextSize(1);
            // Centering dashes (approx 18px wide)
            canvas.setCursor(x + (boxW - 18) / 2, y + 8);
            canvas.print("---");
        }
    }

    // --- 4. FOOTER: Red with White Centered Text ---
    // Lowered height to 25px and shifted Y to ensure boxes clear it
    canvas.fillRect(0, 145, 320, 25, TFT_RED); 
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    
    String footerMsg = "NODE:" + status;
    int footerX = (320 - (footerMsg.length() * 12)) / 2;
    canvas.setCursor(max(5, footerX), 150);
    canvas.print(footerMsg);

    canvas.pushSprite(0, 0);
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    // --- MUST INITIALIZE DISPLAY FIRST ---
    initDisplay();

    ClusterBus.begin(CLUSTER_SDA, CLUSTER_SCL, 100000);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Increase UI stack slightly to avoid crashes with String manipulation
    xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(i2cTask, "I2C", 4096, NULL, 2, NULL, 1);
}

void loop()
{
    if (WiFi.status() == WL_CONNECTED)
        stats.pool_status = "WiFi OK";
    vTaskDelay(1000);
}