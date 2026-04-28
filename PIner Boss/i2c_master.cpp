#include "i2c_master.h"
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>

I2CMaster::I2CMaster(int bus) 
    : m_i2c_fd(-1)
    , m_bus(bus)
    , m_initialized(false)
    , m_transaction_count(0)
    , m_error_count(0)
    , m_retry_count(0)
    , m_last_error_code(0) {
    std::memset(m_tx_buffer, 0, sizeof(m_tx_buffer));
    std::memset(m_rx_buffer, 0, sizeof(m_rx_buffer));
}

I2CMaster::~I2CMaster() {
    closeDevice();
}

bool I2CMaster::start() {
    if (m_initialized.load()) return true;
    
    std::lock_guard<std::mutex> lock(m_i2c_mutex);
    
    if (openDevice()) {
        m_initialized.store(true);
        return true;
    }
    return false;
}

bool I2CMaster::openDevice() {
    std::ostringstream device_path;
    device_path << "/dev/i2c-" << m_bus;
    
    m_i2c_fd = open(device_path.str().c_str(), O_RDWR | O_CLOEXEC);
    if (m_i2c_fd < 0) {
        std::cerr << "[I2C] Failed to open " << device_path.str() 
                  << " (error: " << strerror(errno) << ")" << std::endl;
        return false;
    }
    
    // Set 10-bit addressing mode (disabled for 7-bit)
    if (ioctl(m_i2c_fd, I2C_TENBIT, 0) < 0) {
        std::cerr << "[I2C] Failed to set 7-bit addressing" << std::endl;
        closeDevice();
        return false;
    }
    
    // NOTE: Pi I2C speed is set via /boot/config.txt
    // Add: dtparam=i2c_arm_baudrate=800000
    // The ioctl doesn't support setting baudrate directly on Pi
    
    std::cout << "[I2C] Master opened on bus " << m_bus << " @ 800kHz" << std::endl;
    return true;
}

void I2CMaster::closeDevice() {
    std::lock_guard<std::mutex> lock(m_i2c_mutex);
    
    if (m_i2c_fd >= 0) {
        close(m_i2c_fd);
        m_i2c_fd = -1;
    }
    m_initialized.store(false);
}

std::vector<uint8_t> I2CMaster::scan(uint8_t start_addr, uint8_t end_addr) {
    std::vector<uint8_t> found;
    
    if (!m_initialized.load()) {
        std::cerr << "[I2C] Not initialized for scan" << std::endl;
        return found;
    }
    
    std::lock_guard<std::mutex> lock(m_i2c_mutex);
    
    for (uint8_t addr = start_addr; addr <= end_addr; addr++) {
        // Skip reserved addresses
        if (addr < 0x08 || addr > 0x77) continue;
        
        // Probe with write transaction (no data)
        struct i2c_msg msg = { addr, 0, 0, nullptr };
        struct i2c_rdwr_ioctl_data io_data = { &msg, 1 };
        
        if (ioctl(m_i2c_fd, I2C_RDWR, &io_data) >= 0) {
            found.push_back(addr);
            m_transaction_count.fetch_add(1);
        } else {
            m_error_count.fetch_add(1);
        }
        
        // Small delay between probes
        usleep(I2C_INTER_TRANSACTION_US);
    }
    
    return found;
}

