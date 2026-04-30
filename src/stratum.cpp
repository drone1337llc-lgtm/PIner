#include "stratum.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#define CURRENT_VERSION "1.0"

JsonDocument doc;
unsigned long id = 1;

// Get next JSON RPC Id
unsigned long getNextId(unsigned long id)
{
    if (id == ULONG_MAX)
    {
        id = 1;
        return id;
    }
    return ++id;
}

// Verify Payload doesn't have zero length
bool verifyPayload(String *line)
{
    if (line->length() == 0)
        return false;
    line->trim();
    if (line->isEmpty())
        return false;
    return true;
}

// Enhanced checkError function that ignores false positive difficulty errors
bool checkError(const JsonDocument &jsonDoc)
{
    // Only return true for actual errors, not difficulty complaints
    if (jsonDoc["error"])
    {
        // Check if it's an array with error code
        // FIX: Use JsonArrayConst instead of JsonArray
        if (jsonDoc["error"].is<JsonArrayConst>())
        {
            JsonArrayConst errorArray = jsonDoc["error"].as<JsonArrayConst>(); // Use JsonArrayConst
            if (errorArray.size() >= 3)
            {
                int errorCode = errorArray[0].as<int>();                     // Add .as<int>()
                const char *errorMessage = errorArray[1].as<const char *>(); // Add .as<const char*>()

                // Ignore "Difficulty too low" errors since we validate ourselves
                if (errorCode == 23 && strcmp(errorMessage, "Difficulty too low") == 0)
                {
                    Serial.println("Ignoring false positive 'Difficulty too low' error");
                    return false; // Not a real error for us
                }

                // Handle other errors normally
                Serial.printf("STRATUM ERROR occurred: %d | reason: %s \n", errorCode, errorMessage);
                return true;
            }
        }
        else if (!jsonDoc["error"].isNull())
        {
            // Handle other error formats
            Serial.printf("STRATUM ERROR occurred: %s\n", jsonDoc["error"].as<String>().c_str());
            return true;
        }
    }
    return false;
}

// STEP 1: Pool server connection (SUBSCRIBE)
bool tx_mining_subscribe(WiFiClient &client, mining_subscribe &mSubscribe)
{
    char payload[BUFFER] = {0};

    // Subscribe
    id = 1; // Initialize id messages
#ifndef HAN
    sprintf(payload, "{\"id\": %u, \"method\": \"mining.subscribe\", \"params\": [\"NerdMinerV2/%s\"]}\n", id, CURRENT_VERSION);
#else
    sprintf(payload, "{\"id\": %u, \"method\": \"mining.subscribe\", \"params\": [\"HAN_SOLOminer/%s\"]}\n", id, CURRENT_VERSION);
#endif

    Serial.printf("[WORKER] ==> Mining subscribe\n");
    Serial.print("  Sending  : ");
    Serial.println(payload);
    client.print(payload);

    delay(200); // Small delay

    String line = client.readStringUntil('\n');
    if (!parse_mining_subscribe(line, mSubscribe))
        return false;

    Serial.print("    sub_details: ");
    Serial.println(mSubscribe.sub_details);
    Serial.print("    extranonce1: ");
    Serial.println(mSubscribe.extranonce1);
    Serial.print("    extranonce2_size: ");
    Serial.println(mSubscribe.extranonce2_size);

    if (mSubscribe.extranonce1.length() == 0)
    {
        Serial.printf("[WORKER] >>>>>>>>> Work aborted\n");
        Serial.printf("extranonce1 length: %u \n", mSubscribe.extranonce1.length());
        return false;
    }
    return true;
}

