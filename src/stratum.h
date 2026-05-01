#ifndef STRATUM_API_H
#define STRATUM_API_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#define MAX_MERKLE_BRANCHES     32
#define HASH_SIZE               32
#define COINBASE_SIZE           100
#define COINBASE2_SIZE          128
#define BUFFER_JSON_DOC         4096
#define BUFFER                  1024
#define MINIMUM_ACCEPTABLE_DIFFICULTY 0.0001f

struct mining_subscribe {
    String      sub_details;
    String      extranonce1;
    int         extranonce2_size;
    String      extranonce2;
    const char* wName;
    
    mining_subscribe() : extranonce2_size(0), wName("worker") {}
};

struct mining_job {
    String      job_id;
    String      prev_block_hash;
    String      coinb1;
    String      coinb2;
    size_t      n_merkle_branches;
    uint8_t     merkle_branches[MAX_MERKLE_BRANCHES * 32];
    String      version;
    String      nbits;
    String      ntime;
    bool        clean_jobs;
    uint8_t     header_bytes[76];  // Added for block header
    
    mining_job() : n_merkle_branches(0), clean_jobs(false) {
        memset(merkle_branches, 0, sizeof(merkle_branches));
        memset(header_bytes, 0, sizeof(header_bytes));
    }
};

typedef enum {
    STRATUM_SUCCESS = 0,
    STRATUM_UNKNOWN,
    STRATUM_PARSE_ERROR,
    MINING_NOTIFY,
    MINING_SET_DIFFICULTY
} stratum_method;

unsigned long getNextId(unsigned long id);
bool tx_mining_subscribe(WiFiClient& client, mining_subscribe& mSubscribe);
bool parse_mining_subscribe(String line, mining_subscribe& mSubscribe);
bool tx_mining_auth(WiFiClient& client, const char* user, const char* pass);
stratum_method parse_mining_method(String line);
bool parse_mining_notify(String line, mining_job& mJob);
bool tx_mining_submit(WiFiClient& client, mining_subscribe& mWorker, 
                      mining_job& mJob, unsigned long nonce, unsigned long &submit_id);
bool parse_mining_set_difficulty(String line, float& difficulty);
bool tx_suggest_difficulty(WiFiClient& client, float difficulty);
unsigned long parse_extract_id(const String &line);

#endif
