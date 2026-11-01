#include <ESP32StorageLibrary.h>

// Starter sketch for an ESP32 project that uses both an SPI SD card and
// internal flash with LittleFS. Change the SD pins to match your wiring.
// LittleFS mounting does not format its filesystem.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;

bool sdReady = false;
bool littleFsReady = false;

void setup() {
  Serial.begin(115200);
  delay(500);

  sdReady = StorageLibrary.SD.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
  if (sdReady)
    Serial.println("SD card ready.");
  else {
    Serial.print("SD card mount failed: ");
    Serial.println(StorageLibrary.SD.lastError());
  }

  littleFsReady = StorageLibrary.LittleFS.begin();
  if (littleFsReady)
    Serial.println("LittleFS ready.");
  else {
    Serial.print("LittleFS mount failed: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
  }

  Serial.println("Add your project setup code here.");
}

void loop() {
  // Add your project code here. Check sdReady and littleFsReady first.
}