bool parse_mining_subscribe(String line, mining_subscribe &mSubscribe)
{
    if (!verifyPayload(&line))
        return false;
    Serial.print("  Receiving: ");
    Serial.println(line);

    // Clear document before parsing new data
    doc.clear();

    // Parse JSON with proper error handling
    DeserializationError error = deserializeJson(doc, line);
    if (error)
    {
        Serial.printf("JSON Parse Error: %s\n", error.c_str());
        return false;
    }

    if (checkError(doc))
    {
        return false;
    }

    // Proper way to access "result"
    JsonVariantConst result_field = doc["result"]; // JsonVariantConst
    if (result_field.isNull())
    {
        Serial.println("No result field found");
        return false;
    }

    // Check if result is an array and convert safely
    if (!result_field.is<JsonArrayConst>())
    {
        Serial.println("Result is not an array");
        return false;
    }

    JsonArrayConst result = result_field.as<JsonArrayConst>();

    if (result.size() >= 3)
    {
        // Handle sub_details (first element) - FIXED
        JsonVariantConst subDetailsVar = result[0]; // JsonVariantConst, not JsonVariant
        if (subDetailsVar.is<JsonArrayConst>())
        {
            JsonArrayConst subDetailsArray = subDetailsVar.as<JsonArrayConst>();
            if (subDetailsArray.size() > 0 && subDetailsArray[0].is<JsonArrayConst>())
            {
                JsonArrayConst detailItem = subDetailsArray[0].as<JsonArrayConst>();
                if (detailItem.size() >= 2)
                {
                    mSubscribe.sub_details = detailItem[1].as<String>();
                }
            }
        }

        // Handle extranonce1 (second element)
        if (!result[1].isNull())
        {
            mSubscribe.extranonce1 = result[1].as<String>();
        }

        // Handle extranonce2_size (third element)
        if (!result[2].isNull())
        {
            mSubscribe.extranonce2_size = result[2].as<int>();
        }

        return true;
    }

    return false;
}

// STEP 2: Pool server auth (authorize)
bool tx_mining_auth(WiFiClient &client, const char *user, const char *pass)
{
    char payload[BUFFER] = {0};

    // Authorize
    id = getNextId(id);
    sprintf(payload, "{\"params\": [\"%s\", \"%s\"], \"id\": %u, \"method\": \"mining.authorize\"}\n",
            user, pass, id);

    Serial.printf("[WORKER] ==> Authorize work\n");
    Serial.print("  Sending  : ");
    Serial.println(payload);
    client.print(payload);

    delay(200); // Small delay
    return true;
}

stratum_method parse_mining_method(String line) {
    if (!verifyPayload(&line)) return STRATUM_PARSE_ERROR;
    Serial.print("  Receiving: "); Serial.println(line);
    
    // Clear document before parsing
    doc.clear();
    DeserializationError error = deserializeJson(doc, line);

    if (error) {
        Serial.printf("JSON Parse Error: %s\n", error.c_str());
        return STRATUM_PARSE_ERROR;
    }
    
    if (checkError(doc)) {
        return STRATUM_PARSE_ERROR;
    }

    // Check if method field exists
    JsonVariantConst methodVar = doc["method"];
    if (!methodVar.isNull()) {
        String method = methodVar.as<String>();
        if (method == "mining.notify") {
            return MINING_NOTIFY;
        } else if (method == "mining.set_difficulty") {
            return MINING_SET_DIFFICULTY;
        }
    } else {
        // Check if it's a response (has result or id)
        JsonVariantConst resultVar = doc["result"];
        JsonVariantConst idVar = doc["id"];
        
        if (!resultVar.isNull() || !idVar.isNull()) {
            // Check for errors
            JsonVariantConst errorVar = doc["error"];
            if (errorVar.isNull() || !errorVar) {
                return STRATUM_SUCCESS;
            }
        }
    }

    return STRATUM_UNKNOWN;
}

