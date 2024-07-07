#if defined(ARDUINO_ARCH_ESP32)
#include <HardwareSerial.h>
HardwareSerial fserial(1);
#else
#include <SoftwareSerial.h>
SoftwareSerial fserial(16, 17);
#endif

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <fpm.h>
#include <Adafruit_Fingerprint.h>

using namespace websockets;

/* WiFi credentials */
const char* ssid = "SLT_FIBRE"; //should be changed with network change
const char* password = "0775997649Mendis"; //should be changed with network change
const char* serverVerify = "http://192.168.1.5:5000/api/v1/fingerprints/verify"; //should be changed with network change
const char* serverCreate = "http://192.168.1.5:5000/api/v1/fingerprints/"; //should be changed with network change

FPM finger(&fserial);
FPMSystemParams params;

Adafruit_Fingerprint finger_ad = Adafruit_Fingerprint(&fserial);

/* for convenience */
#define PRINTF_BUF_SZ   60
char printfBuf[PRINTF_BUF_SZ];

/* WebSocket client */
WebsocketsClient webSocket;

/* Current mode flag */
volatile int currentMode = 0; // 0: idle, 1: enroll, 2: verify, 3: empty database

void setup() {
    Serial.begin(57600);
    while (!Serial) {
        yield();
    }

#if defined(ARDUINO_ARCH_ESP32)
    fserial.begin(57600, SERIAL_8N1, 16, 17);
#else
    fserial.begin(57600);
#endif

    Serial.print("Connecting to Wi-Fi");
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println(" Connected to Wi-Fi");

    Serial.println("Fingerprint Sensor example");

    if (finger.begin()) {
        finger.readParams(&params);
        Serial.println("Found fingerprint sensor!");
        Serial.print("Capacity: "); Serial.println(params.capacity);
        Serial.print("Packet length: "); Serial.println(FPM::packetLengths[static_cast<uint8_t>(params.packetLen)]);
    } else {
        Serial.println("Did not find fingerprint sensor :(");
        while (1) yield();
    }

    // Initialize WebSocket
    webSocket.onMessage(webSocketMessage);
    webSocket.onEvent(webSocketEvent);
    webSocket.connect("ws://192.168.1.5:5000"); //should be changed with network change
}

void webSocketEvent(WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
        Serial.println("WebSocket Connected, sending initial message");
        webSocket.send("fp1");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
        Serial.println("WebSocket Disconnected");
    }
}

void webSocketMessage(WebsocketsMessage message) {
    Serial.println("Got message: " + message.data());
    handleWebSocketCommand(message.data().c_str());
}

void loop() {
    webSocket.poll();

    switch (currentMode) {
        case 1:
            enroll();
            currentMode = 0;
            break;
        case 2:
            while (currentMode == 2) {
                verify();
                webSocket.poll(); // Allow WebSocket to handle incoming messages
                delay(5000); // Add some delay to prevent rapid looping
            }
            break;
        case 3:
            emptyDatabase();
            currentMode = 0;
            break;
        default:
            break;
    }
}

void handleWebSocketCommand(const char* command) {
    if (strcmp(command, "1") == 0) {
        Serial.println("Enroll fingerprint");
        currentMode = 1;
    } else if (strcmp(command, "2") == 0) {
        Serial.println("Verify fingerprint");
        currentMode = 2;
    } else if (strcmp(command, "3") == 0) {
        Serial.println("Empty database");
        currentMode = 3;
    } else {
        Serial.println("Invalid command");
        currentMode = 0;
    }
}

bool emptyDatabase() {
    FPMStatus status = finger.emptyDatabase();

    switch (status) {
        case FPMStatus::OK:
            snprintf(printfBuf, PRINTF_BUF_SZ, "Database empty.");
            Serial.println(printfBuf);
            break;

        case FPMStatus::DBCLEARFAIL:
            snprintf(printfBuf, PRINTF_BUF_SZ, "Could not clear sensor database.");
            Serial.println(printfBuf);
            return false;

        default:
            snprintf(printfBuf, PRINTF_BUF_SZ, "emptyDatabase(): error 0x%X", static_cast<uint16_t>(status));
            Serial.println(printfBuf);
            return false;
    }

    return true;
}

