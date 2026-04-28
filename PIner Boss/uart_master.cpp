#include "uart_master.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

const uint8_t UartMaster::s_crc8_table[256] = {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97,
    0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4,
    0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11,
    0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52,
    0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA,
    0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9,
    0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C,
    0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F,
    0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED,
    0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE,
    0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B,
    0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28,
    0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0,
    0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93,
    0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56,
    0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15,
    0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

UartMaster::UartMaster(const std::string& device, int baud_rate)
    : m_uart_fd(-1), m_device(device), m_baud_rate(baud_rate), m_connected(false), m_running(false)
{
}

UartMaster::~UartMaster()
{
    stop();
}

uint8_t UartMaster::calculateCRC8(const void* data, size_t len)
{
    const uint8_t* ptr = (const uint8_t*)data;
    uint8_t crc = 0xFF;
    crc = s_crc8_table[crc ^ ptr[0]];
    for (size_t n = 2; n < len; ++n)
        crc = s_crc8_table[crc ^ ptr[n]];
    return crc;
}

bool UartMaster::start()
{
    if (m_connected) return true;
    
    if (!openUart()) {
        return false;
    }
    
    m_running = true;
    m_receive_thread = std::thread(&UartMaster::receiveLoop, this);
    
    return true;
}

void UartMaster::stop()
{
    m_running = false;
    if (m_receive_thread.joinable()) {
        m_receive_thread.join();
    }
    closeUart();
    m_connected = false;
}

bool UartMaster::openUart()
{
    m_uart_fd = open(m_device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (m_uart_fd < 0) {
        return false;
    }
    
    struct termios tty;
    if (tcgetattr(m_uart_fd, &tty) != 0) {
        close(m_uart_fd);
        return false;
    }
    
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    
    if (tcsetattr(m_uart_fd, TCSANOW, &tty) != 0) {
        close(m_uart_fd);
        return false;
    }
    
    m_connected = true;
    return true;
}

void UartMaster::closeUart()
{
    if (m_uart_fd >= 0) {
        close(m_uart_fd);
        m_uart_fd = -1;
    }
}

void UartMaster::sendJob(const UartJobRequest& job)
{
    if (!m_connected) return;
    
    std::string message = "JOB:";
    message += std::to_string(job.job_id) + ":";
    message += std::to_string(job.difficulty) + ":";
    message += std::to_string(job.nonce_start) + ":";
    
    char hex_buf[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hex_buf + i * 2, "%02x", job.merkle_root[i]);
    }
    message += std::string(hex_buf, 64) + ":";
    
    for (int i = 0; i < 32; i++) {
        sprintf(hex_buf + i * 2, "%02x", job.prev_block_hash[i]);
    }
    message += std::string(hex_buf, 64) + ":";
    
    for (int i = 0; i < 4; i++) {
        sprintf(hex_buf + i * 2, "%02x", job.version[i]);
    }
    message += std::string(hex_buf, 8) + ":";
    
    for (int i = 0; i < 4; i++) {
        sprintf(hex_buf + i * 2, "%02x", job.nbits[i]);
    }
    message += std::string(hex_buf, 8) + ":";
    
    for (int i = 0; i < 4; i++) {
        sprintf(hex_buf + i * 2, "%02x", job.ntime[i]);
    }
    message += std::string(hex_buf, 8) + "\n";
    
    write(m_uart_fd, message.c_str(), message.length());
}

bool UartMaster::receiveResult(UartJobResult& /*result*/, int /*timeout_ms*/)
{
    return false;
}

void UartMaster::receiveLoop()
{
    std::cout << "[UART " << m_device << "] Receive loop started" << std::endl;
    
    char buffer[256];
    std::string received_data;
    
    while (m_running) {
        int bytes_read = read(m_uart_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            received_data += buffer;
            
            std::cout << "[UART " << m_device << "] Received " << bytes_read 
                      << " bytes: " << buffer << std::endl;  // Debug output
            
            size_t pos;
            while ((pos = received_data.find('\n')) != std::string::npos) {
                std::string line = received_data.substr(0, pos);
                received_data.erase(0, pos + 1);
                
                std::cout << "[UART " << m_device << "] Processing: " << line << std::endl;
                
                if (line.find("REQUEST_JOB") == 0) {
                    std::cout << "[UART " << m_device << "] JOB REQUEST DETECTED!" << std::endl;
                    if (m_job_request_callback) {
                        m_job_request_callback();
                    }
                }
                else if (line.find("RESULT:") == 0) {
                    // Parse result...
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "[UART " << m_device << "] Receive loop stopped" << std::endl;
}

void UartMaster::setJobRequestCallback(std::function<void()> callback)
{
    m_job_request_callback = callback;
}

void UartMaster::setResultCallback(std::function<void(const UartJobResult&)> callback)
{
    m_result_callback = callback;
}
