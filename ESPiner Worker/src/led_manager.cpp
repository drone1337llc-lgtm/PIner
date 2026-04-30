#include "led_manager.h"
#include "config.h"

LedManager& LedManager::getInstance() {
    static LedManager instance;
    return instance;
}

LedManager::LedManager() 
    : m_current_pattern(LED_OFF)
    , m_last_update(0)
    , m_pattern_state(0)
    , m_led_state(false) {
}

void LedManager::begin() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
}

void LedManager::setLed(bool on) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
    m_led_state = on;
}

void LedManager::setPattern(LedPattern pattern) {
    m_current_pattern.store(pattern, std::memory_order_release);
    m_pattern_state = 0;
    m_last_update = millis();
}

void LedManager::update() {
    uint32_t now = millis();
    updatePattern(m_current_pattern.load(std::memory_order_acquire), now);
}

void LedManager::updatePattern(LedPattern pattern, uint32_t now) {
    if (now - m_last_update < 10) return;  // Min update interval
    
    switch (pattern) {
        case LED_OFF:
            setLed(false);
            break;
        case LED_MINING_IDLE:
            if (now - m_last_update >= 2000) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
        case LED_MINING_ACTIVE:
            setLed(true);
            break;
        case LED_SHARE_FOUND:
            if (now - m_last_update >= 100) {
                m_pattern_state++;
                if (m_pattern_state >= 6) {
                    setPattern(LED_MINING_ACTIVE);
                } else {
                    setLed(m_pattern_state % 2 == 0);
                }
                m_last_update = now;
            }
            break;
        case LED_ERROR:
            if (now - m_last_update >= 200) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
        case LED_NO_JOB:
            if (now - m_last_update >= 3000) {
                m_pattern_state = !m_pattern_state;
                setLed(m_pattern_state);
                m_last_update = now;
            }
            break;
        case LED_NO_HEARTBEAT:
            if (now - m_last_update >= 500) {
                m_pattern_state++;
                setLed((m_pattern_state % 4) < 2);
                m_last_update = now;
            }
            break;
    }
}
