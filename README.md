# ESP32StorageLibrary

An Arduino library for ESP32 projects using the built-in SD card over SPI.

## Quick start

```cpp
#include <ESP32StorageLibrary.h>

void setup() {
  Serial.begin(115200);
  if (!ESP32Storage.begin(5, 18, 19, 23)) {
    Serial.println(ESP32Storage.lastError());
    return;
  }
  ESP32Storage.createFile("/hello.txt", "Hello\n");
}
```

The `begin()` pin order is chip select, clock, MISO, MOSI. Change the example pins to match your wiring.

The library also exposes named built-in instances: use `StorageLibrary.SD.begin(cs, sck, miso, mosi)` for SD, or `StorageLibrary.LittleFS.begin()` for internal flash. `StorageLibrary.SD.end()` unmounts SD so it can be mounted again cleanly after removal. LittleFS mounting does not format by default; `StorageLibrary.LittleFS.begin(true)` opts into formatting the LittleFS partition if mounting fails, which erases its contents. The original `ESP32Storage` SD instance remains available.

For applications that need SD/LittleFS failover, call `StorageLibrary.beginStorage(cs, sck, miso, mosi)` once, then `StorageLibrary.writeWithFallback(data)` for each record. It writes each record as a separate `.dat` file under `/StorageLibraryBackup` on SD; if SD is unavailable, it queues the record in LittleFS. Call `StorageLibrary.syncBackup()` periodically to retry the SD connection and transfer queued files. A queued LittleFS file is removed only after its SD copy is verified. `StorageLibrary.isSDReady()` and `StorageLibrary.isLittleFSReady()` report mount status. This function is for records, not for writing to an arbitrary destination path.

For a PC file-transfer session over USB serial, call `StorageLibrary.beginHostSession()` before handling PC commands. It first syncs queued records, then pauses automatic SD remount/replay. SD stays mounted so the ESP32 serial command handler can list, read, and write PC-requested files. Calls to `writeWithFallback(data)` during the session go to LittleFS. After the PC session ends, call `StorageLibrary.endHostSession()` to resume queue sync. If SD is unavailable at that point, the queue remains in LittleFS and later `syncBackup()` or `writeWithFallback()` calls can retry. Use `StorageLibrary.SD` directly only for the serial file commands while the session is active; ordinary application writes should go through `writeWithFallback()`.

## Copy files between a PC and the ESP32

To browse the SD card from a PC and copy files or folders over the board's USB serial COM port, install the separate **ESP32PcSession** Arduino library as well as this library. `ESP32StorageLibrary` provides the session-aware LittleFS fallback; `ESP32PcSession` provides the serial server and transfer protocol. The matching Windows app and firmware example are in the ESP32 Serial File Manager project. The ESP32 does not appear as a Windows drive; use the companion app.

## Hardware testing

This library has only been tested on the **ESP32 Dev Module**. Other ESP32 boards have not been tested.

## Functions

- `createFolder()` creates a folder and any missing parent folders.
- `createFile()` creates a file and fails if it already exists.
- `appendFile()` adds data to a file.
- `overwriteFile()` replaces a file's contents or creates the file.
- `deleteFile()` removes a file.
- `deleteFolder()` recursively removes a folder and its contents.
- `wipeSDCard()` removes every file and folder from the mounted SD card.
- `archiveFolder()` creates a ZIP using Store mode (no compression).
- `archiveFolderCompressed()` creates a ZIP using DEFLATE compression.
- `unarchiveFolder()` extracts Store and DEFLATE ZIP entries.
- `lastError()` returns the latest error message.

Both archive functions remove the source folder after a successful archive. ZIP operations stream data in blocks; DEFLATE uses additional ESP32 heap memory. The implementation uses miniz 3.1.2; see `src/miniz/LICENSE`.

## Install

In Arduino IDE, choose **Sketch → Include Library → Add .ZIP Library...** and select `ESP32StorageLibrary.zip`. Then open **File → Examples → ESP32StorageLibrary**.

## Examples and storage cleanup

The SD test examples **BasicTestSD**, **SDCardCheck**, **ZipRoundTripStore**, **ZipRoundTripCompressed**, **LargeFileTest**, and **ZipErrorHandling** call `StorageLibrary.SD.wipeSDCard()` after a successful mount when the test finishes or a handled test step fails. This erases **every file and folder** on the card; it does not restore the card's prior contents. Each sketch prints this warning in Serial Monitor before testing. If the card cannot be mounted, the example reports that the wipe was skipped. A power loss or device crash can interrupt cleanup. The starter sketches, LittleFS examples, and SD/LittleFS fallback example do not wipe either filesystem.

- **BasicTestSD** exercises folder creation, file creation, append, overwrite, and Store-mode archiving on an SD card.
- **BasicTestLittleFS** runs the same basic operations using LittleFS on internal flash and cleans up only its own test folder.
- **SDLittleFSFallback** demonstrates the library's `beginStorage()`, `writeWithFallback()`, and `syncBackup()` API. It queues sample records in LittleFS while SD is unavailable, then transfers and verifies them on SD when it returns before clearing the LittleFS queue.
- **SDCardCheck** checks SD mounting and basic file write, append, read-back, and delete operations.
- **ZipRoundTripStore** creates a ZIP without compression, extracts it, and compares the restored file with the original.
- **ZipRoundTripCompressed** repeats the ZIP round-trip using DEFLATE compression and reports the archive size.
- **LargeFileTest** writes and verifies a 32 KiB patterned file using 512-byte chunks.
- **ZipErrorHandling** checks that missing and invalid ZIP files are rejected and displays the reported errors.
- **LittleFSExample** demonstrates file operations and ZIP extraction using internal flash with LittleFS. It does not format or wipe LittleFS and removes only its own `/ESP32StorageLibraryExample` test folder.
- **StarterSD** is a minimal project starting point that mounts an SD card.
- **StarterLittleFS** is a minimal starting point that mounts LittleFS without formatting it.
- **StarterSDAndLittleFS** initializes both filesystems and provides `sdReady` and `littleFsReady` status flags.

The three `Starter...` sketches do not create test files or wipe storage; add your project code in `setup()` or `loop()`.

Open Serial Monitor at 115200 baud to see the test results. Change the CS, SCK, MISO, and MOSI pin constants for your hardware.

## Compatibility

Target: ESP32 Arduino core 3.x. Uses Arduino `FS`, `SD`, and `SPI` APIs.
## GitHub repository and updates

The source code is hosted at [github.com/SnowyRuby/ESP32StorageLibrary](https://github.com/SnowyRuby/ESP32StorageLibrary). The Arduino Library Manager metadata also links to this repository.

Changes in a local copy are not uploaded automatically. To publish an update, commit your changes and push them to the repository's `main` branch, or upload the updated files through GitHub. You need write access to this private repository.
