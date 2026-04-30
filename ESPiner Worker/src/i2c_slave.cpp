#include "i2c_slave.h"

I2CSlave* I2CSlave::s_instance = nullptr;

I2CSlave::I2CSlave(uint8_t addr) : m_addr(addr), m_mining_enabled(true) {
    s_instance = this;
    m_jobMutex = xSemaphoreCreateMutex();
    m_hashes_since_poll = 0;
    m_found_nonce = 0xFFFFFFFF;
    m_new_job_available = false;
}

void I2CSlave::begin(uint8_t address) {
    Wire.setBufferSize(I2C_BUFFER_SIZE); 
    Wire.begin(address, I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_SPEED);
    Wire.onReceive(handleReceive);
    Wire.onRequest(handleRequest);
}

// ISR SAFE: No Serial prints here!
void I2CSlave::handleReceive(int len) {
    if (!s_instance || len < 1) return;
    
    uint8_t cmd = Wire.read();
    if (cmd == I2C_CMD_FEED && len >= sizeof(JobI2cRequest)) {
        JobI2cRequest temp;
        temp.cmd = cmd;
        Wire.readBytes((uint8_t*)&temp.nonce_start_byte, sizeof(JobI2cRequest) - 1);
        
        // Use a non-blocking try-take for the ISR
        if (xSemaphoreTakeFromISR(s_instance->m_jobMutex, NULL) == pdTRUE) {
            s_instance->m_current_job = temp;
            s_instance->m_new_job_available = true;
            s_instance->m_mining_enabled = true;
            xSemaphoreGiveFromISR(s_instance->m_jobMutex, NULL);
        }
    } else if (cmd == I2C_CMD_RESET) {
        ESP.restart();
    }
}

void I2CSlave::handleRequest() {
    if (!s_instance) return;
    
    JobI2cResult res;
    res.cmd = I2C_CMD_SLAVE_RESULT;
    res.id = 0x01; // Example ID
    res.nonce = s_instance->m_found_nonce.exchange(0xFFFFFFFF);
    res.hashrate_raw = s_instance->m_hashes_since_poll.exchange(0);
    res.crc = 0; // Simplified CRC for this example
    
    Wire.write((uint8_t*)&res, sizeof(res));
}

bool I2CSlave::hasNewJob() {
    return m_new_job_available;
}

void I2CSlave::getJob(JobI2cRequest &dest) {
    if (xSemaphoreTake(m_jobMutex, pdMS_TO_TICKS(10))) {
        dest = m_current_job;
        xSemaphoreGive(m_jobMutex);
    }
}

void I2CSlave::clearNewJob() {
    m_new_job_available = false;
}