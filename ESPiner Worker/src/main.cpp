#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"
#include "config.h"
#include "sha256_optimized.h"

#if CONFIG_FREERTOS_UNICORE
  #define MINING_CORE 0
#else
  #define MINING_CORE 1
#endif

uint8_t my_i2c_addr = 0x00;
volatile bool new_job_available = false;
volatile bool share_found = false;       // Flag for master polling
volatile uint32_t found_nonce = 0;       // Stored result
JobI2cRequest current_job;
SemaphoreHandle_t jobMutex;

uint32_t midstate[8];
uint32_t baked_vals[15];
uint8_t local_header[76];
float local_diff = 0;

void onI2CReceive(int len) {
    if (len == sizeof(JobI2cRequest)) {
        static JobI2cRequest temp;
        Wire.readBytes((uint8_t*)&temp, sizeof(JobI2cRequest));
        
        // Structure check: We verify CRC starting from the 'id' field
        if (crc8_compute(&temp.id, sizeof(temp) - 2) == temp.crc) {
            if (xSemaphoreTakeFromISR(jobMutex, NULL)) {
                memcpy(&current_job, &temp, sizeof(JobI2cRequest));
                new_job_available = true;
                share_found = false; 
                xSemaphoreGiveFromISR(jobMutex, NULL);
            }
        } else {
            Serial.println("Slave: CRC Mismatch!");
        }
    }
}

// Handler for Master polling (requestFrom)
void onI2CRequest() {
    uint8_t response[5];
    response[0] = share_found ? 0x02 : 0x01;
    // Note: ensure Big Endian or Little Endian matches Master expectations
    memcpy(&response[1], (void*)&found_nonce, 4);
    Wire.write(response, 5);
    if (share_found) share_found = false;
}

void miningTask(void* pv) {
    uint8_t hash_result[32];
    uint32_t nonce = 0;
    
    // Autonomous Nonce: Each slave gets a ~536 million nonce search space
    uint32_t nonce_start = (my_i2c_addr - I2C_BASE_ADDRESS) * 0x1FFFFFFF; 
    nonce = nonce_start;

    while (1) {
        if (new_job_available) {
            if (xSemaphoreTake(jobMutex, pdMS_TO_TICKS(10))) {
                memcpy(local_header, current_job.buffer, 76);
                local_diff = current_job.difficulty;
                new_job_available = false;
                
                sha256_midstate(midstate, local_header);
                sha256_bake(midstate, local_header + 64, baked_vals);
                
                nonce = nonce_start; 
                xSemaphoreGive(jobMutex);
                Serial.println("Miner: Job Loaded (Autonomous)");
            }
        }

        for (int i = 0; i < HASH_BATCH_SIZE; i++) {
            nonce++;
            local_header[72] = (nonce >> 24) & 0xFF;
            local_header[73] = (nonce >> 16) & 0xFF;
            local_header[74] = (nonce >> 8) & 0xFF;
            local_header[75] = nonce & 0xFF;

            if (sha256_double_baked(midstate, local_header + 64, baked_vals, hash_result)) {
                found_nonce = nonce;
                share_found = true;
                Serial.printf("!!! Share Found: %08X\n", nonce);
            }
        }
        vTaskDelay(1); 
    }
}

void setup() {
    Serial.begin(115200);
    jobMutex = xSemaphoreCreateMutex();
    analogReadResolution(12);
    my_i2c_addr = 0x10 + (analogRead(34) / 512);
    
    Wire.begin(my_i2c_addr); 
    Wire.onReceive(onI2CReceive);
    Wire.onRequest(onI2CRequest); // Attach the responder

    #ifdef esp32c3
    xTaskCreatePinnedToCore(miningTask, "Miner", 8192, NULL, 1, NULL, MINING_CORE);
    #else
    xTaskCreatePinnedToCore(miningTask, "Miner", 8192, NULL, 1, NULL, MINING_CORE);
    #endif
    Serial.printf("Slave 0x%02X initialized on Core %d\n", my_i2c_addr, MINING_CORE);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}