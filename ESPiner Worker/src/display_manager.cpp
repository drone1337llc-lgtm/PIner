#ifdef LCD
#include "display_manager.h"

DisplayManager::DisplayManager() 
    : m_last_update(0), m_current_page(0), m_last_page_change(0) {
    memset(&m_current_stats, 0, sizeof(m_current_stats));
}

DisplayManager::~DisplayManager() {}

bool DisplayManager::begin() {
    tft.init();
    tft.setRotation(1);
#ifdef TFT_BL_PIN
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
#endif
    tft.fillScreen(TFT_BLACK);
    return true;
}

void DisplayManager::showBootScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(20, 40);
    tft.println("ESP MINER");
    tft.setTextSize(3);
    tft.setCursor(120, 100);
    tft.printf("Ready!");
    delay(2000);
}

void DisplayManager::updateStats(const DisplayStats& stats) {
    m_current_stats = stats;
    uint32_t now = millis();
    
    if (now - m_last_update >= DISPLAY_UPDATE_INTERVAL) {
        m_last_update = now;
        
        // Only clear stats area, not header
        tft.fillRect(0, 30, 240, 130, TFT_BLACK);
        
        drawStatsPage1();
    }
}

void DisplayManager::drawStatsPage1() {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(80, 40);
    tft.print("Hashrate:");
    
    char buffer[32];
    formatHashrate(m_current_stats.hashrate, buffer, sizeof(buffer));
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(80, 65);
    tft.print(buffer);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(80, 95);
    tft.print("Total:");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(100, 115);
    tft.printf("%lu", m_current_stats.total_hashes);
    
    // Mining status
    tft.setTextColor(m_current_stats.mining_active ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(80, 145);
    tft.print(m_current_stats.mining_active ? "MINING" : "IDLE");
}

void DisplayManager::formatHashrate(double rate, char* buffer, size_t len) {
    if (rate >= 1000000) snprintf(buffer, len, "%.2f MH/s", rate / 1000000.0);
    else if (rate >= 1000) snprintf(buffer, len, "%.2f KH/s", rate / 1000.0);
    else snprintf(buffer, len, "%.2f H/s", rate);
}

void DisplayManager::handleButtons() {
#ifdef BUTTON1_GPIO
    static byte b1Last = HIGH;
    byte b1Now = digitalRead(BUTTON1_GPIO);
    
    if (b1Now == LOW && b1Last == HIGH) {
        m_current_page = (m_current_page + 1) % 2;
        m_last_page_change = millis();
        tft.fillRect(0, 30, 240, 130, TFT_BLACK);
    }
    b1Last = b1Now;
#endif
}

void DisplayManager::drawHeader() {
    tft.fillRect(0, 0, 240, 25, TFT_NAVY);
    tft.setCursor(50, 5);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(2);
    tft.print("MINER STATUS");
}

void DisplayManager::clearScreen() { 
    tft.fillRect(0, 25, 240, 135, TFT_BLACK); 
}

void DisplayManager::drawStatsPage2() {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(5, 40);
    tft.print("Jobs:");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 65);
    tft.printf("%lu", m_current_stats.jobs_received);
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(5, 95);
    tft.print("CRC Err:");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(5, 115);
    tft.printf("%lu", m_current_stats.crc_errors);
}

void DisplayManager::drawButtonHints() {
    tft.fillRect(0, 160, 240, 20, TFT_NAVY);
    tft.setCursor(5, 163);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(1);
    tft.print("Press BTN for more info");
}

void DisplayManager::showMiningScreen() { 
    drawHeader(); 
    drawStatsPage1();
}

void DisplayManager::showStatsScreen() { 
    drawHeader(); 
    drawStatsPage2();
}

void DisplayManager::setBrightness(uint8_t brightness) { 
#ifdef TFT_BL_PIN
    analogWrite(TFT_BL_PIN, brightness);
#endif
}
#endif
