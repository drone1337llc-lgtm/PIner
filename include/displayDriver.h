#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <LovyanGFX.hpp>

class LGFX_Master : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance; // Change to Panel_ST7789 if your screen is that model
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance; // Adds backlight control

public:
    LGFX_Master() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.pin_sclk = 18; 
            cfg.pin_mosi = 23;
            cfg.pin_miso = -1;
            cfg.pin_dc   = 2;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs    = 15;
            cfg.pin_rst   = 4;  // Updated from -1 to 4 based on your previous config
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 32;    // Backlight pin
            cfg.invert = false; // Set to true if backlight is active low
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};

extern LGFX_Master tft;
extern LGFX_Sprite canvas;

void initDisplay();
void updateUI(int slaveCount, float diff, uint32_t uptime, String status);

#endif