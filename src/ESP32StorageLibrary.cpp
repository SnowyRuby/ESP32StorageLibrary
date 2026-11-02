#include "ESP32StorageLibrary.h"
#include "miniz/miniz.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <vector>

struct ZipEntryInfo {
  String name;
  uint32_t crc;
  uint32_t size;
  uint32_t compressedSize;
  uint32_t offset;
  uint16_t method;
};

static String zipBuildError;

static void *zipStateAlloc(void *, size_t items, size_t size) {
  if (size && items > SIZE_MAX / size) return NULL;
  size_t bytes = items * size;
  void *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  return memory;
}

static void zipStateFree(void *, void *memory) {
  if (memory) heap_caps_free(memory);
}

static void put16(File &f, uint16_t v) {
  f.write((uint8_t)(v & 0xff)); f.write((uint8_t)(v >> 8));
}
static void put32(File &f, uint32_t v) {
  put16(f, (uint16_t)v); put16(f, (uint16_t)(v >> 16));
}
static uint32_t crcUpdate(uint32_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ (0xedb88320UL & (-(int32_t)(crc & 1)));
  }
  return crc;
}
static uint16_t zip16(const uint8_t *b) {
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}
static uint32_t zip32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static bool zipReadAt(File &f, uint32_t offset, uint8_t *buffer, size_t length) {
  return f.seek(offset) && f.read(buffer, length) == (int)length;
}
static String baseName(String name) {
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  return name;
}
static void dosDateTime(uint16_t &date, uint16_t &time) {
  // Valid ZIP timestamp fallback (1980-01-01 00:00:00).
  date = (uint16_t)((1 << 5) | 1);
  time = 0;
}

ESP32StorageLibrary::ESP32StorageLibrary() : _fs(SD) {}
ESP32StorageLibrary ESP32Storage;
ESP32StorageBackends::ESP32StorageBackends() : SD(), LittleFS(::LittleFS) {}
ESP32StorageBackends StorageLibrary;

static const char *FALLBACK_SD_FOLDER = "/StorageLibraryBackup";
static const char *FALLBACK_LITTLEFS_FOLDER = "/StorageLibraryQueue";
static const uint32_t FALLBACK_SD_RETRY_MS = 5000;

static bool storageFilesMatch(fs::FS &leftFs, const String &leftPath,
                              fs::FS &rightFs, const String &rightPath) {
  File left = leftFs.open(leftPath, FILE_READ);
  File right = rightFs.open(rightPath, FILE_READ);
  if (!left || !right) {
    if (left) left.close();
    if (right) right.close();
    return false;
  }
  if (left.size() != right.size()) {
    left.close();
    right.close();
    return false;
  }
  uint8_t leftBuffer[256];
  uint8_t rightBuffer[256];
  bool matches = true;
  while (left.available() || right.available()) {
    int leftCount = left.read(leftBuffer, sizeof(leftBuffer));
    int rightCount = right.read(rightBuffer, sizeof(rightBuffer));
    if (leftCount <= 0 || rightCount != leftCount) {
      matches = false;
      break;
    }
    for (int i = 0; i < leftCount; ++i) {
      if (leftBuffer[i] != rightBuffer[i]) {
        matches = false;
        break;
      }
    }
    if (!matches) break;
  }
  left.close();
  right.close();
  return matches;
}

bool ESP32StorageBackends::beginStorage(uint8_t csPin, int8_t sckPin,
                                        int8_t misoPin, int8_t mosiPin,
                                        uint32_t frequency,
                                        bool formatLittleFSOnFail,
                                        SPIClass *sdSPI) {
  if (_hostSessionActive) {
    _lastError = "Cannot reinitialize storage during an active host session.";
    return false;
  }
  _csPin = csPin;
  _sckPin = sckPin;
  _misoPin = misoPin;
  _mosiPin = mosiPin;
  _frequency = frequency;
  _sdSPI = sdSPI;
  _started = true;
  _lastError = "";

  if (!_littleFsReady)
    _littleFsReady = LittleFS.begin(formatLittleFSOnFail);
  if (!_littleFsReady)
    _lastError = LittleFS.lastError();

  ::SD.end();
  if (_sdSPI) {
    _sdSPI->begin(_sckPin, _misoPin, _mosiPin, _csPin);
    _sdReady = ::SD.begin(_csPin, *_sdSPI, _frequency);
  } else {
    if (_sdSPI) {
      _sdSPI->begin(_sckPin, _misoPin, _mosiPin, _csPin);
      _sdReady = ::SD.begin(_csPin, *_sdSPI, _frequency);
    } else {
      _sdReady = SD.begin(_csPin, _sckPin, _misoPin, _mosiPin, _frequency);
    }
  }
  if (_sdReady && !SD.createFolder(FALLBACK_SD_FOLDER)) {
    _lastError = SD.lastError();
    ::SD.end();
    _sdReady = false;
  }
  if (!_sdReady && !_lastError.length())
    _lastError = SD.lastError();
  _lastSdAttempt = millis();
  return _sdReady || _littleFsReady;
}

void ESP32StorageBackends::markSDUnavailable() {
  ::SD.end();
  _sdReady = false;
}

