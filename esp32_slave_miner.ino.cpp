#include "config.h"  
#include <Arduino.h>
#include <Wire.h>
#include "miner_core.h"
#include "i2c_slave.h"

#ifdef LCD
#include "display_manager.h"  // Included AFTER miner_core.h
#define ADDR_PIN 27
#endif
#if defined(esp32dev) || defined(esp32c3)
#define ADDR_PIN 34
#endif
#define BASE_ADDR 0x10
#define HEARTBEAT_TIMEOUT_MS 5000
unsigned long last_heartbeat = 0;
bool mining_enabled = false;

uint8_t getSlotID()
{
    pinMode(ADDR_PIN, INPUT);
    int val = 0;
    for (int i = 0; i < 10; i++)
        val += analogRead(ADDR_PIN);
    val /= 10;
    if (val < 300)
        return 0;
    if (val < 1000)
        return 1;
    if (val < 1600)
        return 2;
    if (val < 2400)
        return 3;
    return 7;
}

I2CSlave *i2c_slave = nullptr;
MinerCore miner;
volatile uint32_t g_found_nonce = 0xFFFFFFFF;

#ifdef LCD
DisplayManager display;
#endif

void onI2CCommand(uint8_t cmd) {
    if (cmd == I2C_CMD_PING) {
        last_heartbeat = millis();  // Reset heartbeat timer
        mining_enabled = true;       // Keep mining active
    }
    else if (cmd == I2C_CMD_RESET) {
        mining_enabled = false;      // Stop immediately
        last_heartbeat = millis();
    }
    else if (cmd == I2C_CMD_FEED) {
        last_heartbeat = millis();
        mining_enabled = true;
        // ... process job ...
    }
}

void setup()
{
    Serial.begin(115200);
    analogReadResolution(12);

    disableCore0WDT();

    uint8_t slot = getSlotID();
    uint8_t myAddr = BASE_ADDR + slot;

    Serial.printf("\n=== ESP32 MINER ===\n");
    Serial.printf("Slot: %d | Address: 0x%02X | CPU: %d MHz\n",
                  slot + 1, myAddr, ESP.getCpuFreqMHz());
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());

#ifdef LCD
    Serial.println("Initializing LCD...");
    if (!display.begin()) {
        Serial.println("LCD Init Failed");
    } else {
        display.showBootScreen();
        Serial.println("LCD Ready");
    }
#endif

    i2c_slave = new I2CSlave(myAddr, 21, 22);
    if (!i2c_slave->begin())
    {
        Serial.println("I2C Init Failed");
        while (1);
    }

    miner.begin();
    miner.startMining();
    Serial.println("System Online - Mining Started");
}

void loop()
{
    if (mining_enabled && (millis() - last_heartbeat > HEARTBEAT_TIMEOUT_MS)) {
        Serial.println("[WARN] Heartbeat timeout - stopping mining");
        mining_enabled = false;
        // Stop hashing task
    }
    // Get job if available
    if (i2c_slave->hasNewJob())
    {
        JobI2cRequest raw_job = i2c_slave->getCurrentJob();

        JobRequest m_job;
        m_job.job_id = raw_job.id;
        m_job.difficulty = raw_job.difficulty;
        m_job.nonce_start = raw_job.nonce_start;
        m_job.nonce_range = NONCES_PER_JOB;
        memcpy(m_job.version, raw_job.buffer, 4);
        memcpy(m_job.prev_block_hash, raw_job.buffer + 4, 32);
        memcpy(m_job.merkle_root, raw_job.buffer + 36, 32);
        memcpy(m_job.ntime, raw_job.buffer + 68, 4);
        memcpy(m_job.nbits, raw_job.buffer + 72, 4);

        Serial.printf("[JOB] ID=%d, nonces 0x%08X-0x%08X\n",
                      m_job.job_id,
                      m_job.nonce_start,
                      m_job.nonce_start + m_job.nonce_range - 1);

        miner.setNewJob(m_job);
        i2c_slave->clearNewJob();
    }

    // Check for found shares
    uint32_t found_nonce = miner.getFoundNonce();
    if (found_nonce != 0xFFFFFFFF)
    {
        g_found_nonce = found_nonce;
        Serial.printf("[SHARE] Found: 0x%08X\n", found_nonce);
    }

    // Report hashes to I2C master
    uint32_t hashes = miner.getAndResetHashes();
    i2c_slave->addHashes(hashes);
    i2c_slave->setFoundNonce(g_found_nonce);

    // Update hashrate calculation
    miner.updateHashrate();

    // Update LCD display
#ifdef LCD
    DisplayStats stats = miner.getDisplayStats();
    display.updateStats(stats);
    display.handleButtons();
#endif

    // Debug output
    static uint32_t last_debug = 0;
    if (millis() - last_debug >= 5000)
    {
        double hr = miner.getHashrate();
        Serial.printf("[DEBUG] Hashrate: %.1f KH/s | Total: %lu | Session: %lu\n",
                      hr / 1000.0, 
                      miner.getTotalHashes(),
                      miner.getHashes());
        last_debug = millis();
    }

    delay(10);
}
