// --- FILE: displayDriver.cpp ---
#ifdef SCREEN
#include "displayDriver.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <driver/ledc.h>

#ifdef DISPLAY_USE_TFT_ESPI
    TFT_eSPI tft = TFT_eSPI();
    TFT_eSprite canvas = TFT_eSprite(&tft);
#endif

#define BACKLIGHT_CHANNEL 0
#define BACKLIGHT_FREQ 5000
#define BACKLIGHT_RESOLUTION 8

#define GREY_DARK       0x39E7
#define GREY_MEDIUM     0x7BEF
#define GREY_LIGHT      0xBDF7

void setBacklight(uint8_t brightness) {
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

void initDisplay() {
#ifdef DISPLAY_USE_TFT_ESPI
    tft.init();
    tft.setRotation(3); // Landscape
    tft.fillScreen(TFT_BLACK);
    
    if (TFT_BL_PIN != -1) {
        ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ, BACKLIGHT_RESOLUTION);
        ledcAttachPin(TFT_BL_PIN, BACKLIGHT_CHANNEL);
        setBacklight(200); 
    }
    
    canvas.setColorDepth(8);
    canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    canvas.fillSprite(TFT_BLACK);
    canvas.pushSprite(0, 0);
#endif
    Serial.printf("[Display] Initialized - %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
}

void updateUI(int slaveCount, float totalHashrate, float diff,
              uint32_t uptime, String status, float acc) {
    
    canvas.fillSprite(TFT_BLACK);

    // Dynamic UI Scaling Multipliers
    int topBarH = max(25, SCREEN_HEIGHT / 10);
    int botBarH = max(25, SCREEN_HEIGHT / 10);
    int midY = topBarH;
    int midH = SCREEN_HEIGHT - topBarH - botBarH;
    
    int tSizeLg = (SCREEN_WIDTH >= 480) ? 6 : 4;
    int tSizeMd = (SCREEN_WIDTH >= 480) ? 3 : 2;
    int tSizeSm = (SCREEN_WIDTH >= 480) ? 2 : 1;

    // ========================================================================
    // TOP BAR
    // ========================================================================
    canvas.fillRect(0, 0, SCREEN_WIDTH, topBarH, GREY_DARK);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextSize(tSizeMd);
    canvas.setCursor(5, (topBarH / 2) - (8 * tSizeMd / 2));
    canvas.print("ESPMiner Master");
    
    char diffStr[16];
    snprintf(diffStr, sizeof(diffStr), "D:%.2f", diff);
    int diffWidth = canvas.textWidth(diffStr);
    canvas.setCursor(SCREEN_WIDTH - diffWidth - 5, (topBarH / 2) - (8 * tSizeMd / 2));
    canvas.print(diffStr);
    
    // ========================================================================
    // LEFT SECTION - Hashrate Metrics
    // ========================================================================
    int leftW = SCREEN_WIDTH / 2;
    int hashCenterY = midY + (midH / 2) - (8 * tSizeLg / 2);
    
    uint16_t hashColor = (totalHashrate <= 0.01f) ? TFT_RED : (totalHashrate <= 100.0f) ? TFT_YELLOW : TFT_GREEN;
    
    canvas.setTextColor(hashColor, TFT_BLACK);
    canvas.setTextSize(tSizeLg);
    
    char hashStr[16];
    snprintf(hashStr, sizeof(hashStr), "%.0f", totalHashrate);
    int hashWidth = canvas.textWidth(hashStr);
    
    canvas.setCursor((leftW - hashWidth) / 2, hashCenterY - (10 * tSizeLg));
    canvas.print(hashStr);
    
    canvas.setTextSize(tSizeMd);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    int unitWidth = canvas.textWidth("KH/s");
    canvas.setCursor((leftW - unitWidth) / 2, hashCenterY + 5);
    canvas.print("KH/s");
    
    canvas.setTextSize(tSizeSm);
    canvas.setCursor(10, midY + midH - (20 * tSizeSm));
    canvas.printf("Shares: %lu", uptime);
    
    canvas.setTextColor(GREY_LIGHT, TFT_BLACK);
    canvas.setCursor(10, midY + midH - (10 * tSizeSm));
    canvas.printf("Acc: %.1f%%", acc);
    
    // ========================================================================
    // RIGHT SECTION - Slave Rig Visualization
    // ========================================================================
    int rightStartX = leftW + 5;
    int padX = SCREEN_WIDTH * 0.01;
    int padY = SCREEN_HEIGHT * 0.01;
    
    int cols = (SCREEN_WIDTH >= 480) ? 3 : 2;
    int boxW = ((SCREEN_WIDTH - rightStartX) / cols) - (padX * 2);
    int boxH = (SCREEN_HEIGHT >= 320) ? 40 : 20; 
    
    int startY = midY + padY;
    int maxRows = midH / (boxH + padY);
    int maxBoxes = maxRows * cols;
    
    for (int i = 0; i < maxBoxes; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = rightStartX + (col * (boxW + padX));
        int y = startY + (row * (boxH + padY));
        
        if (i < slaveCount) {
            canvas.fillRoundRect(x, y, boxW, boxH, 3, GREY_MEDIUM);
            canvas.drawRoundRect(x, y, boxW, boxH, 3, TFT_WHITE);
            canvas.setTextColor(TFT_WHITE, TFT_BLACK);
            canvas.setTextSize(tSizeSm);
            canvas.setCursor(x + (boxW * 0.15), y + (boxH * 0.25));
            canvas.printf("0x%02X", i + 0x10);
        } else {
            canvas.fillRoundRect(x, y, boxW, boxH, 3, GREY_DARK);
            canvas.drawRoundRect(x, y, boxW, boxH, 3, GREY_MEDIUM);
            canvas.setTextColor(GREY_LIGHT, TFT_BLACK);
            canvas.setTextSize(tSizeSm);
            canvas.setCursor(x + (boxW * 0.25), y + (boxH * 0.25));
            canvas.print("---");
        }
    }
    
    // ========================================================================
    // BOTTOM STATUS BAR
    // ========================================================================
    uint16_t statusColor = (status == "Mining") ? TFT_GREEN : (status == "Connecting...") ? TFT_YELLOW : TFT_RED;
    
    canvas.fillRect(0, SCREEN_HEIGHT - botBarH, SCREEN_WIDTH, botBarH, statusColor);
    canvas.setTextColor(TFT_BLACK, TFT_BLACK);
    canvas.setTextSize(tSizeMd);
    canvas.setCursor(5, SCREEN_HEIGHT - botBarH + (botBarH * 0.2));
    canvas.printf("Status| %s", status.c_str());
    
    canvas.pushSprite(0, 0);
}
#endif