bool ESP32StorageBackends::refreshSD() {
  if (_hostSessionActive) {
    _lastError = "SD retry is paused during the active host session.";
    return false;
  }
  if (!_started) {
    _lastError = "Call StorageLibrary.beginStorage() before using fallback writes.";
    return false;
  }
  if (!_littleFsReady) _littleFsReady = LittleFS.begin(false);

  if (_sdReady) {
    File folder = SD.filesystem().open(FALLBACK_SD_FOLDER, FILE_READ);
    bool responding = folder && folder.isDirectory();
    if (folder) folder.close();
    if (!responding) markSDUnavailable();
  }

  if (!_sdReady && (uint32_t)(millis() - _lastSdAttempt) >= FALLBACK_SD_RETRY_MS) {
    _lastSdAttempt = millis();
    _sdReady = SD.begin(_csPin, _sckPin, _misoPin, _mosiPin, _frequency);
    if (_sdReady && !SD.createFolder(FALLBACK_SD_FOLDER)) {
      _lastError = SD.lastError();
      markSDUnavailable();
    }
  }
  return _sdReady;
}

bool ESP32StorageBackends::beginHostSession() {
  if (!_started) {
    _lastError = "Call StorageLibrary.beginStorage() before starting a host session.";
    return false;
  }
  if (_hostSessionActive) return true;
  if (!syncBackup()) return false;
  _hostSessionActive = true;
  _lastError = "";
  return true;
}

bool ESP32StorageBackends::endHostSession() {
  if (!_started) {
    _lastError = "Call StorageLibrary.beginStorage() before ending a host session.";
    return false;
  }
  _hostSessionActive = false;
  // Force an immediate remount attempt if the SD card was removed during the
  // serial session. If it fails, later syncBackup()/writeWithFallback() calls
  // can retry normally.
  _lastSdAttempt = millis() - FALLBACK_SD_RETRY_MS;
  return syncBackup();
}

bool ESP32StorageBackends::queueBackupRecord(const String &data) {
  if (!_littleFsReady) {
    _lastError = "LittleFS is unavailable; the backup record could not be queued.";
    return false;
  }
  if (!LittleFS.createFolder(FALLBACK_LITTLEFS_FOLDER)) {
    _lastError = LittleFS.lastError();
    return false;
  }
  String base = "record-" + String(millis());
  String path = String(FALLBACK_LITTLEFS_FOLDER) + "/" + base + ".dat";
  uint16_t suffix = 0;
  while (LittleFS.filesystem().exists(path)) {
    path = String(FALLBACK_LITTLEFS_FOLDER) + "/" + base + "-" + String(++suffix) + ".dat";
  }
  if (!LittleFS.createFile(path, data)) {
    _lastError = LittleFS.lastError();
    return false;
  }
  _lastError = "";
  return true;
}

bool ESP32StorageBackends::transferQueuedRecord(const String &sourcePath) {
  int slash = sourcePath.lastIndexOf('/');
  if (slash < 0) {
    _lastError = "Invalid LittleFS queue filename.";
    return false;
  }
  String targetPath = String(FALLBACK_SD_FOLDER) + sourcePath.substring(slash);

  if (SD.filesystem().exists(targetPath)) {
    if (storageFilesMatch(LittleFS.filesystem(), sourcePath,
                          SD.filesystem(), targetPath)) {
      if (!LittleFS.deleteFile(sourcePath)) {
        _lastError = LittleFS.lastError();
        return false;
      }
      return true;
    }
    if (!SD.deleteFile(targetPath)) {
      _lastError = SD.lastError();
      markSDUnavailable();
      return false;
    }
  }

  File source = LittleFS.filesystem().open(sourcePath, FILE_READ);
  File target = SD.filesystem().open(targetPath, FILE_WRITE);
  if (!source || !target) {
    if (source) source.close();
    if (target) target.close();
    _lastError = "Could not open files for the LittleFS-to-SD backup transfer.";
    markSDUnavailable();
    return false;
  }
  uint8_t buffer[256];
  bool copied = true;
  while (source.available()) {
    int count = source.read(buffer, sizeof(buffer));
    if (count <= 0 || target.write(buffer, count) != (size_t)count) {
      copied = false;
      break;
    }
  }
  source.close();
  target.flush();
  target.close();
  if (!copied || !storageFilesMatch(LittleFS.filesystem(), sourcePath,
                                    SD.filesystem(), targetPath)) {
    _lastError = "Backup copy to SD did not verify; the LittleFS copy was kept.";
    markSDUnavailable();
    return false;
  }
  if (!LittleFS.deleteFile(sourcePath)) {
    _lastError = LittleFS.lastError();
    return false;
  }
  return true;
}

