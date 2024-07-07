

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <fpm.h>
#include <Adafruit_Fingerprint.h>
#include <LiquidCrystal_I2C.h>

using namespace websockets;
HardwareSerial fserial(1);
/* WiFi credentials */
const char* ssid = "SLT-4G-E047D4"; // should be changed with network change
const char* password = "2KK3HXL4S2"; // should be changed with network change
const char* serverVerify = "http://192.168.1.111:5000/api/v1/fingerprints/verify"; // should be changed with network change
const char* serverCreate = "http://192.168.1.111:5000/api/v1/fingerprints/"; // should be changed with network change

FPM finger(&fserial);
FPMSystemParams params;

Adafruit_Fingerprint finger_ad = Adafruit_Fingerprint(&fserial);

/* for convenience */
#define PRINTF_BUF_SZ 60
char printfBuf[PRINTF_BUF_SZ];

/* WebSocket client */
WebsocketsClient webSocket;

/* Current mode flag */
volatile int currentMode = 0; // 0: idle, 1: enroll, 2: verify, 3: empty database

/* LCD configuration */
LiquidCrystal_I2C lcd(0x27, 16, 2); // Set the LCD address to 0x27 for a 16 chars and 2 line display

void setup() {
    lcd.init();                       // Initialize the LCD
    lcd.backlight();                  // Turn on the backlight
    lcd.clear();                      // Clear the LCD screen

    Serial.begin(57600);
    while (!Serial) {
        yield();
    }

#if defined(ARDUINO_ARCH_ESP32)
    fserial.begin(57600, SERIAL_8N1, 16, 17);
#else
    fserial.begin(57600);
#endif

    lcd.print("Connecting...");
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        lcd.print(".");
    }
    lcd.clear();
    lcd.print("Connected");

    lcd.setCursor(0, 1);
    lcd.print("Fingerprint Init");

    if (finger.begin()) {
        finger.readParams(&params);
        lcd.clear();
        lcd.print("FP sensor found!");
        lcd.setCursor(0, 1);
        lcd.print("Capacity: ");
        lcd.print(params.capacity);
    } else {
        lcd.clear();
        lcd.print("FP sensor NOT found");
        while (1) yield();
    }

    // Initialize WebSocket
    webSocket.onMessage(webSocketMessage);
    webSocket.onEvent(webSocketEvent);
    connectWebSocket(); // Connect the WebSocket
}

void webSocketEvent(WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
        lcd.clear();
        lcd.print("System Ready");
        webSocket.send("fp1");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
        lcd.clear();
        lcd.print("WebSocket Disconnected");
        reconnectWebSocket(); // Try to reconnect
    }
}

void webSocketMessage(WebsocketsMessage message) {
    lcd.clear();
    lcd.print("Message: ");
    lcd.print(message.data());
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
        lcd.clear();
        lcd.print("Enroll fingerprint");
        currentMode = 1;
    } else if (strcmp(command, "2") == 0) {
        lcd.clear();
        lcd.print("Verify fingerprint");
        currentMode = 2;
    } else if (strcmp(command, "3") == 0) {
        lcd.clear();
        lcd.print("Empty database");
        currentMode = 3;
    } else {
        lcd.clear();
        lcd.print("Invalid command");
        currentMode = 0;
    }
}

bool emptyDatabase() {
    FPMStatus status = finger.emptyDatabase();

    switch (status) {
        case FPMStatus::OK:
            lcd.clear();
            lcd.print("Database empty");
            break;

        case FPMStatus::DBCLEARFAIL:
            lcd.clear();
            lcd.print("DB clear fail");
            return false;

        default:
            snprintf(printfBuf, PRINTF_BUF_SZ, "Error 0x%X", static_cast<uint16_t>(status));
            lcd.clear();
            lcd.print(printfBuf);
            return false;
    }

    return true;
}