bool I2CMaster::feedSlavesWithJob(const std::vector<uint8_t>& slave_addresses,
                                  uint8_t job_id,
                                  uint32_t nonce_start,
                                  float difficulty,
                                  const uint8_t* header_buffer,
                                  size_t header_size) {
    if (!m_initialized.load() || slave_addresses.empty()) return false;
    
    // Prepare job request
    JobI2cRequest request;
    request.cmd = I2C_CMD_FEED;
    request.id = job_id;
    request.nonce_start = nonce_start;
    request.difficulty = difficulty;
    
    // Copy header data (safely bounded)
    size_t copy_size = std::min(header_size, sizeof(request.buffer));
    std::memcpy(request.buffer, header_buffer, copy_size);
    
    // Calculate CRC (with CRC field zeroed)
    request.crc = 0;
    request.crc = CryptoUtils::crc8(&request, sizeof(request));
    
    bool all_success = true;
    
    std::lock_guard<std::mutex> lock(m_i2c_mutex);
    
    for (size_t i = 0; i < slave_addresses.size(); i++) {
        uint8_t addr = slave_addresses[i];
        
        // Update nonce start for each slave (distribute work)
        request.nonce_start = nonce_start + (i * NONCE_INCREMENT);
        
        // Recalculate CRC with new nonce_start
        request.crc = 0;
        request.crc = CryptoUtils::crc8(&request, sizeof(request));
        
        bool success = false;
        for (int retry = 0; retry < I2C_RETRY_COUNT && !success; retry++) {
            struct i2c_msg msg = { addr, 0, sizeof(request), 
                                  reinterpret_cast<uint8_t*>(&request) };
            struct i2c_rdwr_ioctl_data io_data = { &msg, 1 };
            
            if (ioctl(m_i2c_fd, I2C_RDWR, &io_data) >= 0) {
                success = true;
                m_transaction_count.fetch_add(1);
            } else {
                m_error_count.fetch_add(1);
                m_last_error_code.store(errno);
                
                if (retry < I2C_RETRY_COUNT - 1) {
                    m_retry_count.fetch_add(1);
                    usleep(I2C_INTER_TRANSACTION_US * 10);
                }
            }
        }
        
        if (!success) {
            all_success = false;
        }
        
        // Inter-transaction delay
        usleep(I2C_INTER_TRANSACTION_US);
    }
    
    return all_success;
}

std::vector<uint32_t> I2CMaster::harvestSlaves(const std::vector<uint8_t>& slave_addresses,
                                               uint8_t /*job_id*/,
                                               uint32_t& total_processed_nonce) {
    std::vector<uint32_t> found_nonces;
    total_processed_nonce = 0;
    
    if (!m_initialized.load()) {
        std::cerr << "[I2C] ERROR: Not initialized!" << std::endl;
        return found_nonces;
    }
    
    if (slave_addresses.empty()) {
        std::cerr << "[I2C] ERROR: No slave addresses!" << std::endl;
        return found_nonces;
    }
    
    std::lock_guard<std::mutex> lock(m_i2c_mutex);
    
    for (uint8_t addr : slave_addresses) {
        JobI2cResult result;
        std::memset(&result, 0, sizeof(result));
        
        // DEBUG: Show read attempt
        static uint32_t read_attempts = 0;
        read_attempts++;
        
        bool success = false;
        int last_errno = 0;
        
        for (int retry = 0; retry < I2C_RETRY_COUNT && !success; retry++) {
            struct i2c_msg msg = { 
                addr, 
                I2C_M_RD, 
                sizeof(result), 
                reinterpret_cast<uint8_t*>(&result) 
            };
            struct i2c_rdwr_ioctl_data io_data = { &msg, 1 };
            
            int ioctl_result = ioctl(m_i2c_fd, I2C_RDWR, &io_data);
            
            if (ioctl_result >= 0) {
                success = true;
                m_transaction_count.fetch_add(1);
            } else {
                last_errno = errno;
                m_error_count.fetch_add(1);
                m_last_error_code.store(errno);
                
                if (retry < I2C_RETRY_COUNT - 1) {
                    m_retry_count.fetch_add(1);
                    usleep(I2C_INTER_TRANSACTION_US * 10);
                }
            }
        }
        
        // DEBUG: Log every read attempt
        if (read_attempts % 10 == 0) {
            std::cout << "[I2C] Read from 0x" << std::hex << (int)addr << std::dec
                      << " success=" << (success ? "YES" : "NO")
                      << " errno=" << last_errno << std::endl;
        }
        
        if (!success) {
            std::cerr << "[I2C] ✗ Read failed from 0x" << std::hex << (int)addr 
                      << std::dec << " (errno=" << last_errno << ")" << std::endl;
            continue;
        }
        
        // DEBUG: Show raw data received
        if (read_attempts % 20 == 0) {
            std::cout << "[I2C] Raw bytes: ";
            for (size_t i = 0; i < sizeof(result); i++) {
                printf("%02X ", ((uint8_t*)&result)[i]);
            }
            std::cout << std::endl;
        }
        
        // Verify CRC
        uint8_t received_crc = result.crc;
        result.crc = 0;
        uint8_t calculated_crc = CryptoUtils::crc8(&result, sizeof(result));
        
        if (calculated_crc != received_crc) {
            std::cerr << "[I2C] ✗ CRC mismatch from 0x" << std::hex << (int)addr 
                      << std::dec << " (recv=0x" << std::hex << (int)received_crc 
                      << ", calc=0x" << (int)calculated_crc << ")" << std::dec << std::endl;
            m_error_count.fetch_add(1);
            continue;
        }
        
        // SUCCESS: Add hashes to total
        total_processed_nonce += result.processed_nonce;
        
        // Collect nonce if valid
        if (result.nonce != 0xFFFFFFFF) {
            found_nonces.push_back(result.nonce);
            std::cout << "[I2C] ✓ Found nonce 0x" << std::hex << result.nonce 
                      << std::dec << " from 0x" << (int)addr << std::endl;
        }
        
        // DEBUG: Log successful harvest
        if (read_attempts % 10 == 0) {
            std::cout << "[I2C] ✓ 0x" << std::hex << (int)addr << std::dec
                      << " hashes=" << result.processed_nonce
                      << " total=" << total_processed_nonce << std::endl;
        }
        
        // Inter-transaction delay
        usleep(I2C_INTER_TRANSACTION_US);
    }
    
    return found_nonces;
}


