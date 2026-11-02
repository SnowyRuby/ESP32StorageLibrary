#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_system.h>

class ESP32StorageLibrary {
public:
  // Uses the built-in SD filesystem. Call begin() with the SD pins first.
  ESP32StorageLibrary();
  bool begin(uint8_t csPin, int8_t sckPin, int8_t misoPin, int8_t mosiPin,
             uint32_t frequency = 4000000);
  // Mounts SD using a caller-initialized SPI bus shared with another device.
  bool begin(uint8_t csPin, SPIClass &spi, uint32_t frequency = 4000000);
  // Unmounts the built-in SD filesystem so it can be mounted again cleanly.
  bool end();
  // Mounts LittleFS when this instance is bound to LittleFS. Formatting is
  // opt-in; pass true only when you accept erasing the LittleFS partition.
  bool begin(bool formatOnFail = false);

  // Access the bound filesystem for operations not wrapped by this library.
  fs::FS &filesystem() { return _fs; }

  // Also supports another filesystem, such as LittleFS.
  explicit ESP32StorageLibrary(fs::FS &filesystem);

  bool createFolder(const String &path);
  bool createFile(const String &path, const String &contents = "");
  bool appendFile(const String &path, const String &contents);
  bool overwriteFile(const String &path, const String &contents);
  bool deleteFile(const String &path);
  bool deleteFolder(const String &path);
  // Copies files or folders within this filesystem. The destination must not exist.
  bool copyPath(const String &sourcePath, const String &destinationPath);
  // Renames a file or folder within its current folder. The destination must not exist.
  bool renamePath(const String &sourcePath, const String &destinationPath);
  // Moves a file or folder to another folder while keeping its name. Destination must not exist.
  bool movePath(const String &sourcePath, const String &destinationPath);

  // Permanently deletes every file and folder on the mounted filesystem.
  bool wipeSDCard();
  // Creates a ZIP Store archive without compression.
  bool archiveFolder(const String &sourceFolder, const String &destinationZip);
  // Creates a ZIP archive using standard DEFLATE compression.
  bool archiveFolderCompressed(const String &sourceFolder, const String &destinationZip,
                               int compressionLevel = 6);
  // Extracts ZIP Store and DEFLATE entries.
  bool unarchiveFolder(const String &sourceZip, const String &destinationFolder);

  const String &lastError() const { return _lastError; }

private:
  fs::FS &_fs;
  String _lastError;
  bool setError(const String &message);
  bool archiveFolderInternal(const String &sourceFolder, const String &destinationZip,
                             bool compress, int compressionLevel);
  bool addFolderToZip(File &zip, const String &folder, const String &relative,
                      void *entries);
};

// Ready-to-use SD-backed instance: ESP32Storage.begin(cs, sck, miso, mosi)
extern ESP32StorageLibrary ESP32Storage;

// Named built-in filesystem instances. Example: StorageLibrary.SD.begin(...)
// or StorageLibrary.LittleFS.begin();
struct ESP32StorageBackends {
  ESP32StorageLibrary SD;
  ESP32StorageLibrary LittleFS;
  ESP32StorageBackends();

  // Starts both built-in filesystems. LittleFS is never formatted unless
  // formatLittleFSOnFail is explicitly true. Returns true if either mounts.
  bool beginStorage(uint8_t csPin, int8_t sckPin, int8_t misoPin, int8_t mosiPin,
                    uint32_t frequency = 4000000,
                    bool formatLittleFSOnFail = false,
                    SPIClass *sdSPI = nullptr);
  // Saves one record to SD, or queues it in LittleFS while SD is unavailable.
  // Each call creates its own .dat record under /StorageLibraryBackup on SD.
  bool writeWithFallback(const String &data);
  // Flushes queued records and starts a serial host session. SD remains
  // mounted for host file commands; writeWithFallback() queues app records.
  bool beginHostSession();
  // Ends the host session and resumes queued-record replay to SD.
  bool endHostSession();
  // Checks the SD mount and copies queued LittleFS records back to SD.
  bool syncBackup();
  bool isSDReady() const { return _sdReady; }
  bool isLittleFSReady() const { return _littleFsReady; }
  bool isHostSessionActive() const { return _hostSessionActive; }
  const String &lastError() const { return _lastError; }

private:
  bool _sdReady = false;
  bool _littleFsReady = false;
  bool _started = false;
  bool _hostSessionActive = false;
  uint8_t _csPin = 5;
  int8_t _sckPin = 18;
  int8_t _misoPin = 19;
  int8_t _mosiPin = 23;
  uint32_t _frequency = 4000000;
  SPIClass *_sdSPI = nullptr;
  uint32_t _lastSdAttempt = 0;
  String _lastError;

  bool refreshSD();
  bool queueBackupRecord(const String &data);
  bool transferQueuedRecord(const String &sourcePath);
  void markSDUnavailable();
};

extern ESP32StorageBackends StorageLibrary;
