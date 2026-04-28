#ifdef LCD
#include "display_manager.h"
#include "config.h"
#include <Arduino.h>

DisplayManager::DisplayManager()
    : m_last_update(0)
    , m_current_page(0)
    , m_last_page_change(0)
{
    memset(&m_current_stats, 0, sizeof(m_current_stats));
}

DisplayManager::~DisplayManager() {
}

bool DisplayManager::begin() {
    tft.init();
    tft.setRotation(0);  // Portrait orientation
    digitalWrite(TFT_BL, HIGH);  // Turn on backlight
    
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_GOLD);
    
    return true;
}

void DisplayManager::showBootScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_GOLD);
    
    tft.setTextColor(TFT_GOLD);
    tft.setTextSize(2);
    tft.setCursor((TFT_WIDTH - 120) / 2, 40);
    tft.println("TTGO Miner");
    
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(1);
    tft.setCursor((TFT_WIDTH - 80) / 2, 80);
    tft.println("I2C Mining");
    
    tft.setCursor((TFT_WIDTH - 60) / 2, 100);
    tft.println("Dual Core");
    
    tft.setTextColor(TFT_BLUE);
    tft.setCursor((TFT_WIDTH - 100) / 2, 140);
    tft.println("Initializing...");
    
    delay(2000);
}

void DisplayManager::updateStats(const MiningStats& stats) {
    m_current_stats = stats;
    
    uint32_t now = millis();
    if (now - m_last_update >= DISPLAY_UPDATE_INTERVAL) {
        m_last_update = now;
        
        // Auto-rotate pages
        if (now - m_last_page_change >= STATS_PAGE_INTERVAL) {
            m_current_page = (m_current_page + 1) % 2;
            m_last_page_change = now;
            clearScreen();
        }
        
        drawHeader();
        
        if (m_current_page == 0) {
            drawStatsPage1();
        } else {
            drawStatsPage2();
        }
        
        drawButtonHints();
    }
}

void DisplayManager::drawHeader() {
    tft.fillRect(0, 0, TFT_WIDTH, 20, TFT_NAVY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 4);
    tft.println("ESP32 I2C Miner");
    
    // Status indicator
    tft.fillCircle(TFT_WIDTH - 10, 10, 5, m_current_stats.mining_active ? TFT_GREEN : TFT_RED);
}

void DisplayManager::drawStatsPage1() {
    tft.setTextColor(TFT_YELLOW);
    tft.setTextSize(2);
    
    // Hashrate
    tft.setCursor(5, 35);
    tft.println("Hashrate:");
    
    char buffer[32];
    formatHashrate(m_current_stats.hashrate, buffer, sizeof(buffer));
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(5, 55);
    tft.println(buffer);
    
    // Total Hashes
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(5, 85);
    tft.println("Hashes:");
    
    tft.setTextColor(TFT_WHITE);
    snprintf(buffer, sizeof(buffer), "%lu", m_current_stats.hashes);
    tft.setCursor(5, 105);
    tft.println(buffer);
    
    // Shares Found
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(5, 135);
    tft.println("Shares:");
    
    tft.setTextColor(TFT_GREEN);
    snprintf(buffer, sizeof(buffer), "%lu", m_current_stats.shares_found);
    tft.setCursor(5, 155);
    tft.println(buffer);
}

void DisplayManager::drawStatsPage2() {
    tft.setTextColor(TFT_YELLOW);
    tft.setTextSize(2);
    
    // Jobs Received
    tft.setCursor(5, 35);
    tft.println("Jobs:");
    
    tft.setTextColor(TFT_WHITE);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu", m_current_stats.jobs_received);
    tft.setCursor(5, 55);
    tft.println(buffer);
    
    // CRC Errors
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(5, 85);
    tft.println("CRC Err:");
    
    tft.setTextColor(m_current_stats.crc_errors > 0 ? TFT_RED : TFT_GREEN);
    snprintf(buffer, sizeof(buffer), "%lu", m_current_stats.crc_errors);
    tft.setCursor(5, 105);
    tft.println(buffer);
    
    // Core Distribution
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(5, 135);
    tft.println("Cores:");
    
    tft.setTextColor(TFT_CYAN);
    snprintf(buffer, sizeof(buffer), "0:%s 1:%s", 
             m_current_stats.core0_active ? "ON" : "OFF", 
             m_current_stats.core1_active ? "ON" : "OFF");
    tft.setCursor(5, 155);
    tft.println(buffer);
}

void DisplayManager::drawButtonHints() {
    tft.fillRect(0, TFT_HEIGHT - 20, TFT_WIDTH, 20, TFT_NAVY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, TFT_HEIGHT - 16);
    tft.println("Left:Page Right:Reset");
}

void DisplayManager::showMiningScreen() {
    clearScreen();
    drawHeader();
    drawStatsPage1();
    drawButtonHints();
}

void DisplayManager::showStatsScreen() {
    clearScreen();
    drawHeader();
    drawStatsPage2();
    drawButtonHints();
}

void DisplayManager::clearScreen() {
    tft.fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 40, TFT_BLACK);
}

void DisplayManager::setBrightness(uint8_t brightness) {
    // PWM brightness control could be added here
    digitalWrite(TFT_BL, brightness > 0 ? HIGH : LOW);
}

void DisplayManager::handleButtons() {
    static byte button1LastState = digitalRead(BUTTON1_GPIO);
    static byte button1LastReportedState = button1LastState;
    static unsigned long button1LastChange = millis() - BOUNCING_MS;
    
    static byte button2LastState = digitalRead(BUTTON2_GPIO);
    static byte button2LastReportedState = button2LastState;
    static unsigned long button2LastChange = millis() - BOUNCING_MS;
    
    byte button1NewState = digitalRead(BUTTON1_GPIO);
    if (button1NewState != button1LastState) {
        button1LastState = button1NewState;
        button1LastChange = millis();
    } else {
        if (millis() - button1LastChange >= BOUNCING_MS && 
            button1LastReportedState != button1LastState) {
            
            if (!button1LastState) {  // Button pressed (LOW)
                // Right button - Reset stats
                m_current_stats.hashes = 0;
                m_current_stats.shares_found = 0;
                m_current_stats.crc_errors = 0;
                clearScreen();
            }
            button1LastReportedState = button1LastState;
        }
    }
    
    byte button2NewState = digitalRead(BUTTON2_GPIO);
    if (button2NewState != button2LastState) {
        button2LastState = button2NewState;
        button2LastChange = millis();
    } else {
        if (millis() - button2LastChange >= BOUNCING_MS && 
            button2LastReportedState != button2LastState) {
            
            if (!button2LastState) {  // Button pressed (LOW)
                // Left button - Force page change
                m_current_page = (m_current_page + 1) % 2;
                m_last_page_change = millis();
                clearScreen();
            }
            button2LastReportedState = button2LastState;
        }
    }
}

void DisplayManager::formatHashrate(double rate, char* buffer, size_t len) {
    if (rate >= 1000000) {
        snprintf(buffer, len, "%.2f MH/s", rate / 1000000);
    } else if (rate >= 1000) {
        snprintf(buffer, len, "%.2f KH/s", rate / 1000);
    } else {
        snprintf(buffer, len, "%.1f H/s", rate);
    }
}
#endif