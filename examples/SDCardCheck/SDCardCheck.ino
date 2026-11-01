#include <ESP32StorageLibrary.h>

// Checks mounting, file creation, append, read-back, and deletion.
// WARNING: after mounting, this example wipes ALL SD card contents at the
// end of the check, whether it passes or fails.
// Common ESP32 DevKit V1 VSPI wiring. Change these pins to match your board.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;

static void finishAndWipe(bool passed) {
  Serial.println(passed ? "SD card check passed." : "SD card check failed.");
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
  Serial.println("ESP32StorageLibrary SD card check");
  Serial.println("WARNING: this example erases ALL SD card contents after the check or a test failure.");

  if (!StorageLibrary.SD.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN)) {
    Serial.print("FAIL: Could not mount SD card: ");
    Serial.println(StorageLibrary.SD.lastError());
    Serial.println("Wipe skipped because the SD card did not mount.");
    return;
  }
  Serial.println("PASS: SD card mounted.");

  String testPath = "/SDCardCheck-" + String(millis()) + ".txt";
  uint8_t suffix = 0;
  while (StorageLibrary.SD.filesystem().exists(testPath)) {
    testPath = "/SDCardCheck-" + String(millis()) + "-" + String(++suffix) + ".txt";
  }

  const String expected = "SD write/read check\nAppend check passed\n";
  if (!StorageLibrary.SD.createFile(testPath, "SD write/read check\n")) {
    Serial.print("FAIL: Could not create/write test file: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }
  if (!StorageLibrary.SD.appendFile(testPath, "Append check passed\n")) {
    Serial.print("FAIL: Could not append to test file: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  File testFile = StorageLibrary.SD.filesystem().open(testPath, FILE_READ);
  if (!testFile) {
    Serial.println("FAIL: Could not open test file for reading.");
    finishAndWipe(false);
    return;
  }
  String actual = testFile.readString();
  testFile.close();
  if (actual != expected) {
    Serial.println("FAIL: Data read back did not match the data written.");
    finishAndWipe(false);
    return;
  }
  Serial.println("PASS: File write, append, and read-back verified.");

  if (!StorageLibrary.SD.deleteFile(testPath)) {
    Serial.print("FAIL: Test passed, but could not remove the temporary test file: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }
  Serial.println("PASS: Temporary test file removed.");
  finishAndWipe(true);
}

void loop() {
  delay(1000);
}
