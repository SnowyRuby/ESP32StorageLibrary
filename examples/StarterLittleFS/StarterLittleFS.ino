#include <ESP32StorageLibrary.h>

// Starter sketch for an ESP32 project that stores data in internal flash
// using LittleFS. Mounting does not format the filesystem.

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!StorageLibrary.LittleFS.begin()) {
    Serial.print("LittleFS mount failed: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
    return;
  }

  Serial.println("LittleFS ready. Add your project setup code here.");
}

void loop() {
  // Add your project code here.
}