bool verify() {
    uint16_t fid = 0;
    bool matchFound = false;
    FPMStatus status;

    lcd.clear();
    lcd.print("Place finger");

    do {
        status = finger.getImage();

        switch (status) {
            case FPMStatus::OK:
                lcd.clear();
                lcd.print("Image taken");
                break;

            case FPMStatus::NOFINGER:
                lcd.clear();
                lcd.print(".");
                break;

            default:
                snprintf(printfBuf, PRINTF_BUF_SZ, "Error 0x%X", static_cast<uint16_t>(status));
                lcd.clear();
                lcd.print(printfBuf);
                break;
        }

        yield();
    } while (status != FPMStatus::OK && currentMode == 2);

    if (currentMode != 2) return false; // Exit if mode changed

    status = finger.image2Tz(1);
    switch (status) {
        case FPMStatus::OK:
            lcd.clear();
            lcd.print("Image converted");
            break;

        default:
            snprintf(printfBuf, PRINTF_BUF_SZ, "Error 0x%X", static_cast<uint16_t>(status));
            lcd.clear();
            lcd.print(printfBuf);
            return false;
    }

    lcd.clear();
    lcd.print("Remove finger");
    delay(1000);
    do {
        status = finger.getImage();
        delay(200);
    } while (status != FPMStatus::NOFINGER);

    while (fid <= 10 && !matchFound && currentMode == 2) {
        matchFound = innerCheck(fid);
        if (matchFound) {
            lcd.clear();
            lcd.print("Match found ID: ");
            lcd.print(fid);
            apireqverify(fid);
        }
        fid++;
    }

    if (!matchFound && currentMode == 2) {
        lcd.clear();
        lcd.print("No match found");
    }

    return true;
}

bool innerCheck(uint16_t fid) {
    FPMStatus status = finger.loadTemplate(fid, 2);

    switch (status) {
        case FPMStatus::OK:
            lcd.clear();
            lcd.print("Template ");
            lcd.print(fid);
            lcd.print(" loaded");
            break;

        case FPMStatus::DBREADFAIL:
            lcd.clear();
            lcd.print("Invalid template");
            return false;

        default:
            snprintf_P(printfBuf, PRINTF_BUF_SZ, PSTR("Error 0x%X"), static_cast<uint16_t>(status));
            lcd.clear();
            lcd.print(printfBuf);
            return false;
    }

    uint16_t score;
    status = finger.matchTemplatePair(&score);

    switch (status) {
        case FPMStatus::OK:
            snprintf(printfBuf, PRINTF_BUF_SZ, "Match! Confidence: %u", score);
            lcd.clear();
            lcd.print(printfBuf);
            return true;

        case FPMStatus::NOMATCH:
            lcd.clear();
            lcd.print("No match");
            return false;

        default:
            snprintf(printfBuf, PRINTF_BUF_SZ, "Error 0x%X", static_cast<uint16_t>(status));
            lcd.clear();
            lcd.print(printfBuf);
            return false;
    }
}

bool enroll() {
    lcd.clear();
    lcd.print("Searching slot");

    int16_t fid;
    if (getFreeId(&fid)) {
        enrollFinger(fid);
    } else {
        lcd.clear();
        lcd.print("No free slot");
    }

    return true;
}

bool getFreeId(int16_t * fid) {
    for (int page = 0; page < (params.capacity / FPM_TEMPLATES_PER_PAGE) + 1; page++) {
        FPMStatus status = finger.getFreeIndex(page, fid);

        switch (status) {
            case FPMStatus::OK:
                if (*fid != -1) {
                    lcd.clear();
                    lcd.print("Free slot at ID ");
                    lcd.print(*fid);
                    return true;
                }
                break;

            default:
                snprintf(printfBuf, PRINTF_BUF_SZ, "Error 0x%X", static_cast<uint16_t>(status));
                lcd.clear();
                lcd.print(printfBuf);
                return false;
        }

        yield();
    }

    lcd.clear();
    lcd.print("No free slots");
    return false;
}