bool ESP32StorageBackends::syncBackup() {
  if (_hostSessionActive) {
    _lastError = "Backup replay is paused during the active host session.";
    return false;
  }
  if (!refreshSD()) {
    if (!_littleFsReady) _lastError = "LittleFS is unavailable and SD could not be mounted.";
    else if (!_lastError.length()) _lastError = SD.lastError();
    return false;
  }
  if (!_littleFsReady) {
    _lastError = "LittleFS could not be mounted; queued records cannot be checked.";
    return false;
  }
  if (!LittleFS.filesystem().exists(FALLBACK_LITTLEFS_FOLDER)) {
    _lastError = "";
    return true;
  }

  File directory = LittleFS.filesystem().open(FALLBACK_LITTLEFS_FOLDER, FILE_READ);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    _lastError = "Could not open the LittleFS backup queue folder.";
    return false;
  }
  std::vector<String> queuedFiles;
  while (true) {
    File entry = directory.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String path = String(entry.name());
      int entrySlash = path.lastIndexOf('/');
      String name = entrySlash >= 0 ? path.substring(entrySlash + 1) : path;
      queuedFiles.push_back(String(FALLBACK_LITTLEFS_FOLDER) + "/" + name);
    }
    entry.close();
  }
  directory.close();
  std::sort(queuedFiles.begin(), queuedFiles.end());
  for (const String &path : queuedFiles) {
    if (!transferQueuedRecord(path)) return false;
  }
  _lastError = "";
  return true;
}

bool ESP32StorageBackends::writeWithFallback(const String &data) {
  if (_hostSessionActive) return queueBackupRecord(data);
  bool wasSDReady = _sdReady;
  bool cardReady = refreshSD();
  // If this call detected that the card just returned, queue the current
  // record first and transfer it with the backlog in one verified pass.
  if (!wasSDReady && _littleFsReady) {
    if (cardReady) {
      if (!queueBackupRecord(data)) return false;
      return syncBackup();
    }
  }
  if (cardReady && _littleFsReady && !syncBackup()) cardReady = false;
  if (!cardReady) return queueBackupRecord(data);

  String base = "record-" + String(millis());
  String path = String(FALLBACK_SD_FOLDER) + "/" + base + ".dat";
  uint16_t suffix = 0;
  while (SD.filesystem().exists(path))
    path = String(FALLBACK_SD_FOLDER) + "/" + base + "-" + String(++suffix) + ".dat";
  if (SD.createFile(path, data)) {
    _lastError = "";
    return true;
  }
  _lastError = SD.lastError();
  SD.deleteFile(path);
  markSDUnavailable();
  return queueBackupRecord(data);
}

bool ESP32StorageLibrary::begin(uint8_t csPin, int8_t sckPin, int8_t misoPin,
                                int8_t mosiPin, uint32_t frequency) {
  SPI.begin(sckPin, misoPin, mosiPin, csPin);
  if (!SD.begin(csPin, SPI, frequency))
    return setError("SD mount failed; check CS, SCK, MISO, and MOSI pins.");
  _lastError = "";
  return true;
}

bool ESP32StorageLibrary::end() {
  if (&_fs != static_cast<fs::FS *>(&::SD))
    return setError("end() is only available for the SD filesystem.");
  ::SD.end();
  _lastError = "";
  return true;
}

bool ESP32StorageLibrary::begin(bool formatOnFail) {
  if (&_fs != static_cast<fs::FS *>(&::LittleFS))
    return setError("This begin() overload is only for StorageLibrary.LittleFS.");
  if (!::LittleFS.begin(formatOnFail))
    return setError(formatOnFail ? "LittleFS mount failed, including the requested format attempt."
                                 : "LittleFS mount failed; no format was attempted.");
  _lastError = "";
  return true;
}
ESP32StorageLibrary::ESP32StorageLibrary(fs::FS &filesystem) : _fs(filesystem) {}

bool ESP32StorageLibrary::setError(const String &message) {
  _lastError = message;
  return false;
}

bool ESP32StorageLibrary::createFolder(const String &path) {
  if (path.length() == 0 || path == "/") return true;
  if (_fs.exists(path)) {
    File f = _fs.open(path);
    bool isDir = f && f.isDirectory();
    if (f) f.close();
    return isDir || setError("Path exists but is not a folder: " + path);
  }
  String current;
  for (size_t i = 0; i < path.length(); ++i) {
    current += path[i];
    if (path[i] == '/' && current.length() > 1) {
      if (!_fs.exists(current) && !_fs.mkdir(current)) return setError("Could not create folder: " + current);
    }
  }
  if (!_fs.exists(path) && !_fs.mkdir(path)) return setError("Could not create folder: " + path);
  return true;
}

bool ESP32StorageLibrary::createFile(const String &path, const String &contents) {
  int slash = path.lastIndexOf('/');
  if (slash > 0 && !createFolder(path.substring(0, slash))) return false;
  if (_fs.exists(path)) return setError("File already exists: " + path);
  File f = _fs.open(path, FILE_WRITE);
  if (!f) return setError("Could not create file: " + path);
  size_t written = contents.length() ? f.write((const uint8_t *)contents.c_str(), contents.length()) : 0;
  f.flush(); f.close();
  return written == contents.length() || setError("Incomplete file write: " + path);
}

bool ESP32StorageLibrary::appendFile(const String &path, const String &contents) {
  int slash = path.lastIndexOf('/');
  if (slash > 0 && !createFolder(path.substring(0, slash))) return false;
  File f = _fs.open(path, FILE_APPEND);
  if (!f) return setError("Could not open file for append: " + path);
  size_t written = f.write((const uint8_t *)contents.c_str(), contents.length());
  f.flush(); f.close();
  return written == contents.length() || setError("Incomplete append: " + path);
}

