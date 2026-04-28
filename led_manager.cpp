#include "led_manager.h"

LedManager::LedManager() 
    : m_current_pattern(LED_OFF)
    , m_last_update(0)
    , m_pattern_state(0)
    , m_led_state(false) {
}

LedManager& LedManager::getInstance() {
    static LedManager instance;
    return instance;
}

void LedManager::begin() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
    Serial.println("[LED] Manager initialized");
}

void LedManager::setLed(bool on) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
    m_led_state = on;
}

void LedManager::setPattern(LedPattern pattern) {
    m_current_pattern.store(pattern);
    m_pattern_state = 0;
    m_last_update = millis();
}

void LedManager::update() {
    uint32_t now = millis();
    updatePattern(m_current_pattern.load(), now);
}

void LedManager::updatePattern(LedPattern pattern, uint32_t now) {
    // Minimum update interval (prevent flickering)
    if (now - m_last_update < 10) return;
    
    switch (pattern) {
        case LED_OFF:
            setLed(false);
            break;
            
        case LED_MINING_IDLE:
            // Slow pulse (2 seconds on, 2 seconds off)
            if (now - m_last_update >= 2000) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
            
        case LED_MINING_ACTIVE:
            // Fast pulse (500ms on, 500ms off)
            if (now - m_last_update >= 500) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
            
        case LED_SHARE_FOUND:
            // Bright flash (3 quick blinks)
            if (now - m_last_update >= 150) {
                m_pattern_state++;
                if (m_pattern_state >= 6) {  // 3 on/off cycles
                    setPattern(LED_MINING_ACTIVE);  // Return to active
                    m_pattern_state = 0;
                } else {
                    setLed(m_pattern_state % 2 == 0);
                }
                m_last_update = now;
            }
            break;
            
        case LED_ERROR:
            // Fast blink (200ms on, 200ms off)
            if (now - m_last_update >= 200) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
            
        case LED_NO_JOB:
            // Very slow blink (3 seconds on, 3 seconds off)
            if (now - m_last_update >= 3000) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
    }
}
