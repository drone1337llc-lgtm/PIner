#ifdef SCREEN
#include "displayDriver.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <driver/ledc.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);

// PWM channel for backlight
#define BACKLIGHT_CHANNEL 0
#define BACKLIGHT_FREQ 5000
#define BACKLIGHT_RESOLUTION 8

// Grey color definitions
#define GREY_DARK       0x39E7    // Dark grey
#define GREY_MEDIUM     0x7BEF    // Medium grey
#define GREY_LIGHT      0xBDF7    // Light grey

void setBacklight(uint8_t brightness)
{
    // Brightness: 0 (off) to 255 (max)
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

void initDisplay()
{
    tft.init();
    
    // Use rotation 1 for proper landscape orientation on TTGO T-Display
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    
    // Setup PWM for backlight control
    ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ, BACKLIGHT_RESOLUTION);
    ledcAttachPin(TFT_BL_PIN, BACKLIGHT_CHANNEL);
    setBacklight(200);  // Set brightness (0-255)
    
    // Create sprite for double-buffering - MUST MATCH SCREEN_WIDTH x SCREEN_HEIGHT
    canvas.setColorDepth(8);
    canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    canvas.fillSprite(TFT_BLACK);
    canvas.pushSprite(0, 0);
    
    Serial.printf("[Display] Initialized - %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
}

void updateUI(int slaveCount, float totalHashrate, float diff,
              uint32_t uptime, String status, float acc)
{
    canvas.fillSprite(TFT_BLACK);
    
    // ========================================================================
    // TOP BAR - Increased to 25px for bigger text
    // ========================================================================
    canvas.fillRect(0, 0, SCREEN_WIDTH, 25, GREY_DARK);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextSize(2);  // Bigger text
    canvas.setCursor(5, 6);
    canvas.print("ESPMiner");
    
    char diffStr[16];
    snprintf(diffStr, sizeof(diffStr), "D:%.2f", diff);
    int diffWidth = canvas.textWidth(diffStr);
    canvas.setCursor(SCREEN_WIDTH - diffWidth - 5, 6);
    canvas.print(diffStr);
    
    // ========================================================================
    // LEFT SECTION - Hashrate, Shares, Acc (0 to SCREEN_WIDTH/2)
    // ========================================================================
    int leftSectionWidth = SCREEN_WIDTH / 2;
    int centerY = 65;  // Middle of usable area (25 to 110)
    
    // HASHRATE
    uint16_t hashColor = (totalHashrate <= 0.01f) ? TFT_RED : (totalHashrate <= 100.0f) ? TFT_YELLOW
                                                                                        : TFT_GREEN;
    
    canvas.setTextColor(hashColor, TFT_BLACK);
    canvas.setTextSize(4);  // Bigger hashrate text
    
    char hashStr[16];
    snprintf(hashStr, sizeof(hashStr), "%.0f", totalHashrate);
    int hashWidth = canvas.textWidth(hashStr);
    
    // Centered in left section
    canvas.setCursor((leftSectionWidth - hashWidth) / 2, centerY - 30);
    canvas.print(hashStr);
    
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    int unitWidth = canvas.textWidth("KH/s");
    canvas.setCursor((leftSectionWidth - unitWidth) / 2, centerY + 0);
    canvas.print("KH/s");
    
    // SHARES & ACC - Below hashrate in left section
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setCursor(10, centerY + 20);
    canvas.printf("Shares: %lu", uptime);
    
    canvas.setTextColor(GREY_LIGHT, TFT_BLACK);
    canvas.setCursor(10, centerY + 30);
    canvas.printf("Acc: %.1f%%", acc);
    
    // ========================================================================
    // RIGHT SECTION - Slave Boxes (SCREEN_WIDTH/2 to SCREEN_WIDTH)
    // ========================================================================
    int rightStartX = leftSectionWidth + 5;
    int boxW = 52, boxH = 20;  // Slightly bigger boxes
    int padX = 4, padY = 4;
    int startY = 30;  // Start below top bar
    
    // Calculate max rows that fit
    int availableHeight = SCREEN_HEIGHT - 25 - 25 - startY;  // Minus top/bottom bars and start
    int maxRows = availableHeight / (boxH + padY);
    
    for (int i = 0; i < 7; i++)
    {
        int col = i % 2;
        int row = i / 2;
        int x = rightStartX + (col * (boxW + padX));
        int y = startY + (row * (boxH + padY));
        
        // Check if box would be outside screen bounds (above bottom bar)
        if (y + boxH > SCREEN_HEIGHT - 25) {
            continue;  // Skip boxes that would overlap status bar
        }
        
        if (i < slaveCount)
        {
            canvas.fillRoundRect(x, y, boxW, boxH, 3, GREY_MEDIUM);
            canvas.drawRoundRect(x, y, boxW, boxH, 3, TFT_WHITE);
            canvas.setTextColor(TFT_WHITE, TFT_BLACK);
            canvas.setTextSize(1);
            canvas.setCursor(x + 8, y + 6);
            canvas.printf("0x%02X", i + 0x10);
        }
        else
        {
            canvas.fillRoundRect(x, y, boxW, boxH, 3, GREY_DARK);
            canvas.drawRoundRect(x, y, boxW, boxH, 3, GREY_MEDIUM);
            canvas.setTextColor(GREY_LIGHT, TFT_BLACK);
            canvas.setTextSize(1);
            canvas.setCursor(x + 18, y + 6);
            canvas.print("---");
        }
    }
    
    // ========================================================================
    // BOTTOM STATUS BAR - Increased to 25px for bigger text
    // ========================================================================
    uint16_t statusColor = (status == "Mining") ? TFT_GREEN : (status == "Connecting...") ? TFT_YELLOW
                                                                                          : TFT_RED;
    
    canvas.fillRect(0, SCREEN_HEIGHT - 25, SCREEN_WIDTH, 25, statusColor);
    canvas.setTextColor(TFT_BLACK, TFT_BLACK);
    canvas.setTextSize(2);  // Bigger text
    canvas.setCursor(5, SCREEN_HEIGHT - 19);
    canvas.printf("Status|", status.c_str());
    
    canvas.pushSprite(0, 0);
}
#endif
