#include <ESP32StorageLibrary.h>

// Starter sketch for an ESP32 project that stores data on an SPI SD card.
// Change these pins to match your SD card wiring.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!StorageLibrary.SD.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN)) {
    Serial.print("SD card mount failed: ");
    Serial.println(StorageLibrary.SD.lastError());
    return;
  }

  Serial.println("SD card ready. Add your project setup code here.");
}

void loop() {
  // Add your project code here.
}
