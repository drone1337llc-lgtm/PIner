#include "stratum.h"
#include <ArduinoJson.h>

#define CURRENT_VERSION "2.0"

static JsonDocument doc;
static unsigned long g_id = 1;

unsigned long getNextId(unsigned long id) {
    if (id >= ULONG_MAX - 1) {
        g_id = 1;
        return g_id;
    }
    g_id++;
    return g_id;
}

bool verifyPayload(String* line) {
    if (line == nullptr || line->length() == 0) return false;
    line->trim();
    return !line->isEmpty();
}

bool checkError(const JsonDocument& jsonDoc) {
    if (jsonDoc["error"]) {
        if (jsonDoc["error"].is<JsonArrayConst>()) {
            JsonArrayConst errorArray = jsonDoc["error"].as<JsonArrayConst>();
            if (errorArray.size() >= 2) {
                int errorCode = errorArray[0].as<int>();
                const char* errorMessage = errorArray[1].as<const char*>();
                
                if (errorCode == 23 && errorMessage != nullptr && 
                    strcmp(errorMessage, "Difficulty too low") == 0) {
                    Serial.println("[Stratum] Ignoring false positive difficulty error");
                    return false;
                }
                
                Serial.printf("[Stratum] ERROR %d: %s\n", errorCode, 
                             errorMessage ? errorMessage : "Unknown");
                return true;
            }
        } else if (!jsonDoc["error"].isNull()) {
            Serial.printf("[Stratum] ERROR: %s\n", 
                         jsonDoc["error"].as<String>().c_str());
            return true;
        }
    }
    return false;
}

bool tx_mining_subscribe(WiFiClient& client, mining_subscribe& mSubscribe) {
    char payload[BUFFER] = {0};
    
    g_id = 1;
    snprintf(payload, BUFFER, 
            "{\"id\": %lu, \"method\": \"mining.subscribe\", \"params\": [\"ESPMiner/%s\"]}\n",
            g_id, CURRENT_VERSION);
    
    Serial.println("[Stratum] Sending subscribe...");
    Serial.print(payload);
    client.print(payload);
    
    delay(500);
    
    String line = client.readStringUntil('\n');
    if (!parse_mining_subscribe(line, mSubscribe)) {
        Serial.println("[Stratum] Subscribe failed");
        return false;
    }
    
    Serial.printf("[Stratum] Subscribed - Extranonce1: %s, Size: %d\n",
                 mSubscribe.extranonce1.c_str(), mSubscribe.extranonce2_size);
    
    if (mSubscribe.extranonce1.length() == 0) {
        Serial.println("[Stratum] Invalid extranonce1");
        return false;
    }
    
    return true;
}

bool parse_mining_subscribe(String line, mining_subscribe& mSubscribe) {
    if (!verifyPayload(&line)) return false;
    
    doc.clear();
    DeserializationError error = deserializeJson(doc, line);
    if (error) {
        Serial.printf("[Stratum] JSON Error: %s\n", error.c_str());
        return false;
    }
    
    if (checkError(doc)) return false;
    
    JsonVariantConst resultVar = doc["result"];
    if (resultVar.isNull() || !resultVar.is<JsonArrayConst>()) {
        Serial.println("[Stratum] No valid result array");
        return false;
    }
    
    JsonArrayConst result = resultVar.as<JsonArrayConst>();
    if (result.size() >= 3) {
        if (result[0].is<JsonArrayConst>()) {
            JsonArrayConst subDetails = result[0].as<JsonArrayConst>();
            if (subDetails.size() >= 2 && subDetails[0].is<JsonArrayConst>()) {
                JsonArrayConst detailItem = subDetails[0].as<JsonArrayConst>();
                if (detailItem.size() >= 2) {
                    mSubscribe.sub_details = detailItem[1].as<String>();
                }
            }
        }
        
        if (!result[1].isNull()) {
            mSubscribe.extranonce1 = result[1].as<String>();
        }
        
        if (!result[2].isNull()) {
            mSubscribe.extranonce2_size = result[2].as<int>();
        }
        
        return true;
    }
    
    return false;
}

bool tx_mining_auth(WiFiClient& client, const char* user, const char* pass) {
    char payload[BUFFER] = {0};
    
    g_id = getNextId(g_id);
    snprintf(payload, BUFFER,
            "{\"id\": %lu, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}\n",
            g_id, user, pass);
    
    Serial.println("[Stratum] Sending authorize...");
    Serial.print(payload);
    client.print(payload);
    
    delay(500);
    return true;
}

