#ifdef LCD
#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <TFT_eSPI.h>
#include "config.h"

struct DisplayStats {
    double hashrate;
    uint32_t total_hashes;
    uint32_t shares_found;
    uint32_t jobs_received;
    uint32_t crc_errors;
    bool mining_active;
    bool core0_active;
    bool core1_active;
};

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();

    bool begin();
    void showBootScreen();
    void updateStats(const DisplayStats& stats);
    void handleButtons();
    void showMiningScreen();
    void showStatsScreen();
    void setBrightness(uint8_t brightness);
    void formatHashrate(double rate, char* buffer, size_t len);

private:
    TFT_eSPI tft = TFT_eSPI();
    uint32_t m_last_update;
    uint8_t m_current_page;
    uint32_t m_last_page_change;
    DisplayStats m_current_stats;

    void drawHeader();
    void drawStatsPage1();
    void drawStatsPage2();
    void drawButtonHints();
    void clearScreen();
};

#endif
#endif
