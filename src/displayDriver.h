#ifdef SCREEN

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <vector>

// TFT_eSPI handles pin definitions in User Setup
// No need to define TFT pins here when using USER_SETUP_ID=25

// TTGO T-Display Backlight Pin
#define TFT_BL_PIN 4

struct SlaveData;

extern TFT_eSPI tft;
extern TFT_eSprite canvas;
extern std::vector<SlaveData> g_slave_list;

void initDisplay();
void setBacklight(uint8_t brightness);
void updateUI(int slaveCount, float totalHashrate, float diff, 
              uint32_t uptime, String status, float acc);

#endif
#endif
