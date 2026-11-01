#include <ESP32StorageLibrary.h>

// Writes a 32 KiB patterned file in small chunks, then reads it back in chunks
// and verifies every byte. This checks larger-file I/O without holding the
// entire file in RAM.
// WARNING: after mounting, this example wipes ALL SD card contents at the end
// of the test or after any detected failure.
// Common ESP32 DevKit V1 VSPI wiring. Change these pins to match your board.
static const int8_t SD_CS_PIN = 5;
static const int8_t SD_SCK_PIN = 18;
static const int8_t SD_MISO_PIN = 19;
static const int8_t SD_MOSI_PIN = 23;
static const char *TEST_PATH = "/LargeFileTest.bin";
static const size_t TOTAL_BYTES = 32 * 1024;
static const size_t CHUNK_SIZE = 512;

static char expectedByte(size_t offset) {
  return 'A' + ((offset * 17 + offset / 13) % 26);
}

static void finishAndWipe(bool passed) {
  Serial.println(passed ? "Large file test passed." : "Large file test failed.");
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
  Serial.println("ESP32StorageLibrary large file test");
  Serial.println("Writes and verifies 32 KiB using 512-byte chunks.");
  Serial.println("WARNING: this example erases ALL SD card contents after the test or a detected failure.");

  if (!StorageLibrary.SD.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN)) {
    Serial.print("FAIL: SD mount failed: ");
    Serial.println(StorageLibrary.SD.lastError());
    Serial.println("Wipe skipped because the SD card did not mount.");
    return;
  }

  if (StorageLibrary.SD.filesystem().exists(TEST_PATH) && !StorageLibrary.SD.deleteFile(TEST_PATH)) {
    Serial.print("FAIL: Could not remove an old test file: ");
    Serial.println(StorageLibrary.SD.lastError());
    finishAndWipe(false);
    return;
  }

  String chunk;
  chunk.reserve(CHUNK_SIZE);
  for (size_t offset = 0; offset < TOTAL_BYTES; offset += CHUNK_SIZE) {
    chunk = "";
    for (size_t i = 0; i < CHUNK_SIZE; ++i)
      chunk += expectedByte(offset + i);
    bool written = offset == 0
        ? StorageLibrary.SD.createFile(TEST_PATH, chunk)
        : StorageLibrary.SD.appendFile(TEST_PATH, chunk);
    if (!written) {
      Serial.print("FAIL: Could not write at byte ");
      Serial.print((unsigned int)offset);
      Serial.print(": ");
      Serial.println(StorageLibrary.SD.lastError());
      finishAndWipe(false);
      return;
    }
  }
  Serial.printf("PASS: Wrote %u bytes in chunks.\n", (unsigned int)TOTAL_BYTES);

  File input = StorageLibrary.SD.filesystem().open(TEST_PATH, FILE_READ);
  if (!input) {
    Serial.println("FAIL: Could not open the large test file for reading.");
    finishAndWipe(false);
    return;
  }
  char buffer[CHUNK_SIZE];
  size_t offset = 0;
  while (offset < TOTAL_BYTES) {
    size_t wanted = min(CHUNK_SIZE, TOTAL_BYTES - offset);
    int count = input.read((uint8_t *)buffer, wanted);
    if (count != (int)wanted) {
      input.close();
      Serial.println("FAIL: Read ended early or returned a short chunk.");
      finishAndWipe(false);
      return;
    }
    for (size_t i = 0; i < wanted; ++i) {
      if (buffer[i] != expectedByte(offset + i)) {
        input.close();
        Serial.printf("FAIL: Data mismatch at byte %u.\n", (unsigned int)(offset + i));
        finishAndWipe(false);
        return;
      }
    }
    offset += wanted;
  }
  input.close();
  Serial.println("PASS: All bytes read back correctly in chunks.");
  finishAndWipe(true);
}

void loop() {
  delay(1000);
}
