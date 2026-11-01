#include <ESP32StorageLibrary.h>

// Creates a test file, makes a ZIP Store archive without compression,
// extracts it, checks the restored contents, then wipes the entire SD card.
// WARNING: ALL files and folders on the SD card are erased after the test or
// a detected failure.
// Common ESP32 DevKit V1 VSPI wiring. Change these pins to match your board.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;

static void finishAndWipe(bool passed) {
  Serial.println(passed ? "ZIP Store round-trip test passed." : "ZIP Store round-trip test failed.");
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
  Serial.println("ESP32StorageLibrary ZIP round-trip example");
  Serial.println("WARNING: this example erases ALL SD card contents after the test or a detected failure.");

  if (!StorageLibrary.SD.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN)) {
    Serial.print("FAIL: SD mount failed: ");
    Serial.println(StorageLibrary.SD.lastError());
    Serial.println("Wipe skipped because the SD card did not mount.");
    return;
  }

  String base = "/ZipRoundTrip-" + String(millis());
  uint16_t suffix = 0;
  while (StorageLibrary.SD.filesystem().exists(base) || StorageLibrary.SD.filesystem().exists(base + ".zip"))
    base = "/ZipRoundTrip-" + String(millis()) + "-" + String(++suffix);
  String source = base + "/Original";
  String archive = base + ".zip";
  String restored = base + "/Restored";
  String originalText;
  for (uint8_t i = 0; i < 64; ++i)
    originalText += "Repeated ZIP test text helps demonstrate the Store archive.\n";
  String fileName = "message.txt";

  if (!StorageLibrary.SD.createFolder(source) ||
      !StorageLibrary.SD.createFile(source + "/" + fileName, originalText)) {
    Serial.print("FAIL: Could not create source test file: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  // archiveFolder removes the source folder after the ZIP validates.
  if (!StorageLibrary.SD.archiveFolder(source, archive)) {
    Serial.print("FAIL: Could not create ZIP archive: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }
  Serial.println("PASS: Test folder archived.");
  File archiveFile = StorageLibrary.SD.filesystem().open(archive, FILE_READ);
  if (archiveFile) {
    Serial.printf("Source data: %u bytes; uncompressed ZIP: %u bytes\n",
                  (unsigned int)originalText.length(), (unsigned int)archiveFile.size());
    archiveFile.close();
  }

  if (!StorageLibrary.SD.unarchiveFolder(archive, restored)) {
    Serial.print("FAIL: Could not extract ZIP archive: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  File restoredFile = StorageLibrary.SD.filesystem().open(restored + "/" + fileName, FILE_READ);
  if (!restoredFile) {
    Serial.println("FAIL: Restored file could not be opened.");
    finishAndWipe(false);
    return;
  }
  String restoredText = restoredFile.readString();
  restoredFile.close();
  if (restoredText != originalText) {
    Serial.println("FAIL: Restored file contents do not match the original.");
    finishAndWipe(false);
    return;
  }
  Serial.println("PASS: ZIP extracted and file contents verified.");

  bool folderRemoved = StorageLibrary.SD.deleteFolder(base);
  bool archiveRemoved = StorageLibrary.SD.deleteFile(archive);
  if (folderRemoved && archiveRemoved)
    Serial.println("PASS: Temporary test files cleaned up.");
  else
    Serial.println("Temporary cleanup incomplete; the final card wipe will remove remaining files.");
  finishAndWipe(true);
}

void loop() {
  delay(1000);
}
