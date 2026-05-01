#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "i2c_protocol.h"

int i2c_master_init();
std::vector<uint8_t> i2c_master_scan(uint8_t start, uint8_t end);
void i2c_feed_slaves(const std::vector<uint8_t>& slaves, uint8_t id, 
                     uint8_t nonce_start, float difficulty, const uint8_t* buffer);
void i2c_hit_slaves(const std::vector<uint8_t>& slaves);
std::vector<uint32_t> i2c_harvest_slaves(const std::vector<uint8_t>& slaves, 
                                          uint8_t id, uint32_t &total_processed_nonce);

#endif
