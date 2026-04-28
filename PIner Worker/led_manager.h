#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <atomic>

// LED Pin Configuration
#define LED_PIN 2  // Built-in LED on most ESP32 boards

// LED Patterns
enum LedPattern {
    LED_OFF = 0,
    LED_MINING_IDLE,      // Slow pulse - mining, no shares
    LED_MINING_ACTIVE,    // Fast pulse - hashing actively
    LED_SHARE_FOUND,      // Bright flash - share found!
    LED_ERROR,            // Fast blink - error/no connection
    LED_NO_JOB            // Slow blink - waiting for job
};

class LedManager {
public:
    static LedManager& getInstance();
    
    void begin();
    void setPattern(LedPattern pattern);
    LedPattern getPattern() const { return m_current_pattern.load(); }
    
    // Call this periodically from a task (non-blocking)
    void update();
    
private:
    LedManager();
    
    std::atomic<LedPattern> m_current_pattern;
    uint32_t m_last_update;
    uint32_t m_pattern_state;
    bool m_led_state;
    
    void setLed(bool on);
    void updatePattern(LedPattern pattern, uint32_t now);
};

// Global instance
#define LED LedManager::getInstance()

#endif // LED_MANAGER_H
