#include "i2c_slave.h"
#include "config.h"
#include "sha256_optimized.h"

I2CSlave* I2CSlave::s_instance = nullptr;

// Constructor
I2CSlave::I2CSlave(uint8_t addr) 
    : m_addr(addr)
    , m_new_job_available(false)
    , m_hashes_since_poll(0)
    , m_found_nonce(0xFFFFFFFF)
    , m_last_heartbeat(millis())
    , m_jobs_received(0)
    , m_crc_errors(0)
    , m_i2c_requests(0)
    , m_mining_enabled(true) {
    s_instance = this;
    memset(&m_current_job, 0, sizeof(m_current_job));
}

I2CSlave::~I2CSlave() {
    s_instance = nullptr;
}

// Initialization
void I2CSlave::begin(uint8_t address) {
    DEBUG_PRINTF("[I2C] Starting slave at 0x%02X\n", address);
    
    Wire.begin(address, I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_SPEED);
    Wire.onReceive(handleReceive);
    Wire.onRequest(handleRequest);
    
    m_addr = address;
    m_last_heartbeat = millis();
    m_mining_enabled = true;
    
    DEBUG_PRINTF("[I2C] Slave started successfully\n");
}

// CRC8 Functions
uint8_t I2CSlave::calculateCRC8(const void* data, size_t len) {
    return crc8_compute(data, len);
}

bool I2CSlave::verifyCRC(void* data, size_t len) {
    uint8_t* ptr = static_cast<uint8_t*>(data);
    uint8_t received_crc = ptr[1];
    ptr[1] = 0;
    uint8_t calculated_crc = calculateCRC8(data, len);
    ptr[1] = received_crc;
    return (calculated_crc == received_crc);
}

// I2C Receive Handler (PI -> ESP32)
void I2CSlave::handleReceive(int len) {
    if (!s_instance) return;
    
    s_instance->m_i2c_requests++;
    
    if (len < 1) return;
    
    uint8_t cmd = Wire.read();
    len--;
    
    switch (cmd) {
        case I2C_CMD_PING:
            s_instance->m_last_heartbeat = millis();
            break;
            
        case I2C_CMD_RESET:
            DEBUG_PRINTLN("[I2C] RESET received");
            ESP.restart();
            break;
            
        case I2C_CMD_STOP:
            s_instance->m_mining_enabled = false;
            break;
            
        case I2C_CMD_FEED:
            if (len >= 87) {
                JobI2cRequest temp;
                temp.cmd = cmd;
                Wire.readBytes((uint8_t*)&temp.crc, 87);
                
                if (s_instance->verifyCRC(&temp, sizeof(temp))) {
                    s_instance->m_current_job = temp;
                    s_instance->m_new_job_available.store(true);
                    s_instance->m_last_heartbeat = millis();
                    s_instance->m_mining_enabled = true;
                    s_instance->m_jobs_received++;
                    
                    DEBUG_PRINTF("[I2C] Job accepted: id=%d nonce_start=%lu\n", 
                                 temp.id, temp.nonce_start);
                } else {
                    s_instance->m_crc_errors++;
                    DEBUG_PRINTLN("[I2C] Job CRC FAILED");
                }
            } else {
                DEBUG_PRINTF("[I2C] Wrong packet size: %d\n", len);
                while (Wire.available()) Wire.read();
            }
            break;
            
        case I2C_CMD_REQUEST_RESULT:
            break;
            
        default:
            DEBUG_PRINTF("[I2C] Unknown cmd: 0x%02X\n", cmd);
            break;
    }
}

// I2C Request Handler (ESP32 -> PI)
void I2CSlave::handleRequest() {
    if (!s_instance) return;
    
    s_instance->m_i2c_requests++;
    
    // Copy values BEFORE resetting (atomic operations)
    uint32_t found_nonce = s_instance->m_found_nonce.load(std::memory_order_acquire);
    uint32_t hashes = s_instance->m_hashes_since_poll.load(std::memory_order_acquire);
    uint8_t job_id = s_instance->m_current_job.id;
    
    // Prepare result packet
    JobI2cResult res;
    res.cmd = I2C_CMD_SLAVE_RESULT;
    res.id = job_id;
    res.reserved = 0;
    res.nonce = found_nonce;
    res.processed_nonce = hashes;
    
    // Reset AFTER copying
    if (found_nonce != 0xFFFFFFFF) {
        s_instance->m_found_nonce.store(0xFFFFFFFF, std::memory_order_release);
    }
    s_instance->m_hashes_since_poll.store(0, std::memory_order_release);
    
    // Calculate CRC
    res.crc = 0;
    res.crc = s_instance->calculateCRC8(&res, sizeof(res));
    
    // Send result
    Wire.write((uint8_t*)&res, sizeof(JobI2cResult));
}
