#include "i2c_slave.h"
#include "sha256_optimized.h"
#include <esp_system.h>

I2CSlave* I2CSlave::s_instance = nullptr;

I2CSlave& I2CSlave::getInstance() {
    static I2CSlave instance;
    return instance;
}

I2CSlave::I2CSlave()
    : m_addr(0x10)
    , m_mining_enabled(false)
    , m_jobMutex(xSemaphoreCreateMutex()) {
    s_instance = this;
}

void I2CSlave::begin(uint8_t address) {
    m_addr = address;
    Wire.begin(m_addr);
    Wire.onReceive(handleReceive);
    Wire.onRequest(handleRequest);
    DEBUG_PRINTF("[I2C] Slave started on address: 0x%02X\n", m_addr);
}

void I2CSlave::getJob(JobI2cRequest &dest) {
    if (xSemaphoreTake(m_jobMutex, pdMS_TO_TICKS(100))) {
        memcpy(&dest, &m_current_job, sizeof(JobI2cRequest));
        xSemaphoreGive(m_jobMutex);
    }
}

void I2CSlave::clearNewJob() {
    m_new_job_available.store(false);
}

void I2CSlave::setFoundNonce(uint32_t nonce) {
    m_found_nonce.store(nonce);
}

uint32_t I2CSlave::getFoundNonce() {
    return m_found_nonce.exchange(0xFFFFFFFF);
}

void I2CSlave::clearFoundNonce() {
    m_found_nonce.store(0xFFFFFFFF);
}

void I2CSlave::handleReceive(int len) {
    if (!s_instance || len < JOB_REQUEST_SIZE) {
        return;
    }
    
    uint8_t buf[JOB_REQUEST_SIZE];
    int bytesRead = 0;
    while(Wire.available() && bytesRead < JOB_REQUEST_SIZE) {
        buf[bytesRead++] = Wire.read();
    }

    if (bytesRead != JOB_REQUEST_SIZE) {
        s_instance->m_crc_errors.fetch_add(1);
        return;
    }
    
    uint8_t received_crc = buf[JOB_REQUEST_SIZE - 1];
    uint8_t calculated_crc = crc8_compute(buf, JOB_REQUEST_SIZE - 1);
    
    if (calculated_crc == received_crc) {
        if (xSemaphoreTake(s_instance->m_jobMutex, pdMS_TO_TICKS(100))) {
            memcpy(&s_instance->m_current_job, buf, JOB_REQUEST_SIZE);
            s_instance->m_new_job_available.store(true);
            xSemaphoreGive(s_instance->m_jobMutex);
        }
    } else {
        s_instance->m_crc_errors.fetch_add(1);
        DEBUG_PRINTF("[I2C] CRC Error: expected 0x%02X, got 0x%02X\n", 
                     calculated_crc, received_crc);
    }
}

void I2CSlave::handleRequest() {
    if (!s_instance) return;

    I2CStatusResponse res;
    uint32_t nonce = s_instance->getFoundNonce();
    
    res.cmd = I2C_CMD_SLAVE_RESULT;
    res.status = (nonce != 0xFFFFFFFF) ? 0x02 : 0x01;
    res.nonce = nonce;
    res.crc = crc8_compute((uint8_t*)&res, STATUS_RESPONSE_SIZE - 1);
    
    Wire.write((uint8_t*)&res, STATUS_RESPONSE_SIZE);
}