bool I2CMaster::writeBytes(uint8_t addr, const uint8_t* data, size_t len) {
    if (!m_initialized.load() || len > sizeof(m_tx_buffer)) return false;
    
    std::lock_guard<std::mutex> lock(m_i2c_mutex);
    
    std::memcpy(m_tx_buffer, data, len);
    
    struct i2c_msg msg = { addr, 0, static_cast<uint16_t>(len), m_tx_buffer };
    struct i2c_rdwr_ioctl_data io_data = { &msg, 1 };
    
    bool success = ioctl(m_i2c_fd, I2C_RDWR, &io_data) >= 0;
    
    if (success) {
        m_transaction_count.fetch_add(1);
    } else {
        m_error_count.fetch_add(1);
        m_last_error_code.store(errno);
    }
    
    return success;
}

bool I2CMaster::readBytes(uint8_t addr, uint8_t* data, size_t len) {
    if (!m_initialized.load() || len > sizeof(m_rx_buffer)) return false;
    
    std::lock_guard<std::mutex> lock(m_i2c_mutex);
    
    struct i2c_msg msg = { addr, I2C_M_RD, static_cast<uint16_t>(len), m_rx_buffer };
    struct i2c_rdwr_ioctl_data io_data = { &msg, 1 };
    
    bool success = ioctl(m_i2c_fd, I2C_RDWR, &io_data) >= 0;
    
    if (success) {
        std::memcpy(data, m_rx_buffer, len);
        m_transaction_count.fetch_add(1);
    } else {
        m_error_count.fetch_add(1);
        m_last_error_code.store(errno);
    }
    
    return success;
}

I2CMaster::I2CStats I2CMaster::getStats() const {
    I2CStats stats;
    stats.transactions = m_transaction_count.load();
    stats.errors = m_error_count.load();
    stats.retries = m_retry_count.load();
    stats.last_error_code = m_last_error_code.load();
    return stats;
}

void I2CMaster::resetStats() {
    m_transaction_count.store(0);
    m_error_count.store(0);
    m_retry_count.store(0);
    m_last_error_code.store(0);
}
