#include <ESP32StorageLibrary.h>

// Demonstrates the reusable library API: beginStorage(),
// writeWithFallback(), and syncBackup(). It never formats or wipes storage.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;
static const uint32_t RECORD_INTERVAL_MS = 5000;

static uint32_t lastRecordAt = 0;
static uint32_t recordNumber = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32StorageLibrary SD + LittleFS fallback example");
  Serial.println("Remove SD to queue records in LittleFS; reinsert it to transfer them back.");
  Serial.println("No format or whole-card wipe is performed.");

  if (!StorageLibrary.beginStorage(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN))
    Serial.println("ERROR: Neither SD nor LittleFS is available.");

  if (StorageLibrary.isLittleFSReady())
    Serial.println("LittleFS ready.");
  else {
    Serial.print("LittleFS unavailable: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
  }
  if (StorageLibrary.isSDReady())
    Serial.println("SD card ready.");
  else {
    Serial.print("SD unavailable: ");
    Serial.println(StorageLibrary.SD.lastError());
  }
}

void loop() {
  delay(10);  // Let the ESP32 idle task run between checks.
  uint32_t now = millis();
  if ((uint32_t)(now - lastRecordAt) < RECORD_INTERVAL_MS)
    return;
  lastRecordAt = now;

  bool sdWasReady = StorageLibrary.isSDReady();
  if (sdWasReady) {
    if (!StorageLibrary.syncBackup()) {
      Serial.print("Backup sync pending: ");
      Serial.println(StorageLibrary.lastError());
    } else if (StorageLibrary.isSDReady()) {
      Serial.println("SD card is available. Remove it to test queueing records in LittleFS.");
    }
  }

  if (!StorageLibrary.isSDReady()) {
    String record = "record=" + String(recordNumber++) + ", millis=" + String(now) + "\n";
    if (!StorageLibrary.writeWithFallback(record)) {
      Serial.print("ERROR: Could not save the record: ");
      Serial.println(StorageLibrary.lastError());
    } else if (!sdWasReady && StorageLibrary.isSDReady()) {
      Serial.println("SD returned; queued records were transferred and verified.");
    } else if (StorageLibrary.isSDReady()) {
      Serial.println("Record saved on SD.");
    } else {
      Serial.println("Record queued in LittleFS until the SD card is available.");
    }
  }
}
