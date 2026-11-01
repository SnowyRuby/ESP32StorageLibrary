#include <ESP32StorageLibrary.h>

// Checks that extraction rejects a ZIP that does not exist and a file with
// invalid ZIP data, and prints the library's error message for each case.
// WARNING: after mounting, this example wipes ALL SD card contents at the end
// of the test or after any detected failure.
// Common ESP32 DevKit V1 VSPI wiring. Change these pins to match your board.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;

static void finishAndWipe(bool passed) {
  Serial.println(passed ? "ZIP error handling test passed." : "ZIP error handling test failed.");
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
  Serial.println("ESP32StorageLibrary ZIP error handling example");
  Serial.println("Checks missing and invalid ZIP inputs are rejected.");
  Serial.println("WARNING: this example erases ALL SD card contents after the test or a detected failure.");

  if (!StorageLibrary.SD.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN)) {
    Serial.print("FAIL: SD mount failed: ");
    Serial.println(StorageLibrary.SD.lastError());
    Serial.println("Wipe skipped because the SD card did not mount.");
    return;
  }

  String missingZip = "/MissingZip-" + String(millis()) + ".zip";
  while (StorageLibrary.SD.filesystem().exists(missingZip))
    missingZip = "/MissingZip-" + String(millis()) + "-again.zip";
  String outputFolder = "/ZipErrorOutput-" + String(millis());
  if (StorageLibrary.SD.unarchiveFolder(missingZip, outputFolder)) {
    Serial.println("FAIL: Extraction unexpectedly accepted a missing ZIP.");
    finishAndWipe(false);
    return;
  }
  Serial.print("PASS: Missing ZIP rejected: ");
  Serial.println(StorageLibrary.SD.lastError());

  String invalidZip = "/InvalidZip-" + String(millis()) + ".zip";
  while (StorageLibrary.SD.filesystem().exists(invalidZip))
    invalidZip = "/InvalidZip-" + String(millis()) + "-again.zip";
  if (!StorageLibrary.SD.createFile(invalidZip, "This file is deliberately not a ZIP archive.")) {
    Serial.print("FAIL: Could not create invalid ZIP test input: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  outputFolder = "/InvalidZipOutput-" + String(millis());
  if (StorageLibrary.SD.unarchiveFolder(invalidZip, outputFolder)) {
    Serial.println("FAIL: Extraction unexpectedly accepted invalid ZIP data.");
    finishAndWipe(false);
    return;
  }
  Serial.print("PASS: Invalid ZIP rejected: ");
  Serial.println(StorageLibrary.SD.lastError());
  finishAndWipe(true);
}

void loop() {
  delay(1000);
}