bool ESP32StorageLibrary::overwriteFile(const String &path, const String &contents) {
  int slash = path.lastIndexOf('/');
  if (slash > 0 && !createFolder(path.substring(0, slash))) return false;
  if (_fs.exists(path) && !_fs.remove(path)) return setError("Could not remove old file: " + path);
  File f = _fs.open(path, FILE_WRITE);
  if (!f) return setError("Could not create replacement file: " + path);
  size_t written = f.write((const uint8_t *)contents.c_str(), contents.length());
  f.flush(); f.close();
  return written == contents.length() || setError("Incomplete overwrite: " + path);
}

bool ESP32StorageLibrary::deleteFile(const String &path) {
  if (!_fs.exists(path)) return true;
  return _fs.remove(path) || setError("Could not delete file: " + path);
}

bool ESP32StorageLibrary::deleteFolder(const String &path) {
  File dir = _fs.open(path);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return setError("Not a folder: " + path); }
  std::vector<String> files, dirs;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String child = baseName(String(entry.name()));
    bool isDir = entry.isDirectory();
    entry.close();
    if (child.length() && child != "." && child != "..") (isDir ? dirs : files).push_back(path + "/" + child);
  }
  dir.close();
  for (const String &p : dirs) if (!deleteFolder(p)) return false;
  for (const String &p : files) if (!_fs.remove(p)) return setError("Could not delete file: " + p);
  return _fs.rmdir(path) || setError("Could not remove folder: " + path);
}

namespace {
bool copyStorageEntry(fs::FS &fs, const String &sourcePath,
                      const String &destinationPath, String &error) {
  File source = fs.open(sourcePath, FILE_READ);
  if (!source) { error = "Could not open source: " + sourcePath; return false; }
  const bool isDirectory = source.isDirectory();
  source.close();
  if (!isDirectory) {
    File input = fs.open(sourcePath, FILE_READ);
    File output = fs.open(destinationPath, FILE_WRITE);
    if (!input || !output) {
      if (input) input.close();
      if (output) output.close();
      error = "Could not open a file for copying.";
      return false;
    }
    uint8_t buffer[512];
    bool ok = true;
    while (input.available()) {
      const size_t count = input.read(buffer, sizeof(buffer));
      if (count == 0 || output.write(buffer, count) != count) { ok = false; break; }
    }
    output.flush();
    input.close();
    output.close();
    if (!ok) { error = "File copy was incomplete."; return false; }
    return true;
  }

  if (!fs.mkdir(destinationPath)) {
    error = "Could not create destination folder: " + destinationPath;
    return false;
  }
  File folder = fs.open(sourcePath, FILE_READ);
  if (!folder || !folder.isDirectory()) {
    if (folder) folder.close();
    error = "Could not read source folder: " + sourcePath;
    return false;
  }
  while (true) {
    File child = folder.openNextFile();
    if (!child) break;
    String name = String(child.name());
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    child.close();
    const String childSource = sourcePath + "/" + name;
    const String childDestination = destinationPath + "/" + name;
    if (!copyStorageEntry(fs, childSource, childDestination, error)) {
      folder.close();
      return false;
    }
  }
  folder.close();
  return true;
}
}

bool ESP32StorageLibrary::copyPath(const String &sourcePath,
                                   const String &destinationPath) {
  if (!sourcePath.length() || !destinationPath.length() || sourcePath == "/" ||
      destinationPath == "/" || sourcePath == destinationPath)
    return setError("Invalid source or destination path.");
  if (!_fs.exists(sourcePath)) return setError("Copy source does not exist.");
  if (_fs.exists(destinationPath)) return setError("Copy destination already exists.");
  if (destinationPath.startsWith(sourcePath + "/"))
    return setError("A folder cannot be copied into itself.");
  String error;
  if (!copyStorageEntry(_fs, sourcePath, destinationPath, error)) {
    // A failed recursive copy may leave a partial destination; remove it.
    if (_fs.exists(destinationPath)) {
      File destination = _fs.open(destinationPath, FILE_READ);
      const bool isDirectory = destination && destination.isDirectory();
      if (destination) destination.close();
      if (isDirectory) deleteFolder(destinationPath);
      else _fs.remove(destinationPath);
    }
    return setError(error);
  }
  _lastError = "";
  return true;
}

bool ESP32StorageLibrary::renamePath(const String &sourcePath,
                                     const String &destinationPath) {
  if (!sourcePath.length() || !destinationPath.length() || sourcePath == "/" ||
      destinationPath == "/" || sourcePath == destinationPath)
    return setError("Invalid source or destination path.");
  if (!_fs.exists(sourcePath)) return setError("Rename source does not exist.");
  if (_fs.exists(destinationPath)) return setError("Rename destination already exists.");
  const int sourceSlash = sourcePath.lastIndexOf('/');
  const int destinationSlash = destinationPath.lastIndexOf('/');
  const String sourceParent = sourceSlash <= 0 ? "/" : sourcePath.substring(0, sourceSlash);
  const String destinationParent = destinationSlash <= 0 ? "/" : destinationPath.substring(0, destinationSlash);
  if (sourceParent != destinationParent)
    return setError("Rename must stay in the current folder; use movePath() to change folders.");
  if (destinationPath.startsWith(sourcePath + "/"))
    return setError("A folder cannot be moved into itself.");
  if (!_fs.rename(sourcePath, destinationPath))
    return setError("Filesystem could not rename the path.");
  _lastError = "";
  return true;
}

