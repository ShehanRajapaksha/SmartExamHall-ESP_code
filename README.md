
# Smart Exam Hall - ESP Code

This repository contains the code for integrating a fingerprint sensor with a React web application and a Flutter mobile application. The project uses an ESP32 microcontroller to handle fingerprint sensor data and communicates with the web and mobile applications via WebSockets and HTTP requests.




## Libraries Used

The following libraries are used in this project:

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <fpm.h>
#include <Adafruit_Fingerprint.h>
#include <LiquidCrystal_I2C.h>
```


## Run Locally

Clone the project

```bash
  git clone https://github.com/ShehanRajapaksha/SmartExamHall-ESP_code.git
```

Go to the project directory

```bash
  cd my-project
```



# Code Overview



The core functionality is implemented in final_code1.ino. It includes:

- WiFi connection setup
- WebSocket initialization and event handling
- Fingerprint sensor initialization and operations (enroll, verify, empty database)
- LCD display updates
    


## Usage/Examples



```cpp
void setup() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    Serial.begin(57600);
    while (!Serial) {
        yield();
    }
    fserial.begin(57600, SERIAL_8N1, 16, 17);
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
    webSocket.onMessage(webSocketMessage);
    webSocket.onEvent(webSocketEvent);
    connectWebSocket();
}
```


# Additional Sensor Codes

This repository also contains additional sensor codes and scripts used during the development of the project. These are located in the *additional-sensors* directory.


# Integration

- React Web App

The React web application communicates with the ESP32 via WebSockets and HTTP requests. The WebSocket connection is used for real-time updates, while HTTP requests handle fingerprint creation and verification.

- Flutter App
  
The Flutter mobile application also integrates with the ESP32 using similar communication methods. Ensure you handle WebSocket connections and HTTP requests properly to interact with the ESP32.
