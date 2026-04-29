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
    tft.setTextColor(TFT_GOLD);
    tft.setTextSize(4);
    tft.setCursor(10, 40);
    tft.println("ESP MINER");
    tft.setTextSize(2);
    tft.setCursor(10, 80);
    tft.printf("Addr: 0x%02X", I2C_BASE_ADDRESS);
}

void DisplayManager::updateStats(const DisplayStats& stats) {
    m_current_stats = stats;
    uint32_t now = millis();
    
    if (now - m_last_update >= DISPLAY_UPDATE_INTERVAL) {
        m_last_update = now;
        clearScreen();
        drawHeader();
        drawStatsPage1();
        drawButtonHints();
    }
}

void DisplayManager::drawStatsPage1() {
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(5, 40);
    tft.print("HR: ");
    
    char buffer[32];
    formatHashrate(m_current_stats.hashrate, buffer, sizeof(buffer));
    tft.setTextColor(TFT_GREEN);
    tft.println(buffer);

    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(5, 70);
    tft.print("Total: ");
    tft.setTextColor(TFT_WHITE);
    tft.println(m_current_stats.total_hashes);
}

void DisplayManager::formatHashrate(double rate, char* buffer, size_t len) {
    if (rate >= 1000) snprintf(buffer, len, "%.2f kH/s", rate / 1000.0);
    else snprintf(buffer, len, "%.2f H/s", rate);
}

void DisplayManager::handleButtons() {
#ifdef BUTTON1_GPIO
    static byte b1Last = digitalRead(BUTTON1_GPIO);
    byte b1Now = digitalRead(BUTTON1_GPIO);
    if (b1Now != b1Last) {
        if (b1Now == LOW) { // Pressed
            m_current_page = (m_current_page + 1) % 2;
            clearScreen();
        }
        b1Last = b1Now;
        delay(BOUNCING_MS);
    }
#endif
}

void DisplayManager::drawHeader() {
    tft.fillRect(0, 0, 240, 25, TFT_NAVY);
    tft.setCursor(60, 5);
    tft.setTextColor(TFT_WHITE);
    tft.print("MINER STATUS");
}

void DisplayManager::clearScreen() { tft.fillRect(0, 25, 240, 135, TFT_BLACK); }
void DisplayManager::drawStatsPage2() { /* Page 2 logic */ }
void DisplayManager::drawButtonHints() { /* Button hint logic */ }
void DisplayManager::showMiningScreen() { clearScreen(); drawHeader(); }
void DisplayManager::showStatsScreen() { clearScreen(); drawHeader(); }
void DisplayManager::setBrightness(uint8_t brightness) { analogWrite(TFT_BL_PIN, brightness); }
#endif