bool ESP32StorageLibrary::movePath(const String &sourcePath,
                                   const String &destinationPath) {
  if (!sourcePath.length() || !destinationPath.length() || sourcePath == "/" ||
      destinationPath == "/" || sourcePath == destinationPath)
    return setError("Invalid source or destination path.");
  if (!_fs.exists(sourcePath)) return setError("Move source does not exist.");
  if (_fs.exists(destinationPath)) return setError("Move destination already exists.");
  const int sourceSlash = sourcePath.lastIndexOf('/');
  const int destinationSlash = destinationPath.lastIndexOf('/');
  const String sourceParent = sourceSlash <= 0 ? "/" : sourcePath.substring(0, sourceSlash);
  const String destinationParent = destinationSlash <= 0 ? "/" : destinationPath.substring(0, destinationSlash);
  if (sourceParent == destinationParent)
    return setError("Move must change folders; use renamePath() to change the name.");
  const String sourceName = sourcePath.substring(sourceSlash + 1);
  const String destinationName = destinationPath.substring(destinationSlash + 1);
  if (!sourceName.equalsIgnoreCase(destinationName))
    return setError("Move must keep the item's name unchanged.");
  if (destinationPath.startsWith(sourcePath + "/"))
    return setError("A folder cannot be moved into itself.");
  if (!_fs.rename(sourcePath, destinationPath))
    return setError("Filesystem could not move the path.");
  _lastError = "";
  return true;
}

