#include <ESP32StorageLibrary.h>

// LittleFS version of BasicTest. It demonstrates folder creation, file
// creation, append, overwrite, and ZIP archiving on internal flash.
// It does not format or wipe LittleFS; it only removes its own test folder.
static const char *TEST_ROOT = "/LittleFSBasicTest";
static const char *SAVE_FOLDER = "/LittleFSBasicTest/Saves";
static const char *LOG_FILE = "/LittleFSBasicTest/log.txt";
static const char *PLAYER_FILE = "/LittleFSBasicTest/Saves/player1.txt";
static const char *ARCHIVE_FILE = "/LittleFSBasicTest/SavesBackup.zip";

static bool cleanTestFolder() {
  return !StorageLibrary.LittleFS.filesystem().exists(TEST_ROOT) ||
         StorageLibrary.LittleFS.deleteFolder(TEST_ROOT);
}

static void failAndClean(const char *step) {
  Serial.print("FAIL: ");
  Serial.println(step);
  Serial.print("Library message: ");
  Serial.println(StorageLibrary.LittleFS.lastError());
  if (cleanTestFolder())
    Serial.println("Removed this example's temporary folder.");
  else {
    Serial.print("WARNING: Could not remove this example's temporary folder: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32StorageLibrary BasicTest on LittleFS");
  Serial.println("Uses internal flash; it does not access or wipe the SD card.");
  Serial.println("Only /LittleFSBasicTest is cleaned up.");

  if (!StorageLibrary.LittleFS.begin()) {
    Serial.print("LittleFS mount failed: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
    Serial.println("No format was attempted.");
    return;
  }

  if (!cleanTestFolder()) {
    failAndClean("Could not remove this example's previous test folder.");
    return;
  }

  if (!StorageLibrary.LittleFS.createFolder(SAVE_FOLDER)) {
    failAndClean("Could not create the Saves folder.");
    return;
  }
  if (!StorageLibrary.LittleFS.createFile(LOG_FILE, "First line\n")) {
    failAndClean("Could not create the log file.");
    return;
  }
  if (!StorageLibrary.LittleFS.appendFile(LOG_FILE, "Second line\n")) {
    failAndClean("Could not append to the log file.");
    return;
  }
  if (!StorageLibrary.LittleFS.overwriteFile(PLAYER_FILE, "Player=1\nCoins=250\n")) {
    failAndClean("Could not write the player save file.");
    return;
  }
  if (!StorageLibrary.LittleFS.archiveFolder(SAVE_FOLDER, ARCHIVE_FILE)) {
    failAndClean("Could not create the ZIP archive.");
    return;
  }

  Serial.println("PASS: Folder creation and file create/append/overwrite.");
  Serial.println("PASS: ZIP archive created on LittleFS.");
  if (cleanTestFolder())
    Serial.println("PASS: Removed this example's test folder. Other LittleFS files were left alone.");
  else {
    Serial.print("WARNING: Could not remove this example's test folder: ");
    Serial.println(StorageLibrary.LittleFS.lastError());
  }
}

void loop() {
  // Add project code here.
  delay(1000);
}