bool verify() {
    uint16_t fid = 0;
    bool matchFound = false;
    FPMStatus status;

    Serial.println("Place a finger.");
    do {
        status = finger.getImage();

        switch (status) {
            case FPMStatus::OK:
                Serial.println("Image taken");
                break;

            case FPMStatus::NOFINGER:
                Serial.print(".");
                break;

            default:
                snprintf(printfBuf, PRINTF_BUF_SZ, "getImage(): error 0x%X", static_cast<uint16_t>(status));
                Serial.println(printfBuf);
                break;
        }

        yield();
    } while (status != FPMStatus::OK && currentMode == 2);

    if (currentMode != 2) return false; // Exit if mode changed

    status = finger.image2Tz(1);
    switch (status) {
        case FPMStatus::OK:
            Serial.println("Image converted");
            break;

        default:
            snprintf(printfBuf, PRINTF_BUF_SZ, "image2Tz(): error 0x%X", static_cast<uint16_t>(status));
            Serial.println(printfBuf);
            return false;
    }

    Serial.println("Remove finger.");
    delay(1000);
    do {
        status = finger.getImage();
        delay(200);
    } while (status != FPMStatus::NOFINGER);

    while (fid <= 10 && !matchFound && currentMode == 2) {
        matchFound = innerCheck(fid);
        if (matchFound) {
            Serial.print("Match found for ID #"); Serial.println(fid);
            apireqverify(fid);
        }
        fid++;
    }

    if (!matchFound && currentMode == 2) {
        Serial.println("No match found.");
    }

    return true;
}

bool innerCheck(uint16_t fid) {
    FPMStatus status = finger.loadTemplate(fid, 2);

    switch (status) {
        case FPMStatus::OK:
            Serial.print("Template "); Serial.print(fid); Serial.println(" loaded");
            break;

        case FPMStatus::DBREADFAIL:
            Serial.println(F("Invalid template or location"));
            return false;

        default:
            snprintf_P(printfBuf, PRINTF_BUF_SZ, PSTR("loadTemplate(%d): error 0x%X"), fid, static_cast<uint16_t>(status));
            Serial.println(printfBuf);
            return false;
    }

    uint16_t score;
    status = finger.matchTemplatePair(&score);

    switch (status) {
        case FPMStatus::OK:
            snprintf(printfBuf, PRINTF_BUF_SZ, "Both prints are a match! Confidence: %u", score);
            Serial.println(printfBuf);
            return true;

        case FPMStatus::NOMATCH:
            Serial.println("Both prints are NOT a match.");
            return false;

        default:
            snprintf(printfBuf, PRINTF_BUF_SZ, "matchTemplatePair(): error 0x%X", static_cast<uint16_t>(status));
            Serial.println(printfBuf);
            return false;
    }
}

bool enroll() {
    Serial.println("Searching for a free slot to store the template...");

    int16_t fid;
    if (getFreeId(&fid)) {
        enrollFinger(fid);
    } else {
        Serial.println("No free slot/ID in database!");
    }

    return true;
}

bool getFreeId(int16_t * fid) {
    for (int page = 0; page < (params.capacity / FPM_TEMPLATES_PER_PAGE) + 1; page++) {
        FPMStatus status = finger.getFreeIndex(page, fid);

        switch (status) {
            case FPMStatus::OK:
                if (*fid != -1) {
                    Serial.print("Free slot at ID ");
                    Serial.println(*fid);
                    return true;
                }
                break;

            default:
                snprintf(printfBuf, PRINTF_BUF_SZ, "getFreeIndex(%d): error 0x%X", page, static_cast<uint16_t>(status));
                Serial.println(printfBuf);
                return false;
        }

        yield();
    }

    Serial.println("No free slots!");
    return false;
}

