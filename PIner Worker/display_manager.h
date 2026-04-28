#ifndef DISPLAY_MANAGER_H
#ifdef LCD
#define DISPLAY_MANAGER_H
#include <TFT_eSPI.h>
#include <atomic>
#include <string>
#include "config.h"

struct MiningStats {
    uint32_t hashes;
    uint32_t shares_found;
    double hashrate;
    uint32_t jobs_received;
    uint32_t crc_errors;
    bool mining_active;
    uint8_t core0_active;
    uint8_t core1_active;
};

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();
    
    bool begin();
    void updateStats(const MiningStats& stats);
    void showBootScreen();
    void showMiningScreen();
    void showStatsScreen();
    void clearScreen();
    
    void setBrightness(uint8_t brightness);
    void handleButtons();
    
private:
    TFT_eSPI tft;
    MiningStats m_current_stats;
    uint32_t m_last_update;
    uint32_t m_current_page;
    uint32_t m_last_page_change;
    
    void drawHeader();
    void drawStatsPage1();
    void drawStatsPage2();
    void drawButtonHints();
    void formatHashrate(double rate, char* buffer, size_t len);
};
#endif
#endif // DISPLAY_MANAGER_H