bool enrollFinger(int16_t id) {
    int p = -1;
    lcd.clear();
    lcd.print("Enroll as ID ");
    lcd.print(id);
    while (p != FINGERPRINT_OK) {
        p = finger_ad.getImage();
        switch (p) {
            case FINGERPRINT_OK:
                lcd.clear();
                lcd.print("Image taken");
                break;
            case FINGERPRINT_NOFINGER:
                lcd.clear();
                lcd.print(".");
                break;
            case FINGERPRINT_PACKETRECIEVEERR:
                lcd.clear();
                lcd.print("Comm error");
                break;
            case FINGERPRINT_IMAGEFAIL:
                lcd.clear();
                lcd.print("Imaging error");
                break;
            default:
                lcd.clear();
                lcd.print("Unknown error");
                break;
        }
    }

    // OK success!

    p = finger_ad.image2Tz(1);
    switch (p) {
        case FINGERPRINT_OK:
            lcd.clear();
            lcd.print("Image converted");
            break;
        case FINGERPRINT_IMAGEMESS:
            lcd.clear();
            lcd.print("Image messy");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            lcd.clear();
            lcd.print("Comm error");
            return p;
        case FINGERPRINT_FEATUREFAIL:
            lcd.clear();
            lcd.print("Feat fail");
            return p;
        case FINGERPRINT_INVALIDIMAGE:
            lcd.clear();
            lcd.print("Invalid img");
            return p;
        default:
            lcd.clear();
            lcd.print("Unknown error");
            return p;
    }

    lcd.clear();
    lcd.print("Remove finger");
    delay(2000);
    p = 0;
    while (p != FINGERPRINT_NOFINGER) {
        p = finger_ad.getImage();
    }
    lcd.clear();
    lcd.print("ID ");
    lcd.print(id);
    p = -1;
    lcd.print("Place same finger");
    while (p != FINGERPRINT_OK) {
        p = finger_ad.getImage();
        switch (p) {
            case FINGERPRINT_OK:
                lcd.clear();
                lcd.print("Image taken");
                break;
            case FINGERPRINT_NOFINGER:
                lcd.clear();
                lcd.print(".");
                break;
            case FINGERPRINT_PACKETRECIEVEERR:
                lcd.clear();
                lcd.print("Comm error");
                break;
            case FINGERPRINT_IMAGEFAIL:
                lcd.clear();
                lcd.print("Imaging error");
                break;
            default:
                lcd.clear();
                lcd.print("Unknown error");
                break;
        }
    }

    // OK success!

    p = finger_ad.image2Tz(2);
    switch (p) {
        case FINGERPRINT_OK:
            lcd.clear();
            lcd.print("Image converted");
            break;
        case FINGERPRINT_IMAGEMESS:
            lcd.clear();
            lcd.print("Image messy");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            lcd.clear();
            lcd.print("Comm error");
            return p;
        case FINGERPRINT_FEATUREFAIL:
            lcd.clear();
            lcd.print("Feat fail");
            return p;
        case FINGERPRINT_INVALIDIMAGE:
            lcd.clear();
            lcd.print("Invalid img");
            return p;
        default:
            lcd.clear();
            lcd.print("Unknown error");
            return p;
    }

    // OK converted!
    lcd.clear();
    lcd.print("Creating model");

    p = finger_ad.createModel();
    if (p == FINGERPRINT_OK) {
        lcd.clear();
        lcd.print("Prints matched!");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
        lcd.clear();
        lcd.print("Comm error");
        return p;
    } else if (p == FINGERPRINT_ENROLLMISMATCH) {
        lcd.clear();
        lcd.print("Not matched");
        return p;
    } else {
        lcd.clear();
        lcd.print("Unknown error");
        return p;
    }

    lcd.clear();
    lcd.print("ID ");
    lcd.print(id);
    if (!apireqcreate(id)) {
        lcd.clear();
        lcd.print("API req fail");
        return false;
    }
    p = finger_ad.storeModel(id);
    if (p == FINGERPRINT_OK) {
        lcd.clear();
        lcd.print("Stored at ID!");
        lcd.print(id);
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
        lcd.clear();
        lcd.print("Comm error");
        return p;
    } else if (p == FINGERPRINT_BADLOCATION) {
        lcd.clear();
        lcd.print("Bad location");
        return p;
    } else if (p == FINGERPRINT_FLASHERR) {
        lcd.clear();
        lcd.print("Flash error");
        return p;
    } else {
        lcd.clear();
        lcd.print("Unknown error");
        return p;
    }

    return true;
}

