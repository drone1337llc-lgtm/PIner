#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <atomic>
#include "config.h"

// ============================================================================
// LED PATTERNS
// ============================================================================

enum LedPattern {
    LED_OFF = 0,
    LED_MINING_IDLE,      // Slow pulse - mining, no shares
    LED_MINING_ACTIVE,    // On - actively hashing
    LED_SHARE_FOUND,      // Flash - share found!
    LED_ERROR,            // Fast blink - error
    LED_NO_JOB,           // Slow blink - waiting for job
    LED_NO_HEARTBEAT      // Alternating - PI disconnected
};

// ============================================================================
// LED MANAGER CLASS
// ============================================================================

class LedManager {
public:
    static LedManager& getInstance();
    
    void begin();
    void setPattern(LedPattern pattern);
    LedPattern getPattern() const { return m_current_pattern.load(); }
    void update();  // Call periodically (non-blocking)
    
private:
    LedManager();
    
    std::atomic<LedPattern> m_current_pattern{LED_OFF};
    uint32_t m_last_update = 0;
    uint32_t m_pattern_state = 0;
    bool m_led_state = false;
    
    void setLed(bool on);
    void updatePattern(LedPattern pattern, uint32_t now);
};

// Global instance macro
#define LED LedManager::getInstance()

#endif
