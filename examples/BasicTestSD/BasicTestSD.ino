#include <ESP32StorageLibrary.h>

// Tests createFolder/createFile/appendFile/overwriteFile/archiveFolder.
// WARNING: after this card mounts, this example wipes ALL SD card contents
// at the end, whether the test passes or fails.
// Example defaults for an ESP32 DevKit V1 using the VSPI pins.
// Change these four values to match your SD card wiring.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;

static void finishAndWipe(bool passed) {
  Serial.println(passed ? "Test finished." : "Test failed.");
  Serial.println("Wiping ALL files and folders from the SD card now.");
  if (StorageLibrary.SD.wipeSDCard())
    Serial.println("SD card wipe complete.");
  else {
    Serial.print("WARNING: SD card wipe failed: ");
    Serial.println(StorageLibrary.SD.lastError());
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("WARNING: BasicTestSD erases ALL contents of the SD card after the test or a test failure.");
  Serial.printf("ESP reset reason: %d\n", (int)esp_reset_reason());
  if (!StorageLibrary.SD.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN)) {
    Serial.print("SD mount failed: ");
    Serial.println(StorageLibrary.SD.lastError());
    Serial.println("Wipe skipped because the SD card did not mount.");
    return;
  }
  Serial.println("SD mounted.");

  if (!StorageLibrary.SD.createFolder("/LibraryTest/Saves")) {
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  StorageLibrary.SD.deleteFile("/LibraryTest/log.txt");
  if (!StorageLibrary.SD.createFile("/LibraryTest/log.txt", "First line\n")) {
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  if (!StorageLibrary.SD.appendFile("/LibraryTest/log.txt", "Second line\n")) {
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  if (!StorageLibrary.SD.overwriteFile("/LibraryTest/Saves/player1.txt", "Player=1\nCoins=250\n")) {
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  if (!StorageLibrary.SD.archiveFolder("/LibraryTest/Saves", "/LibraryTest/SavesBackup.zip")) {
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }
  Serial.println("Archive created: /LibraryTest/SavesBackup.zip");

  finishAndWipe(true);
}

void loop() {
  delay(1000);
}
