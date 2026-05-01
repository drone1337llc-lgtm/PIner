#ifndef ADDRESS_UTILS_H
#define ADDRESS_UTILS_H

#include <Arduino.h>
#include <esp_system.h>

// REPLACE your address derivation code with this:

uint8_t deriveI2CAddress() {
    // Get MAC address
    uint64_t chipid = ESP.getEfuseMac();
    uint8_t mac_last = chipid & 0xFF;
    
    // Map to I2C address range 0x10-0x70
    // Use different bits to avoid conflicts
    uint8_t addr = 0x10 + (mac_last & 0x0F);  // Use lower 4 bits only
    
    // Ensure address is in valid range and odd (I2C convention)
    if (addr < 0x10) addr = 0x10;
    if (addr > 0x70) addr = 0x70;
    
    Serial.printf("[Address] MAC last byte: 0x%02X -> I2C addr: 0x%02X\n", mac_last, addr);
    
    return addr;
}

// OR better yet - use DIP switches or manual assignment:
uint8_t getI2CAddress() {
    // Option 1: Read from GPIO pins (3 pins = 8 possible addresses)
    // pinMode(12, INPUT_PULLUP);
    // pinMode(13, INPUT_PULLUP);
    // pinMode(14, INPUT_PULLUP);
    // uint8_t addr = 0x10 + (digitalRead(12) | (digitalRead(13) << 1) | (digitalRead(14) << 2));
    
    // Option 2: Hardcode unique addresses per board
    #if defined(BOARD_ID)
        return 0x10 + BOARD_ID;
    #else
        return deriveI2CAddress();
    #endif
}


#endif
