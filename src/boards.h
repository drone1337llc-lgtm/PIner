// --- FILE: boards.h ---
#ifndef BOARDS_H
#define BOARDS_H

// ============================================================================
// ACTIVE BOARD SELECTION (Uncomment ONLY one)
// ============================================================================
// #define BOARD_TTGO_T_DISPLAY
#define BOARD_WAVESHARE_35_LCD

// ============================================================================
// BOARD DEFINITIONS
// ============================================================================

#if defined(BOARD_TTGO_T_DISPLAY)
    // TTGO T-Display (135x240)
    #define SCREEN_WIDTH      240
    #define SCREEN_HEIGHT     135
    #define TFT_BL_PIN        4
    #define I2C_SDA_PIN       21
    #define I2C_SCL_PIN       22
    #define ADC_PIN           34
    #define ADC_POWER_PIN     14
    #define HAS_BUTTONS       true
    #define BUTTON1_PIN       35
    #define BUTTON2_PIN       0
    #define DISPLAY_USE_TFT_ESPI

#elif defined(BOARD_WAVESHARE_35_LCD)
    // Waveshare 3.5" Touch LCD on ESP32-D0WDR2-V3 (480x320)
    #define SCREEN_WIDTH      480
    #define SCREEN_HEIGHT     320
    #define TFT_BL_PIN        27 // Standard PWM backlight pin for this module
    #define I2C_SDA_PIN       21 // Standard ESP32 I2C
    #define I2C_SCL_PIN       22 
    #define ADC_PIN           -1 // Unused on this board
    #define ADC_POWER_PIN     -1 // Unused
    #define HAS_BUTTONS       false
    #define BUTTON1_PIN       -1
    #define BUTTON2_PIN       -1
    #define DISPLAY_USE_TFT_ESPI
    
#else
    #error "No valid board selected in boards.h!"
#endif

#endif