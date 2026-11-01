#include <SPI.h>
#include <ESP32StorageLibrary.h>

// Example pins for a classic ESP32 Dev Module. Change these to match your wiring.
constexpr uint8_t SD_CS_PIN = 5;
constexpr int8_t SD_SCK_PIN = 18;
constexpr int8_t SD_MISO_PIN = 19;
constexpr int8_t SD_MOSI_PIN = 23;
constexpr uint32_t SD_FREQUENCY = 4000000;

SPIClass sharedSPI(VSPI);

void setup() {
  Serial.begin(115200);

  // Initialize this bus here, or replace sharedSPI below with the already
  // initialized SPIClass instance used by your display, e.g. Screen.getSPIinstance().
  sharedSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  if (!StorageLibrary.beginStorage(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_FREQUENCY, false, &sharedSPI)) {
    Serial.print("Storage initialization failed: ");
    Serial.println(StorageLibrary.lastError());
    return;
  }

  if (!StorageLibrary.writeWithFallback("shared SPI example record\n")) {
    Serial.print("Record write failed: ");
    Serial.println(StorageLibrary.lastError());
    return;
  }

  Serial.println("SD/LittleFS storage is ready on the shared SPI bus.");
}

void loop() {
  // Application code can continue to use StorageLibrary.writeWithFallback().
}
