#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <atomic>
#include "config.h"

enum LedPattern {
    LED_OFF = 0,
    LED_MINING_IDLE,
    LED_MINING_ACTIVE,
    LED_SHARE_FOUND,
    LED_ERROR,
    LED_NO_JOB,
    LED_NO_HEARTBEAT
};

class LedManager {
public:
    static LedManager& getInstance();
    
    void begin();
    void setPattern(LedPattern pattern);
    LedPattern getPattern() const { return m_current_pattern.load(std::memory_order_acquire); }
    void update();
    
private:
    LedManager();
    
    std::atomic<LedPattern> m_current_pattern{LED_OFF};
    uint32_t m_last_update = 0;
    uint32_t m_pattern_state = 0;
    bool m_led_state = false;
    
    void setLed(bool on);
    void updatePattern(LedPattern pattern, uint32_t now);
};

#define LED LedManager::getInstance()

#endif
