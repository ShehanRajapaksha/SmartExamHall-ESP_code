void setup()
{
  Serial.begin(57600);
  while (!Serial);
  
  #if defined(ARDUINO_ARCH_ESP32)
    fserial.begin(57600, SERIAL_8N1, 16, 17);
  #else
    fserial.begin(57600);
  #endif

  Serial.println("Initializing fingerprint sensor...");

  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor initialized successfully.");
  } else {
    Serial.println("Failed to initialize fingerprint sensor.");
    while (1);
  }

  for (int i = 1; i <= numTemplates; i++) {
    if (uploadFingerprintTemplate(i, templates[i - 1])) {
      Serial.print("Template "); Serial.print(i); Serial.println(" uploaded.");
    } else {
      Serial.print("Failed to upload template "); Serial.println(i);
    }
  }
}

bool uploadFingerprintTemplate(uint16_t id, uint8_t* templateData) {
  if (finger.createModel(templateData) != FINGERPRINT_OK) {
    Serial.print("Failed to create model for ID "); Serial.println(id);
    return false;
  }

  if (finger.storeModel(id) != FINGERPRINT_OK) {
    Serial.print("Failed to store model for ID "); Serial.println(id);
    return false;
  }

  return true;
}

void loop() {
  // Do nothing
}