stratum_method parse_mining_method(String line) {
    if (!verifyPayload(&line)) return STRATUM_PARSE_ERROR;
    
    doc.clear();
    DeserializationError error = deserializeJson(doc, line);
    if (error) {
        Serial.printf("[Stratum] JSON Error: %s\n", error.c_str());
        return STRATUM_PARSE_ERROR;
    }
    
    if (checkError(doc)) return STRATUM_PARSE_ERROR;
    
    JsonVariantConst methodVar = doc["method"];
    if (!methodVar.isNull()) {
        String method = methodVar.as<String>();
        if (method == "mining.notify") return MINING_NOTIFY;
        if (method == "mining.set_difficulty") return MINING_SET_DIFFICULTY;
    }
    
    JsonVariantConst resultVar = doc["result"];
    JsonVariantConst idVar = doc["id"];
    
    if (!resultVar.isNull() || !idVar.isNull()) {
        JsonVariantConst errorVar = doc["error"];
        if (errorVar.isNull()) return STRATUM_SUCCESS;
    }
    
    return STRATUM_UNKNOWN;
}

bool parse_mining_notify(String line, mining_job& mJob) {
    Serial.println("[Stratum] Parsing mining.notify...");
    
    if (!verifyPayload(&line)) return false;
    
    doc.clear();
    DeserializationError error = deserializeJson(doc, line);
    if (error) {
        Serial.printf("[Stratum] JSON Error: %s\n", error.c_str());
        return false;
    }
    
    if (checkError(doc)) return false;
    
    JsonVariantConst paramsVar = doc["params"];
    if (paramsVar.isNull() || !paramsVar.is<JsonArrayConst>()) {
        Serial.println("[Stratum] No params array");
        return false;
    }
    
    JsonArrayConst params = paramsVar.as<JsonArrayConst>();
    if (params.size() < 8) {
        Serial.printf("[Stratum] Insufficient params: %d\n", params.size());
        return false;
    }
    
    mJob.job_id = params[0].as<String>();
    mJob.prev_block_hash = params[1].as<String>();
    mJob.coinb1 = params[2].as<String>();
    mJob.coinb2 = params[3].as<String>();
    
    if (params[4].is<JsonArrayConst>()) {
        JsonArrayConst merkleArray = params[4].as<JsonArrayConst>();
        mJob.n_merkle_branches = min(merkleArray.size(), (size_t)MAX_MERKLE_BRANCHES);
        
        for (size_t i = 0; i < mJob.n_merkle_branches; i++) {
            String branchHex = merkleArray[i].as<String>();
            for (size_t j = 0; j < 32 && (j * 2 + 1) < branchHex.length(); j++) {
                char byteStr[3] = {branchHex[j * 2], branchHex[j * 2 + 1], '\0'};
                mJob.merkle_branches[i * 32 + j] = (uint8_t)strtoul(byteStr, NULL, 16);
            }
        }
    }
    
    mJob.version = params[5].as<String>();
    mJob.nbits = params[6].as<String>();
    mJob.ntime = params[7].as<String>();
    
    if (params.size() >= 9) {
        mJob.clean_jobs = params[8].as<bool>();
    }
    
    Serial.printf("[Stratum] Job %s parsed successfully\n", mJob.job_id.c_str());
    return true;
}

bool tx_mining_submit(WiFiClient& client, mining_subscribe& mWorker, 
                      mining_job& mJob, unsigned long nonce, unsigned long& submit_id) {
    char payload[BUFFER] = {0};
    
    g_id = getNextId(g_id);
    submit_id = g_id;
    
    char nonceStr[9];
    snprintf(nonceStr, sizeof(nonceStr), "%08lx", nonce);
    
    snprintf(payload, BUFFER,
            "{\"id\":%lu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            g_id, mWorker.wName, mJob.job_id.c_str(), 
            mWorker.extranonce2.c_str(), mJob.ntime.c_str(), nonceStr);
    
    Serial.print("[Stratum] Submitting: ");
    Serial.println(payload);
    client.print(payload);
    
    return true;
}

bool parse_mining_set_difficulty(String line, float& difficulty) {
    Serial.println("[Stratum] Parsing set_difficulty...");
    
    if (!verifyPayload(&line)) return false;
    
    doc.clear();
    DeserializationError error = deserializeJson(doc, line);
    if (error) return false;
    
    JsonVariantConst paramsVar = doc["params"];
    if (paramsVar.is<JsonArrayConst>()) {
        JsonArrayConst params = paramsVar.as<JsonArrayConst>();
        if (params.size() > 0) {
            difficulty = params[0].as<float>();
            Serial.printf("[Stratum] Difficulty set to: %.6f\n", difficulty);
            return true;
        }
    }
    
    return false;
}

bool tx_suggest_difficulty(WiFiClient& client, float difficulty) {
    char payload[BUFFER] = {0};
    
    g_id = getNextId(g_id);
    snprintf(payload, BUFFER,
            "{\"id\":%lu,\"method\":\"mining.suggest_difficulty\",\"params\":[%.6f]}\n",
            g_id, difficulty);
    
    Serial.print("[Stratum] Suggest difficulty: ");
    Serial.println(payload);
    return client.print(payload) > 0;
}

unsigned long parse_extract_id(const String& line) {
    doc.clear();
    DeserializationError error = deserializeJson(doc, line);
    if (error) return 0;
    
    if (doc["id"] && !doc["id"].isNull()) {
        return doc["id"].as<unsigned long>();
    }
    
    return 0;
}
