#ifdef SCREEN
#include "displayDriver.h"
#include "boards.h"
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
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    if (TFT_BL_PIN != -1) {
        ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ, BACKLIGHT_RESOLUTION);
        ledcAttachPin(TFT_BL_PIN, BACKLIGHT_CHANNEL);
        setBacklight(200); 
    }
    
    canvas.setColorDepth(16);
    if (canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
        canvas.fillSprite(TFT_BLACK);
        canvas.pushSprite(0, 0);
    }
#endif
    Serial.printf("[Display] Initialized - %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
}

void updateUI(int slaveCount, float totalHashrate, float diff,
              uint32_t uptime, String status, float acc) {
    
#ifdef DISPLAY_USE_TFT_ESPI
    canvas.fillSprite(TFT_BLACK);

    int topBarH = max(25, SCREEN_HEIGHT / 10);
    int botBarH = max(25, SCREEN_HEIGHT / 10);
    
    int tSizeLg = (SCREEN_WIDTH >= 480) ? 6 : 4;
    int tSizeMd = (SCREEN_WIDTH >= 480) ? 3 : 2;
    int tSizeSm = (SCREEN_WIDTH >= 480) ? 2 : 1;

    // TOP BAR
    canvas.fillRect(0, 0, SCREEN_WIDTH, topBarH, GREY_DARK);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextSize(tSizeMd);
    canvas.setCursor(5, 6);
    canvas.print("ESPMiner");
    
    char diffStr[16];
    snprintf(diffStr, sizeof(diffStr), "D:%.3f", diff);
    canvas.setCursor(SCREEN_WIDTH - 80, 6);
    canvas.print(diffStr);
    
    // HASHRATE
    uint16_t hashColor = (totalHashrate <= 0.01f) ? TFT_RED : TFT_GREEN;
    canvas.setTextColor(hashColor, TFT_BLACK);
    canvas.setTextSize(tSizeLg);
    canvas.setCursor(10, 40);
    canvas.printf("%.1f", totalHashrate);
    
    canvas.setTextSize(tSizeMd);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setCursor(10, 70);
    canvas.print("KH/s");
    
    // SHARES
    canvas.setTextSize(tSizeSm);
    canvas.setCursor(10, 95);
    canvas.printf("Shares: %lu", uptime);
    
    // SLAVE BOXES
    int rightStartX = SCREEN_WIDTH / 2 + 5;
    int boxW = 45, boxH = 18;
    int padX = 3, padY = 3;
    int startY = 40;
    
    for (int i = 0; i < 8; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = rightStartX + (col * (boxW + padX));
        int y = startY + (row * (boxH + padY));
        
        if (i < slaveCount) {
            canvas.fillRoundRect(x, y, boxW, boxH, 2, GREY_MEDIUM);
            canvas.setTextColor(TFT_WHITE, TFT_BLACK);
            canvas.setTextSize(tSizeSm);
            canvas.setCursor(x + 8, y + 5);
            canvas.printf("0x%02X", i + 0x10);
        } else {
            canvas.fillRoundRect(x, y, boxW, boxH, 2, GREY_DARK);
            canvas.setTextColor(GREY_LIGHT, TFT_BLACK);
            canvas.setCursor(x + 15, y + 5);
            canvas.print("---");
        }
    }
    
    // BOTTOM BAR
    uint16_t statusColor = (status == "Mining") ? TFT_GREEN : TFT_RED;
    canvas.fillRect(0, SCREEN_HEIGHT - botBarH, SCREEN_WIDTH, botBarH, statusColor);
    canvas.setTextColor(TFT_BLACK, TFT_BLACK);
    canvas.setTextSize(tSizeSm);
    canvas.setCursor(5, SCREEN_HEIGHT - botBarH + 4);
    canvas.printf("Status: %s", status.c_str());
    
    canvas.pushSprite(0, 0);
#endif
}
#endif