bool parse_mining_notify(String line, mining_job &mJob)
{
    Serial.println("    Parsing Method [MINING NOTIFY]");
    if (!verifyPayload(&line))
        return false;

    doc.clear();
    DeserializationError error = deserializeJson(doc, line);

    if (error)
    {
        Serial.printf("JSON Parse Error in mining.notify: %s\n", error.c_str());
        return false;
    }

    // Check for errors first
    if (checkError(doc))
    {
        Serial.printf("[WORKER] >>>>>>>>> Work aborted\n");
        return false;
    }

    // Check if params exists - FIXED VERSION
    JsonVariant paramsVar = doc["params"];
    if (paramsVar.isNull())
    {
        Serial.println("No params field found");
        return false;
    }

    JsonArrayConst params = paramsVar.as<JsonArrayConst>();
    if (params.size() >= 9)
    {
        mJob.job_id = params[0].as<String>();
        mJob.prev_block_hash = params[1].as<String>();
        mJob.coinb1 = params[2].as<String>();
        mJob.coinb2 = params[3].as<String>();

        // Handle merkle_branch
        JsonVariantConst merkleVar = params[4];
        if (!merkleVar.isNull() && merkleVar.is<JsonArrayConst>())
        {
            JsonArrayConst merkle_branch_array = merkleVar.as<JsonArrayConst>();
            mJob.n_merkle_branches = 0;

            if (merkle_branch_array.size() <= MAX_MERKLE_BRANCHES)
            {
                mJob.n_merkle_branches = merkle_branch_array.size();
                for (size_t i = 0; i < mJob.n_merkle_branches; i++)
                {
                    if (!merkle_branch_array[i].isNull())
                    {
                        String branch_hex = merkle_branch_array[i].as<String>();
                        // Convert hex string to binary and store
                        for (int j = 0; j < 32 && (j * 2 + 1) < branch_hex.length(); j++)
                        {
                            char byte_str[3] = {branch_hex[j * 2], branch_hex[j * 2 + 1], '\0'};
                            mJob.merkle_branches[i * 32 + j] = (uint8_t)strtoul(byte_str, NULL, 16);
                        }
                    }
                }
            }
        }

        mJob.version = params[5].as<String>();
        mJob.nbits = params[6].as<String>();
        mJob.ntime = params[7].as<String>();
        mJob.clean_jobs = params[8].as<bool>();

        return true;
    }
    return false;
}

bool tx_mining_submit(WiFiClient &client, mining_subscribe mWorker, mining_job mJob, unsigned long nonce, unsigned long &submit_id)
{
    char payload[BUFFER] = {0};

    // Submit
    id = getNextId(id);
    submit_id = id;
    sprintf(payload, "{\"id\":%u,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            id,
            mWorker.wName,
            mJob.job_id.c_str(),
            mWorker.extranonce2.c_str(),
            mJob.ntime.c_str(),
            String(nonce, HEX).c_str());
    Serial.print("  Sending  : ");
    Serial.print(payload);
    client.print(payload);

    return true;
}

bool parse_mining_set_difficulty(String line, float &difficulty)
{
    Serial.println("    Parsing Method [SET DIFFICULTY]");
    if (!verifyPayload(&line))
        return false;

    doc.clear();
    DeserializationError error = deserializeJson(doc, line);

    if (error)
        return false;

    if (doc["params"])
    {
        JsonArrayConst params = doc["params"]; 
        if (params.size() > 0)
        {
            Serial.print("    difficulty: ");
            Serial.println(params[0].as<float>(), 12); // Change to float
            difficulty = params[0].as<float>();        // Change to float
            return true;
        }
    }
    return false;
}

bool tx_suggest_difficulty(WiFiClient &client, double difficulty)
{
    char payload[BUFFER] = {0};

    id = getNextId(id);
    sprintf(payload, "{\"id\":%lu,\"method\":\"mining.suggest_difficulty\",\"params\":[%.10g]}\n", id, difficulty);

    Serial.print("  Sending  : ");
    Serial.print(payload);
    return client.print(payload);
}

unsigned long parse_extract_id(const String &line)
{
    doc.clear();
    DeserializationError error = deserializeJson(doc, line);
    if (error)
        return 0;

    if (doc["id"] && !doc["id"].isNull())
    {
        return doc["id"].as<unsigned long>();
    }

    return 0;
}
