#include "led_manager.h"
#include "config.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

LedManager& LedManager::getInstance() {
    static LedManager instance;
    return instance;
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

LedManager::LedManager() 
    : m_current_pattern(LED_OFF)
    , m_last_update(0)
    , m_pattern_state(0)
    , m_led_state(false) {
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void LedManager::begin() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
    DEBUG_PRINTLN("[LED] Manager initialized");
}

// ============================================================================
// LED CONTROL
// ============================================================================

void LedManager::setLed(bool on) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
    m_led_state = on;
}

void LedManager::setPattern(LedPattern pattern) {
    m_current_pattern.store(pattern);
    m_pattern_state = 0;
    m_last_update = millis();
}

// ============================================================================
// PATTERN UPDATE (NON-BLOCKING)
// ============================================================================

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
            // Solid on while hashing
            setLed(true);
            break;
            
        case LED_SHARE_FOUND:
            // Quick flash (3 blinks)
            if (now - m_last_update >= 100) {
                m_pattern_state++;
                if (m_pattern_state >= 6) {
                    setPattern(LED_MINING_ACTIVE);
                    m_pattern_state = 0;
                } else {
                    setLed(m_pattern_state % 2 == 0);
                }
                m_last_update = now;
            }
            break;
            
        case LED_ERROR:
            // Fast blink (200ms)
            if (now - m_last_update >= 200) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
            
        case LED_NO_JOB:
            // Very slow blink (3 seconds)
            if (now - m_last_update >= 3000) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
            
        case LED_NO_HEARTBEAT:
            // Alternating fast/slow
            if (now - m_last_update >= 500) {
                m_pattern_state++;
                setLed((m_pattern_state % 4) < 2);
                m_last_update = now;
            }
            break;
    }
}