bool enrollFinger(int16_t id) {
    int p = -1;
    Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);
    while (p != FINGERPRINT_OK) {
        p = finger_ad.getImage();
        switch (p) {
            case FINGERPRINT_OK:
                Serial.println("Image taken");
                break;
            case FINGERPRINT_NOFINGER:
                Serial.print(".");
                break;
            case FINGERPRINT_PACKETRECIEVEERR:
                Serial.println("Communication error");
                break;
            case FINGERPRINT_IMAGEFAIL:
                Serial.println("Imaging error");
                break;
            default:
                Serial.println("Unknown error");
                break;
        }
    }

    // OK success!

    p = finger_ad.image2Tz(1);
    switch (p) {
        case FINGERPRINT_OK:
            Serial.println("Image converted");
            break;
        case FINGERPRINT_IMAGEMESS:
            Serial.println("Image too messy");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            Serial.println("Communication error");
            return p;
        case FINGERPRINT_FEATUREFAIL:
            Serial.println("Could not find fingerprint features");
            return p;
        case FINGERPRINT_INVALIDIMAGE:
            Serial.println("Could not find fingerprint features");
            return p;
        default:
            Serial.println("Unknown error");
            return p;
    }

    Serial.println("Remove finger");
    delay(2000);
    p = 0;
    while (p != FINGERPRINT_NOFINGER) {
        p = finger_ad.getImage();
    }
    Serial.print("ID "); Serial.println(id);
    p = -1;
    Serial.println("Place same finger again");
    while (p != FINGERPRINT_OK) {
        p = finger_ad.getImage();
        switch (p) {
            case FINGERPRINT_OK:
                Serial.println("Image taken");
                break;
            case FINGERPRINT_NOFINGER:
                Serial.print(".");
                break;
            case FINGERPRINT_PACKETRECIEVEERR:
                Serial.println("Communication error");
                break;
            case FINGERPRINT_IMAGEFAIL:
                Serial.println("Imaging error");
                break;
            default:
                Serial.println("Unknown error");
                break;
        }
    }

    // OK success!

    p = finger_ad.image2Tz(2);
    switch (p) {
        case FINGERPRINT_OK:
            Serial.println("Image converted");
            break;
        case FINGERPRINT_IMAGEMESS:
            Serial.println("Image too messy");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            Serial.println("Communication error");
            return p;
        case FINGERPRINT_FEATUREFAIL:
            Serial.println("Could not find fingerprint features");
            return p;
        case FINGERPRINT_INVALIDIMAGE:
            Serial.println("Could not find fingerprint features");
            return p;
        default:
            Serial.println("Unknown error");
            return p;
    }

    // OK converted!
    Serial.print("Creating model for #"); Serial.println(id);

    p = finger_ad.createModel();
    if (p == FINGERPRINT_OK) {
        Serial.println("Prints matched!");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
        Serial.println("Communication error");
        return p;
    } else if (p == FINGERPRINT_ENROLLMISMATCH) {
        Serial.println("Fingerprints did not match");
        return p;
    } else {
        Serial.println("Unknown error");
        return p;
    }

    Serial.print("ID "); Serial.println(id);
    if (!apireqcreate(id)) {
        Serial.println("API request failed, not storing fingerprint.");
        return false;
    }
    p = finger_ad.storeModel(id);
    if (p == FINGERPRINT_OK) {
        Serial.println("Stored!");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
        Serial.println("Communication error");
        return p;
    } else if (p == FINGERPRINT_BADLOCATION) {
        Serial.println("Could not store in that location");
        return p;
    } else if (p == FINGERPRINT_FLASHERR) {
        Serial.println("Error writing to flash");
        return p;
    } else {
        Serial.println("Unknown error");
        return p;
    }

    return true;
}

bool apireqcreate(uint16_t fingerprint_id) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverCreate);
        http.addHeader("Content-Type", "application/json");
        // http.addHeader("ngrok-skip-browser-warning", "69420");

        // Create JSON object
        StaticJsonDocument<200> jsonDoc;
        jsonDoc["fingerprint_id"] = fingerprint_id;

        // Serialize JSON object to string
        String jsonString;
        serializeJson(jsonDoc, jsonString);

        // Send POST request
        Serial.println("JSON Payload: " + jsonString);
        int httpResponseCode = http.POST(jsonString);
        Serial.println(httpResponseCode);
        String response = http.getString();
        if (httpResponseCode == 201) {
            Serial.println("HTTP Response code: " + String(httpResponseCode));
            Serial.println("Response: " + response);

            // Parse the JSON response
            StaticJsonDocument<200> responseDoc;
            deserializeJson(responseDoc, response);
            const char* student_id = responseDoc["student_id"];
            Serial.println("Student ID: " + String(student_id));
            return true;
        } else {
            Serial.println("Error on HTTP request" + response);
            Serial.println(http.errorToString(httpResponseCode));
            return false;
        }

        http.end(); // Free resources
    } else {
        Serial.println("WiFi Disconnected");
    }
}

void apireqverify(uint16_t fingerprint_id) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverVerify);
        http.addHeader("Content-Type", "application/json");
        // http.addHeader("ngrok-skip-browser-warning", "69420");

        StaticJsonDocument<200> jsonDoc;
        jsonDoc["fingerprint_id"] = fingerprint_id;

        String jsonString;
        serializeJson(jsonDoc, jsonString);
        Serial.println("JSON Payload: " + jsonString);
        int httpResponseCode = http.POST(jsonString);
        Serial.println(httpResponseCode);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println("HTTP Response code: " + String(httpResponseCode));
            Serial.println("Response: " + response);

            StaticJsonDocument<200> responseDoc;
            deserializeJson(responseDoc, response);
            const char* student_id = responseDoc["student_id"];
            Serial.println("Student ID: " + String(student_id));
        } else {
            Serial.println("Error on HTTP request");
            Serial.println(http.errorToString(httpResponseCode));
        }

        http.end();
    } else {
        Serial.println("WiFi Disconnected");
    }
}