bool ESP32StorageLibrary::wipeSDCard() {
  File root = _fs.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return setError("Could not open SD card root folder.");
  }

  std::vector<String> names;
  std::vector<bool> isDirs;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String name = baseName(String(entry.name()));
    bool isDir = entry.isDirectory();
    entry.close();
    if (name.length() && name != "." && name != "..") {
      names.push_back(name);
      isDirs.push_back(isDir);
    }
  }
  root.close();

  for (size_t i = 0; i < names.size(); ++i) {
    String path = "/" + names[i];
    if (isDirs[i]) {
      if (!deleteFolder(path)) return false;
    } else if (!_fs.remove(path)) {
      return setError("Could not delete file: " + path);
    }
  }

  _lastError = "";
  return true;
}
bool ESP32StorageLibrary::unarchiveFolder(const String &sourceZip,
                                          const String &destinationFolder) {
  File zip = _fs.open(sourceZip, FILE_READ);
  if (!zip || zip.isDirectory()) {
    if (zip) zip.close();
    return setError("Could not open ZIP file: " + sourceZip);
  }
  size_t length = zip.size();
  if (length < 22 || length > 0xffffffffUL) {
    zip.close();
    return setError("Invalid ZIP size or ZIP64 is not supported.");
  }

  uint32_t eocd = 0;
  bool found = false;
  uint32_t start = length > 65557 ? (uint32_t)(length - 65557) : 0;
  for (uint32_t p = (uint32_t)length - 22;; --p) {
    uint8_t end[22];
    if (!zipReadAt(zip, p, end, sizeof(end))) break;
    if (zip32(end) == 0x06054b50UL &&
        (uint64_t)p + 22 + zip16(end + 20) <= length) {
      eocd = p;
      found = true;
      break;
    }
    if (p == start) break;
  }
  if (!found) { zip.close(); return setError("ZIP end record not found."); }

  uint8_t end[22];
  zipReadAt(zip, eocd, end, sizeof(end));
  uint16_t count = zip16(end + 10);
  uint32_t centralSize = zip32(end + 12);
  uint32_t centralOffset = zip32(end + 16);
  if (zip16(end + 4) || zip16(end + 6) || zip16(end + 8) != count ||
      (uint64_t)centralOffset + centralSize > eocd) {
    zip.close();
    return setError("Multi-disk, ZIP64, or invalid ZIP archives are not supported.");
  }
  if (!createFolder(destinationFolder)) { zip.close(); return false; }

  uint32_t pos = centralOffset;
  uint32_t centralEnd = centralOffset + centralSize;
  uint8_t buffer[512];
  for (uint16_t i = 0; i < count; ++i) {
    uint8_t h[46];
    if (pos > centralEnd || centralEnd - pos < sizeof(h) ||
        !zipReadAt(zip, pos, h, sizeof(h)) || zip32(h) != 0x02014b50UL) {
      zip.close();
      return setError("Invalid ZIP central directory.");
    }
    uint16_t flags = zip16(h + 8), method = zip16(h + 10);
    uint32_t expectedCrc = zip32(h + 16), packedSize = zip32(h + 20);
    uint32_t unpackedSize = zip32(h + 24), localOffset = zip32(h + 42);
    uint16_t nameLen = zip16(h + 28), extraLen = zip16(h + 30), commentLen = zip16(h + 32);
    uint64_t next64 = (uint64_t)pos + 46 + nameLen + extraLen + commentLen;
    if (!nameLen || nameLen > 255 || next64 > centralEnd) {
      zip.close();
      return setError("Invalid ZIP entry name or directory bounds.");
    }
    char nameBuf[256];
    if (!zipReadAt(zip, pos + 46, (uint8_t *)nameBuf, nameLen)) {
      zip.close();
      return setError("Could not read ZIP entry name.");
    }
    nameBuf[nameLen] = '\0';
    String name(nameBuf);
    pos = (uint32_t)next64;

    // Reject absolute paths, drive paths, backslashes, and parent traversal.
    if (name.startsWith("/") || name.indexOf('\\') >= 0 || name.indexOf(':') >= 0) {
      zip.close();
      return setError("Unsafe path in ZIP entry: " + name);
    }
    bool directory = name.endsWith("/");
    if (directory) name.remove(name.length() - 1);
    int segmentStart = 0;
    while (segmentStart < name.length()) {
      int slash = name.indexOf('/', segmentStart);
      int segmentEnd = slash < 0 ? name.length() : slash;
      String segment = name.substring(segmentStart, segmentEnd);
      if (!segment.length() || segment == "." || segment == "..") {
        zip.close();
        return setError("Unsafe path in ZIP entry: " + name);
      }
      if (slash < 0) break;
      segmentStart = slash + 1;
    }
    if (!name.length()) { zip.close(); return setError("Empty ZIP entry path."); }
    String outPath = destinationFolder + "/" + name;
    if (directory) {
      if (!createFolder(outPath)) { zip.close(); return false; }
      continue;
    }
    if ((flags & 1) || (method != 0 && method != 8) ||
        (method == 0 && packedSize != unpackedSize)) {
      zip.close();
      return setError("Only unencrypted ZIP Store and DEFLATE entries are supported: " + name);
    }
    if (_fs.exists(outPath)) {
      zip.close();
      return setError("Extraction would overwrite an existing path: " + outPath);
    }
    if (localOffset > centralOffset || centralOffset - localOffset < 30) {
      zip.close();
      return setError("Invalid local ZIP header: " + name);
    }
    uint8_t local[30];
    if (!zipReadAt(zip, localOffset, local, sizeof(local)) ||
        zip32(local) != 0x04034b50UL || zip16(local + 8) != method) {
      zip.close();
      return setError("Invalid local ZIP header: " + name);
    }
    uint64_t dataOffset64 = (uint64_t)localOffset + 30 + zip16(local + 26) + zip16(local + 28);
    if (dataOffset64 > centralOffset || packedSize > centralOffset - dataOffset64) {
      zip.close();
      return setError("ZIP entry data is out of bounds: " + name);
    }
    int slash = outPath.lastIndexOf('/');
    if (slash > 0 && !createFolder(outPath.substring(0, slash))) { zip.close(); return false; }
    File out = _fs.open(outPath, FILE_WRITE);
    if (!out) { zip.close(); return setError("Could not create extracted file: " + outPath); }
    bool ok = zip.seek((uint32_t)dataOffset64);
    uint32_t remaining = packedSize, crc = 0xffffffffUL, producedTotal = 0;
    if (method == 0) {
      while (ok && remaining) {
        size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        int got = zip.read(buffer, wanted);
        if (got <= 0 || out.write(buffer, (size_t)got) != (size_t)got) { ok = false; break; }
        crc = crcUpdate(crc, buffer, (size_t)got);
        remaining -= (uint32_t)got;
        producedTotal += (uint32_t)got;
        delay(1);
      }
    } else {
      mz_stream stream = {};
      bool streamReady = mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) == MZ_OK;
      if (!streamReady) ok = false;
      uint8_t compressedInput[512];
      int status = MZ_OK;
      while (ok && status != MZ_STREAM_END) {
        if (!stream.avail_in && remaining) {
          size_t wanted = remaining < sizeof(compressedInput) ? remaining : sizeof(compressedInput);
          int got = zip.read(compressedInput, wanted);
          if (got <= 0) { ok = false; break; }
          remaining -= (uint32_t)got;
          stream.next_in = compressedInput;
          stream.avail_in = (unsigned int)got;
        }
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        // Keep streaming even after the final input chunk: MZ_FINISH assumes
        // the output buffer can hold all remaining output in one call.
        status = mz_inflate(&stream, MZ_NO_FLUSH);
        size_t made = sizeof(buffer) - stream.avail_out;
        if (made) {
          if (out.write(buffer, made) != made) { ok = false; break; }
          crc = crcUpdate(crc, buffer, made);
          producedTotal += (uint32_t)made;
        }
        if (status != MZ_OK && status != MZ_STREAM_END) { ok = false; break; }
        if (status == MZ_OK && !made && !stream.avail_in && !remaining) { ok = false; break; }
        delay(1);
      }
      if (stream.total_in != packedSize) ok = false;
      if (streamReady) mz_inflateEnd(&stream);
    }
    out.flush();
    bool writeOk = out.getWriteError() == 0;
    out.close();
    crc ^= 0xffffffffUL;
    if (!ok || remaining || producedTotal != unpackedSize || !writeOk || crc != expectedCrc) {
      _fs.remove(outPath);
      zip.close();
      String reason = !ok ? "data stream/read error" :
                      remaining ? "unused compressed data remains" :
                      producedTotal != unpackedSize ? "extracted size mismatch" :
                      !writeOk ? "SD write error" : "CRC mismatch";
      return setError("Extraction failed (" + reason + "): " + name);
    }
  }
  zip.close();
  if (pos != centralEnd) return setError("ZIP central directory size did not match its entries.");
  _lastError = "";
  return true;
}

