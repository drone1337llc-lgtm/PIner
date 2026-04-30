#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <LovyanGFX.hpp>

class LGFX_Master : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_Master()
    {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = VSPI_HOST; // Standard for Pins 18/23
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_write = 20000000; // 16MHz is rock solid for standard ESP32
            cfg.freq_read = 20000000;
            cfg.pin_sclk = 18; // D18
            cfg.pin_mosi = 23; // D23
            cfg.pin_miso = -1;
            cfg.pin_dc = 2; // D2
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = 15; // D15
            cfg.pin_rst = 4; // D4
            cfg.invert = true;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 32; // D32
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};

extern LGFX_Master tft;
extern LGFX_Sprite canvas;

void initDisplay();
void updateUI(int slaveCount, float totalHashrate, float diff, uint32_t uptime, String status);

#endif