bool apireqcreate(uint16_t fingerprint_id) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverCreate);
        http.addHeader("Content-Type", "application/json");

        // Create JSON object
        StaticJsonDocument<200> jsonDoc;
        jsonDoc["fingerprint_id"] = fingerprint_id;

        // Serialize JSON object to string
        String jsonString;
        serializeJson(jsonDoc, jsonString);

        // Send POST request
        lcd.clear();
        // lcd.print("JSON Payload:");
        // lcd.setCursor(0, 1);
        // lcd.print(jsonString);
        int httpResponseCode = http.POST(jsonString);
        lcd.clear();
        lcd.print("HTTP Resp code:");
        lcd.setCursor(0, 1);
        lcd.print(httpResponseCode);
        String response = http.getString();
        if (httpResponseCode == 201) {
            lcd.clear();
            lcd.print("HTTP Resp code:");
            lcd.setCursor(0, 1);
            lcd.print(httpResponseCode);
            lcd.clear();
            lcd.print("Response: ");
            lcd.setCursor(0, 1);
            lcd.print(response);

            // Parse the JSON response
            StaticJsonDocument<200> responseDoc;
            deserializeJson(responseDoc, response);
            // const char* student_id = responseDoc["student_id"];
            // lcd.clear();
            // lcd.print("Student ID: ");
            // lcd.setCursor(0, 1);
            // lcd.print(student_id);
            return true;
        } else {
            lcd.clear();
            lcd.print("HTTP Req Error:");
            lcd.setCursor(0, 1);
            lcd.print(response);
            lcd.print(http.errorToString(httpResponseCode));
            return false;
        }

        http.end(); // Free resources
    } else {
        lcd.clear();
        lcd.print("WiFi Disconnected");
    }
    return false;
}

void apireqverify(uint16_t fingerprint_id) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverVerify);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<200> jsonDoc;
        jsonDoc["fingerprint_id"] = fingerprint_id;

        String jsonString;
        serializeJson(jsonDoc, jsonString);
        // lcd.clear();
        // lcd.print("JSON Payload:");
        // lcd.setCursor(0, 1);
        // lcd.print(jsonString);
        int httpResponseCode = http.POST(jsonString);
        lcd.clear();
        lcd.print("HTTP Resp code:");
        lcd.setCursor(0, 1);
        lcd.print(httpResponseCode);

        if (httpResponseCode > 0) {
            String response = http.getString();
            lcd.clear();
            // lcd.print("Response:");
            // lcd.setCursor(0, 1);
            // lcd.print(response);

            StaticJsonDocument<200> responseDoc;
            deserializeJson(responseDoc, response);
            const int pcId = responseDoc["pcId"];
            String pcLabel = getPcLabel(pcId);
            
            lcd.clear();
            lcd.print("Your PC:");
            lcd.setCursor(0, 1);
            lcd.print(pcLabel);
        } else {
            lcd.clear();
            lcd.print("HTTP Req Error:");
            lcd.setCursor(0, 1);
            lcd.print(http.errorToString(httpResponseCode));
        }

        http.end();
    } else {
        lcd.clear();
        lcd.print("WiFi Disconnected");
    }
}

void connectWebSocket() {
    if (webSocket.connect("ws://192.168.1.111:5000")) { // should be changed with network change
        lcd.clear();
        lcd.print("System Ready");
        webSocket.send("fp1");
    } else {
        lcd.clear();
        lcd.print(" Connect Fail");
    }
}

String getPcLabel(int pcId) {
    int rowIndex = (pcId - 1) / 5; // Each row has 5 PCs
    int colIndex = (pcId - 1) % 5; // Column index within the range 0 to 4
    char rowLabel = 'A' + rowIndex; // Row label starting from 'A'
    int colLabel = colIndex + 1; // Column label from 1 to 5

    return String(rowLabel) + String(colLabel);
}

void reconnectWebSocket() {
    int retryCount = 0;
    const int maxRetries = 5;

    while (retryCount < maxRetries && !webSocket.available()) {
        lcd.clear();
        lcd.print("WS Reconnect attempt ");
        lcd.setCursor(0, 1);
        lcd.print(retryCount + 1);
        connectWebSocket();
        delay(5000); // Wait for 5 seconds before retrying
        retryCount++;
    }

    if (webSocket.available()) {
        lcd.clear();
        lcd.print("WebSocket Reconnected");
    } else {
        lcd.clear();
        lcd.print("WS Reconnect Failed");
    }
}