static bool addZipFile(fs::FS &fs, File &zip, const String &source,
                       const String &entryName, std::vector<ZipEntryInfo> &entries,
                       bool compress, int compressionLevel) {
  if (!entryName.length() || entryName.length() > 255 || entries.size() >= 65535) return false;
  File in = fs.open(source, FILE_READ);
  if (!in || in.isDirectory()) { if (in) in.close(); return false; }
  if ((uint64_t)in.size() > 0xffffffffUL) { in.close(); return false; }
  uint32_t size = (uint32_t)in.size(), offset = (uint32_t)zip.position();
  uint16_t date, time; dosDateTime(date, time);
  uint16_t method = compress ? 8 : 0, nameLength = (uint16_t)entryName.length();
  put32(zip, 0x04034b50); put16(zip, 20); put16(zip, 0); put16(zip, method);
  put16(zip, time); put16(zip, date); put32(zip, 0); put32(zip, 0); put32(zip, size);
  put16(zip, nameLength); put16(zip, 0);
  if (zip.write((const uint8_t *)entryName.c_str(), nameLength) != nameLength) { in.close(); return false; }
  uint8_t input[512], output[512]; uint32_t crc = 0xffffffffUL, copied = 0;
  mz_stream stream = {}; bool streamReady = false, ok = true;
  if (compress) {
    stream.zalloc = zipStateAlloc;
    stream.zfree = zipStateFree;
    int initStatus = (compressionLevel < 1 || compressionLevel > 9)
                         ? MZ_PARAM_ERROR
                         : mz_deflateInit2(&stream, compressionLevel, MZ_DEFLATED,
                                           -MZ_DEFAULT_WINDOW_BITS, 8, MZ_DEFAULT_STRATEGY);
    if (initStatus != MZ_OK) {
      zipBuildError = initStatus == MZ_MEM_ERROR
                          ? "Not enough heap for DEFLATE compressor state (free heap: " +
                                String(ESP.getFreeHeap()) + " bytes)."
                          : "Could not initialize DEFLATE compressor (status " + String(initStatus) + ").";
      in.close(); return false;
    }
    streamReady = true;
  }
  while (copied < size) {
    size_t wanted = (size - copied) < sizeof(input) ? size - copied : sizeof(input);
    int got = in.read(input, wanted);
    if (got <= 0) { ok = false; break; }
    copied += (uint32_t)got; crc = crcUpdate(crc, input, (size_t)got);
    if (!compress) {
      if (zip.write(input, (size_t)got) != (size_t)got) { ok = false; break; }
    } else {
      stream.next_in = input; stream.avail_in = (unsigned int)got;
      while (stream.avail_in) {
        unsigned int before = stream.avail_in;
        stream.next_out = output; stream.avail_out = sizeof(output);
        int status = mz_deflate(&stream, MZ_NO_FLUSH);
        size_t produced = sizeof(output) - stream.avail_out;
        if (status != MZ_OK || (produced && zip.write(output, produced) != produced) ||
            (before == stream.avail_in && !produced)) {
          zipBuildError = "DEFLATE stream failed (status " + String(status) + ").";
          ok = false; break;
        }
      }
      if (!ok) break;
    }
    delay(1);
  }
  if (copied != size) ok = false;
  if (compress && ok) {
    int status;
    do {
      stream.next_in = NULL; stream.avail_in = 0;
      stream.next_out = output; stream.avail_out = sizeof(output);
      status = mz_deflate(&stream, MZ_FINISH);
      size_t produced = sizeof(output) - stream.avail_out;
      if (produced && zip.write(output, produced) != produced) {
        zipBuildError = "Could not write compressed data to the SD card.";
        ok = false; break;
      }
    } while (status == MZ_OK);
    if (status != MZ_STREAM_END) {
      if (!zipBuildError.length()) zipBuildError = "DEFLATE finalization failed (status " + String(status) + ").";
      ok = false;
    }
  }
  in.close(); if (streamReady) mz_deflateEnd(&stream);
  if (!ok || (uint64_t)stream.total_out > 0xffffffffUL) return false;
  crc ^= 0xffffffffUL;
  uint32_t compressedSize = compress ? (uint32_t)stream.total_out : size;
  uint32_t end = (uint32_t)zip.position();
  if (!zip.seek(offset + 6)) return false;
  put16(zip, 0); put16(zip, method); put16(zip, time); put16(zip, date);
  put32(zip, crc); put32(zip, compressedSize); put32(zip, size);
  if (!zip.seek(end)) return false;
  entries.push_back({entryName, crc, size, compressedSize, offset, method});
  return zip.getWriteError() == 0;
}

static bool addZipFolder(fs::FS &fs, File &zip, const String &folder,
                         const String &relative, std::vector<ZipEntryInfo> &entries,
                         bool compress, int compressionLevel) {
  File dir = fs.open(folder);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return false; }
  std::vector<String> names; std::vector<bool> isDirs;
  while (true) {
    File e = dir.openNextFile(); if (!e) break;
    names.push_back(baseName(String(e.name()))); isDirs.push_back(e.isDirectory()); e.close();
  }
  dir.close();
  for (size_t i = 0; i < names.size(); ++i) {
    if (!names[i].length() || names[i] == "." || names[i] == "..") continue;
    String full = folder + "/" + names[i];
    String rel = relative.length() ? relative + "/" + names[i] : names[i];
    if (isDirs[i]) {
      if (!addZipFolder(fs, zip, full, rel, entries, compress, compressionLevel)) return false;
    } else if (!addZipFile(fs, zip, full, rel, entries, compress, compressionLevel)) return false;
  }
  return true;
}

