#ifndef BOARDS_H
#define BOARDS_H


// ============================================================================
// BOARD DEFINITIONS
// ============================================================================

#if defined(BOARD_TTGO_T_DISPLAY)
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
    #define SCREEN

#elif defined(BOARD_WAVESHARE_35_LCD)
    #define SCREEN_WIDTH      480
    #define SCREEN_HEIGHT     320
    #define TFT_BL_PIN        27
    #define I2C_SDA_PIN       21
    #define I2C_SCL_PIN       22 
    #define ADC_PIN           -1
    #define ADC_POWER_PIN     -1
    #define HAS_BUTTONS       false
    #define BUTTON1_PIN       -1
    #define BUTTON2_PIN       -1
    #define DISPLAY_USE_TFT_ESPI
    #define SCREEN
    
#elif defined(esp32dev)
    //#define BUTTON1_PIN       2
    //#define BUTTON2_PIN       -1
    #define I2C_SDA_PIN       21
    #define I2C_SCL_PIN       22

#else
    #error "No valid board selected in boards.h!"
#endif

#endif
