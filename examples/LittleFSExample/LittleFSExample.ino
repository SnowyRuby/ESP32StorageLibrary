#include <ESP32StorageLibrary.h>

// Demonstrates the library with ESP32 internal flash using LittleFS instead
// of an SD card. It tests file operations and a ZIP round trip, then removes
// only its own /ESP32StorageLibraryExample folder. It does not format or wipe
// the LittleFS filesystem and does not affect files elsewhere on the device.
// Set true only if you want the whole LittleFS partition formatted when mount
// fails. Formatting erases all data stored in LittleFS.
static const bool FORMAT_LITTLEFS_ON_MOUNT_FAILURE = false;

static const char *TEST_ROOT = "/ESP32StorageLibraryExample";
static const char *SOURCE_FOLDER = "/ESP32StorageLibraryExample/Original";
static const char *SOURCE_FILE = "/ESP32StorageLibraryExample/Original/sample.txt";
static const char *ARCHIVE_FILE = "/ESP32StorageLibraryExample/sample.zip";
static const char *RESTORED_FOLDER = "/ESP32StorageLibraryExample/Restored";
static const char *RESTORED_FILE = "/ESP32StorageLibraryExample/Restored/sample.txt";

static bool cleanExampleFiles() {
  return !StorageLibrary.LittleFS.filesystem().exists(TEST_ROOT) || StorageLibrary.LittleFS.deleteFolder(TEST_ROOT);
}

static void failAndClean(const char *step) {
  Serial.print("FAIL: ");
  Serial.println(step);
  Serial.print("Library message: ");
  Serial.println(StorageLibrary.LittleFS.lastError());
  if (StorageLibrary.LittleFS.filesystem().exists(TEST_ROOT)) {
    if (cleanExampleFiles())
      Serial.println("Removed this example's temporary folder.");
    else {
      Serial.print("WARNING: Could not remove this example's temporary folder: ");
      Serial.println(StorageLibrary.LittleFS.lastError());
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32StorageLibrary LittleFS example");
  Serial.println("Uses internal flash; the SD card is not accessed.");
  Serial.println("Only /ESP32StorageLibraryExample is cleaned up after a successful mount.");
  if (FORMAT_LITTLEFS_ON_MOUNT_FAILURE)
    Serial.println("WARNING: if mounting fails, the LittleFS partition will be formatted and all its data erased.");

  // StorageLibrary.LittleFS.begin() does not format unless explicitly passed true.
  if (!StorageLibrary.LittleFS.begin(FORMAT_LITTLEFS_ON_MOUNT_FAILURE)) {
    Serial.print("FAIL: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
    if (!FORMAT_LITTLEFS_ON_MOUNT_FAILURE)
      Serial.println("No format was attempted. If this partition is disposable, set FORMAT_LITTLEFS_ON_MOUNT_FAILURE to true and upload again.");
    return;
  }

  if (!cleanExampleFiles()) {
    failAndClean("Could not clear this example's old temporary folder.");
    return;
  }

  if (!StorageLibrary.LittleFS.createFolder(SOURCE_FOLDER)) {
    failAndClean("Could not create the test folder.");
    return;
  }
  if (!StorageLibrary.LittleFS.createFile(SOURCE_FILE, "First line\n")) {
    failAndClean("Could not create the test file.");
    return;
  }
  if (!StorageLibrary.LittleFS.appendFile(SOURCE_FILE, "Second line\n")) {
    failAndClean("Could not append to the test file.");
    return;
  }
  if (!StorageLibrary.LittleFS.overwriteFile(SOURCE_FILE, "LittleFS file contents\n")) {
    failAndClean("Could not overwrite the test file.");
    return;
  }

  File input = StorageLibrary.LittleFS.filesystem().open(SOURCE_FILE, FILE_READ);
  if (!input) {
    failAndClean("Could not open the test file for reading.");
    return;
  }
  String contents = input.readString();
  input.close();
  if (contents != "LittleFS file contents\n") {
    failAndClean("Read-back contents did not match.");
    return;
  }
  Serial.println("PASS: LittleFS file create, append, overwrite, and read-back.");

  if (!StorageLibrary.LittleFS.archiveFolder(SOURCE_FOLDER, ARCHIVE_FILE)) {
    failAndClean("Could not create a ZIP archive on LittleFS.");
    return;
  }
  if (!StorageLibrary.LittleFS.unarchiveFolder(ARCHIVE_FILE, RESTORED_FOLDER)) {
    failAndClean("Could not extract the ZIP archive from LittleFS.");
    return;
  }

  File restored = StorageLibrary.LittleFS.filesystem().open(RESTORED_FILE, FILE_READ);
  if (!restored) {
    failAndClean("Could not open the extracted file.");
    return;
  }
  String restoredContents = restored.readString();
  restored.close();
  if (restoredContents != "LittleFS file contents\n") {
    failAndClean("Extracted file contents did not match.");
    return;
  }
  Serial.println("PASS: ZIP archive extracted and contents verified on LittleFS.");

  if (cleanExampleFiles())
    Serial.println("PASS: Removed this example's temporary folder. Other LittleFS files were left alone.");
  else {
    Serial.print("WARNING: Could not remove this example's temporary folder: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
  }
}

void loop() {
  delay(1000);
}