static bool writeCentral(File &zip, const std::vector<ZipEntryInfo> &entries) {
  uint32_t start = (uint32_t)zip.position(); uint16_t date, time; dosDateTime(date, time);
  for (const auto &e : entries) {
    uint16_t n = (uint16_t)e.name.length();
    put32(zip, 0x02014b50); put16(zip, 20); put16(zip, 20); put16(zip, 0); put16(zip, e.method);
    put16(zip, time); put16(zip, date); put32(zip, e.crc); put32(zip, e.compressedSize); put32(zip, e.size);
    put16(zip, n); put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0); put32(zip, 0); put32(zip, e.offset);
    zip.write((const uint8_t *)e.name.c_str(), n);
  }
  uint32_t end = (uint32_t)zip.position();
  put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0);
  put16(zip, (uint16_t)entries.size()); put16(zip, (uint16_t)entries.size());
  put32(zip, end - start); put32(zip, start); put16(zip, 0); zip.flush();
  return zip.getWriteError() == 0;
}

static bool verifyZip(fs::FS &fs, const String &path, uint16_t expected) {
  File f = fs.open(path, FILE_READ); if (!f || f.size() < 22) { if (f) f.close(); return false; }
  size_t len = f.size(), start = len > 65557 ? len - 65557 : 0; bool found = false; uint16_t count = 0;
  for (size_t p = len - 22;; --p) {
    if (!f.seek(p)) break; uint8_t s[4]; if (f.read(s, 4) != 4) break;
    if (s[0] == 0x50 && s[1] == 0x4b && s[2] == 0x05 && s[3] == 0x06) {
      if (!f.seek(p + 10)) break; uint8_t b[2]; if (f.read(b, 2) != 2) break;
      count = (uint16_t)(b[0] | (b[1] << 8)); found = true; break;
    }
    if (p == start) break;
  }
  f.close(); return found && count == expected;
}
bool ESP32StorageLibrary::archiveFolder(const String &sourceFolder, const String &destinationZip) {
  return archiveFolderInternal(sourceFolder, destinationZip, false, 0);
}

bool ESP32StorageLibrary::archiveFolderCompressed(const String &sourceFolder,
                                                   const String &destinationZip,
                                                   int compressionLevel) {
  struct TaskContext {
    ESP32StorageLibrary *storage;
    String source;
    String destination;
    int level;
    SemaphoreHandle_t done;
    bool result;
    String error;
  } context;

  context.storage = this;
  context.source = sourceFolder;
  context.destination = destinationZip;
  context.level = compressionLevel;
  context.done = xSemaphoreCreateBinary();
  context.result = false;
  if (!context.done) return setError("Could not allocate completion signal for ZIP task.");

  auto worker = [](void *argument) {
    TaskContext *task = (TaskContext *)argument;
    task->result = task->storage->archiveFolderInternal(
        task->source, task->destination, true, task->level);
    task->error = task->storage->lastError();
    xSemaphoreGive(task->done);
    vTaskDelete(NULL);
  };

  // miniz's DEFLATE code uses more stack than Arduino's default loop task.
  if (xTaskCreate(worker, "sdZipDeflate", 24 * 1024, &context,
                  tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    vSemaphoreDelete(context.done);
    return setError("Could not start ZIP task; not enough free heap for its stack.");
  }

  xSemaphoreTake(context.done, portMAX_DELAY);
  vSemaphoreDelete(context.done);
  _lastError = context.error;
  return context.result;
}

bool ESP32StorageLibrary::archiveFolderInternal(const String &sourceFolder,
                                                 const String &destinationZip,
                                                 bool compress, int compressionLevel) {
  if (!_fs.exists(sourceFolder)) return setError("Source folder does not exist: " + sourceFolder);
  if (_fs.exists(destinationZip)) return setError("Archive already exists; source kept: " + destinationZip);
  String temp = destinationZip + ".tmp";
  if (_fs.exists(temp)) _fs.remove(temp);
  File zip = _fs.open(temp, FILE_WRITE);
  if (!zip) return setError("Could not create temporary ZIP: " + temp);
  zipBuildError = "";
  std::vector<ZipEntryInfo> entries;
  bool ok = addZipFolder(_fs, zip, sourceFolder, "", entries, compress, compressionLevel) && !entries.empty();
  if (ok) ok = writeCentral(zip, entries);
  zip.flush(); bool writeOk = zip.getWriteError() == 0; zip.close(); ok = ok && writeOk;
  if (!ok || !verifyZip(_fs, temp, (uint16_t)entries.size())) {
    _fs.remove(temp);
    if (zipBuildError.length()) return setError(zipBuildError + " Temporary ZIP removed; original folder kept.");
    return setError("ZIP creation/validation failed; temporary ZIP removed; original folder kept.");
  }
  if (!_fs.rename(temp, destinationZip)) return setError("ZIP validated but rename failed; original folder kept.");
  if (!deleteFolder(sourceFolder)) return setError("ZIP created, but source folder could not be fully deleted.");
  _lastError = ""; return true;
}
