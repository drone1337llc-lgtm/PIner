#ifdef SCREEN
#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <vector>

struct SlaveData;

extern TFT_eSPI tft;
extern TFT_eSprite canvas;

void initDisplay();
void setBacklight(uint8_t brightness);
void updateUI(int slaveCount, float totalHashrate, float diff, 
              uint32_t uptime, String status, float acc);

#endif
#endif
