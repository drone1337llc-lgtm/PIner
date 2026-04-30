#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "miner_core.h"
#include "i2c_slave.h"

MinerCore miner0, miner1;
I2CSlave *slave;

// Core 1 Mining Task
void miningTaskCore1(void *pv)
{
    while (1)
    {
        if (slave->m_mining_enabled && miner1.isMining())
        {
            miner1.runBatch(miner1.getJobNonceStart() + 0x10000, miner1.getJobNonceStart() + 0x20000);
        }
        vTaskDelay(1);
    }
}

void setup()
{
    Serial.begin(115200);

#ifdef LCD
    display.begin();
    display.showBootScreen();
#endif

    // 1. Detect I2C Address from Ladder
    int adc = analogRead(LADDER_PIN);
    uint8_t myAddr = I2C_BASE_ADDRESS + (adc / 512); // Adjust based on your resistor ladder

    // 2. Start I2C Slave
    slave = new I2CSlave(myAddr);
    slave->begin(myAddr);

    // 3. Init Miner Cores
    miner0.begin();
    miner0.setCoreId(0);
    miner1.begin();
    miner1.setCoreId(1);

    xTaskCreatePinnedToCore(miningTaskCore1, "Miner1", 8192, NULL, 1, NULL, 1);

    Serial.printf("Slave Started at 0x%02X\n", myAddr);
}

void loop()
{
    // 1. Check for New Jobs
    if (slave->hasNewJob())
    {
        JobI2cRequest raw;
        slave->getJob(raw);

        JobRequest job;
        job.nonce_start = (uint32_t)raw.nonce_start_byte << 16;
        job.nonce_range = 0x10000;
        job.difficulty = raw.difficulty;
        memcpy(job.header_bytes, raw.buffer, 76);

        miner0.setNewJob(job);
        miner1.setNewJob(job);
        slave->clearNewJob();
    }

    // 2. Core 0 Mining (Shares loop with I2C housekeeping)
    if (slave->m_mining_enabled && miner0.isMining())
    {
        miner0.runBatch(miner0.getJobNonceStart(), miner0.getJobNonceStart() + 0x10000);
    }

    // 3. Report Stats
    uint32_t h = miner0.getAndResetHashes() + miner1.getAndResetHashes();
    slave->addHashes(h);

    uint32_t n = miner0.getFoundNonce();
    if (n == 0xFFFFFFFF)
        n = miner1.getFoundNonce();
    if (n != 0xFFFFFFFF)
    {
        slave->setFoundNonce(n);
        miner0.clearFoundNonce();
        miner1.clearFoundNonce();
    }

#ifdef LCD
    display.updateStats(stats); // You'll need to populate the 'stats' struct
#endif

    vTaskDelay(1); // Yield to IDLE to prevent WDT trigger
}