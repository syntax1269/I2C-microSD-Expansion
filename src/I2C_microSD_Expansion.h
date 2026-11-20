#ifndef I2C_MICROSD_EXPANSION_H
#define I2C_MICROSD_EXPANSION_H
// I2C_microSD_Expansion.h — public header for the I²C microSD Expansion
// Arduino host library. Zero heap, fixed buffers, no String, no Serial.
// One open file at a time by design. Not reentrant or interrupt-safe: use one
// instance from one call site; the single file handle and shared staging
// buffer are not protected against nested calls.
#include <Arduino.h>
#include "protocol.h"
#include "I2C_microSD_Expansion_Config.h"

#if defined(Tinyi2cStorage)
#include <TinyWireM.h>
#else
#include <Wire.h>
#endif

// Card/volume snapshot returned by cardInfo().
typedef CardInfoFrame CardInfo;

// Normalized error codes returned by lastError().
typedef enum {
  MICROSDEXP_OK = 0,          // No error
  MICROSDEXP_NO_MODULE,       // module not responding on I2C
  MICROSDEXP_NO_FILE,         // No file open for read/write
  MICROSDEXP_OPEN_FAIL,       // File open was rejected
  MICROSDEXP_CARD_ERR,        // SD card or filesystem error
  MICROSDEXP_QUEUE_FULL,      // module request queue full
  MICROSDEXP_INVALID,         // Invalid argument (bad path, etc.)
  MICROSDEXP_OVERFLOW,        // module RX buffers full
  MICROSDEXP_TIMEOUT          // Operation timed out
} MicroSDExpError;

// File handle returned by openFile(). One active file per I2CmicroSD instance.
// Declared before the host class so it can be held as a per-instance member.
class I2CmicroSD;  // forward declaration for the owner pointer

struct MicroSDExpFile {
  uint8_t  open;
  uint8_t  error;
  uint8_t  mode;  // 0 = "r", 1 = "w", 2 = "a"
  uint8_t  _pad;
  uint16_t len;
  int8_t   dummy;  // alignment padding
  I2CmicroSD *microSDexp;
};

// ---- Non-blocking API types ----------------------------------------------
// A token returned by a begin*() call; pass it to pollOp() from loop().
struct MicroSDExpOp {
  uint8_t doneBefore;   // opDone captured before the command was submitted
  uint8_t kind;         // one of MICROSDEXP_OP_* (informational)
  uint8_t reserved[2];
};

typedef enum {
  MICROSDEXP_OP_NONE = 0,
  MICROSDEXP_OP_OPEN_WRITE,
  MICROSDEXP_OP_OPEN_READ,
  MICROSDEXP_OP_CLOSE,
  MICROSDEXP_OP_FORMAT,
  MICROSDEXP_OP_QUERY
} MicroSDExpOpKind;

// pollOp() result. 0 = completed OK, 1..6 = module error (MICROSDEXP_ERR_*),
// -1 = still busy, -2 = STATUS read failed (I2C error).
typedef enum {
  MICROSDEXP_ASYNC_IOERR = -2,
  MICROSDEXP_ASYNC_BUSY = -1,
  MICROSDEXP_ASYNC_OK = 0
} MicroSDExpAsync;

// Main host class. One instance per module on the I2C bus. By default every
// instance owns its file handle and scratch buffer, so multiple modules can be
// driven independently. Define I2CMICROSD_GLOBAL_STATE=1 for the legacy single
// shared handle/buffer (smallest RAM, single-instance builds only).
class I2CmicroSD {
 public:
  I2CmicroSD();

  // Probe the module at the given I2C address and clock speed.
  // Returns true when the module answers STATUS.
  bool begin(uint8_t address = I2CMICROSD_DEFAULT_ADDRESS, uint32_t clockHz = 400000);
  uint8_t address() const;  // address actually bound by begin() (incl. fallback)
  void setPollInterval(uint16_t ms);
  // Bytes requested per READ drain, clamped to 1..I2CMICROSD_READ_CHUNK_MAX.
  // Raise to 256 only after the host Wire receive buffer is enlarged
  // (e.g. Wire.setBufferSize(256) before Wire.begin()) and the caller's
  // buffer holds a full chunk. Defaults to I2CMICROSD_READ_CHUNK_BYTES.
  void setReadChunkBytes(size_t n);
  size_t readChunkBytes() const;

  // ---- Low-level transport (advanced use) ----
  // Best-effort recovery for a stuck bus. Sends 9 general-call writes to
  // address 0x00, which other general-call-capable slaves may respond to;
  // cores that reject 0x00 simply report errors. NOT called by begin().
  // A true 9-clock + STOP recovery needs raw SDA/SCL pin control that Wire
  // does not expose. Call only when you know the bus is stuck.
  void recoverBus();
  bool readStatus(StatusFrame &out);
  int waitForOpComplete(uint8_t doneBefore, uint32_t timeoutMs = I2CMICROSD_OP_TIMEOUT_MS);
  bool waitForCommitted(uint32_t target, uint32_t timeoutMs = I2CMICROSD_COMMIT_TIMEOUT_MS);

  // ---- Async file core ----
  bool openWrite(const char *path);   // create/truncate and open for write
  bool openRead(const char *path);    // open for read
  bool openAppend(const char *path);  // open for append (atomic per chunk)
  size_t write(const uint8_t *buf, size_t len);
  size_t read(uint8_t *buf, size_t len);
  bool sync();                        // wait until all writes are committed to card
  bool close();                       // close the open file

  // ---- Directory and query operations ----
#ifndef I2CSDMODULE_MINIMAL
  bool mkdir(const char *path);       // create directory
  bool rmdir(const char *path);       // remove empty directory
  bool remove(const char *path);      // delete file
#endif
  bool exists(const char *path);      // true if path exists
  // Checked variant: return true only when the command succeeded; the answer
  // is written through `out`. Use when "not found" must be distinct from an
  // I2C/card error. The one-argument form keeps its sentinel behavior.
  bool exists(const char *path, bool *out);
  uint32_t fileSize(const char *path); // size in bytes, 0 if not found
  bool fileSize(const char *path, uint32_t *out);  // checked variant

#if !defined(Tinyi2cStorage) && !defined(I2CSDMODULE_MINIMAL)
  bool isDirectory(const char *path);                 // true if path is a dir
  bool isDirectory(const char *path, bool *out);      // checked variant
  // List a directory into `out` (max `outCap` entries). A return value equal
  // to `outCap` means POSSIBLY TRUNCATED, not an exact fit — raise outCap or
  // page through the directory when you need to know.
  uint8_t listDir(const char *path, DirEntry *out, uint8_t outCap);
  uint8_t version(char *buf, uint8_t buflen);         // version; 0 + INVALID if buffer too small
  uint8_t firmwareBanner(char *buf, uint8_t buflen);  // full CMD_VERSION answer (banner on >=18.8)
  uint32_t firmwareClockHz();                         // measured clock from banner (0 when absent)
  uint16_t temperatureK();                            // whole kelvin, 0xFFFF on error
  uint16_t adcPA5();                                  // raw 12-bit ADC; 0xFFFF on error/n/a
  bool cardInfo(CardInfo *out);                       // card/volume snapshot
  void setClockTime(uint8_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);
  void reboot();                                      // reboot the module
  bool format(uint8_t mode = 0);                      // format card (destructive)
#elif defined(I2CSDMODULE_MINIMAL)
  bool isDirectory(const char *path);
  bool isDirectory(const char *path, bool *out);
#endif

  // ---- NeoPixel (WS2812B) output ----
  // The module bit-bangs a data pin to drive a WS2812B strip. Frames are
  // pushed as raw GRB bytes (one byte per channel, green first).
  bool bitbangSetMode(uint8_t pin);                                  // CMD_BITBANG_SET_MODE
  bool bitbangBuffer(uint16_t offset, const uint8_t *data, uint8_t len); // CMD_BITBANG_BUFFER
  bool bitbangExecute(uint8_t profile);                              // CMD_BITBANG_EXECUTE
  void bitbangClear();                                               // CMD_BITBANG_CLEAR
  // Convenience: push a whole GRB frame and latch it in one call.
  bool showNeoPixels(uint8_t pin, const uint8_t *grb, uint16_t pixelCount);
  // Expansion-pin control (digital out / PWM on the module's spare pins).
  bool setPinMode(uint8_t pin, uint8_t mode);
  bool writePin(uint8_t pin, uint8_t value);

  // Persist the current expansion-pin modes + I2C address to the module's
  // EEPROM. Pin modes take effect immediately via setPinMode() (in RAM only);
  // this writes them to EEPROM so they survive a power loss / reset. The
  // command is explicit-only: the library never persists on its own, so call
  // savePINMode() once when configuration is final (e.g. at teardown) to avoid
  // wearing the EEPROM on dev-loop churn. A redundant call is a no-op when
  // nothing changed since the last save.
  void savePINMode();

  MicroSDExpError lastError() const;   // result of the most recent call
  bool lastCallOk() const { return _lastError == MICROSDEXP_OK; }
  MicroSDExpError stickyError() const; // first failure since clearError()
  void clearError();                   // reset lastError() and stickyError()
  // One-page health report for troubleshooting. Writes to any Print stream
  // (Serial, a file, ...): no String, no Serial dependency. Safe to call even
  // when begin() failed — fields that could not be read show up as n/a.
  void diagnostics(Print &out);
  bool fileOpen() const;
  // Handle used by openFile()/fileWrite()/fileRead()/fileClose(). Points at
  // this instance's active file (the shared handle in GLOBAL_STATE builds).
  MicroSDExpFile *activeFile();
#if !I2CMICROSD_NO_ASYNC
  // ---- Non-blocking API (never sleeps; call from loop) --------------------
  // Each begin*() submits one command and returns immediately; pollOp() does
  // a single STATUS read. Only one async operation may be pending at a time.
  bool beginOpenWrite(const char *path, MicroSDExpOp &op);
  bool beginOpenRead(const char *path, MicroSDExpOp &op);
  bool beginClose(MicroSDExpOp &op);
  bool beginFormat(uint8_t mode, MicroSDExpOp &op);
  bool beginQuery(uint8_t cmd, const char *path, MicroSDExpOp &op);
  int  pollOp(const MicroSDExpOp &op);
  // Move only what the module can accept right now (no waiting).
  size_t writeNow(const uint8_t *buf, size_t len);
  size_t readNow(uint8_t *buf, size_t len);
  // Committed byte count from one STATUS read (compare with expected target).
  uint32_t committedBytes();
#endif

 private:
  bool readOpDone(uint8_t &opDone);
#if !I2CMICROSD_NO_ASYNC
  bool submitOpenCmd(uint8_t cmd, const char *path, MicroSDExpOp &op, uint8_t kind);
  bool _asyncPending;
#endif
  uint8_t  _addr;
  uint32_t _sclHz;
  uint16_t _pollMs;
  uint16_t _flowPollMs;
  size_t   _readChunk;
  // Last result plus the first failure since clearError(). Internal
  // calls can overwrite lastError(), but cannot hide a real fault here.
  struct ErrorSlot {
    MicroSDExpError last;
    MicroSDExpError sticky;
    explicit ErrorSlot(MicroSDExpError e) : last(e), sticky(e) {}
    ErrorSlot &operator=(MicroSDExpError e) {
      last = e;
      if (e != MICROSDEXP_OK) sticky = e;
      return *this;
    }
    operator MicroSDExpError() const { return last; }
  };
  ErrorSlot _lastError;
  bool _fileOpen;
#if I2CMICROSD_GLOBAL_STATE
  // Legacy: one handle + one scratch buffer for all instances (smallest RAM).
  static uint8_t _scratch[I2CMICROSD_SCRATCH_BYTES];
  static MicroSDExpFile _activeFile;
#else
  // Per-instance state: multiple modules stay independent.
  uint8_t _scratch[I2CMICROSD_SCRATCH_BYTES];
  MicroSDExpFile _activeFile;
#endif
  bool _appendMode;
  bool _readStarted;
  bool _wireBegun;
  uint8_t _wireErrLast;
  uint32_t _baseWritten;
  uint32_t _written;
  uint32_t _lastActiveMs;
  char _appendPath[I2CMICROSD_PATH_MAX];
  // Last mode applied per expansion pin (0xFF = "none yet"), so a redundant
  // setPinMode(pin, same) is a library-side no-op: no bus transaction, no
  // module EEPROM write. Indexed by MICROSDEXP_PIN_* value.
  uint8_t _pinLastMode[8];

  uint8_t sendCmd(uint8_t cmd, const uint8_t *data, size_t len, bool stop);
  size_t receiveBytes(uint8_t *buf, size_t len);
  uint32_t scaleTimeout(uint32_t base) const;
  MicroSDExpError normalizeWire(uint8_t wireErr) const;
  bool barrierCloseFlush();
  void barrierIfGap();
  bool pushWriteBlock(const uint8_t *data, size_t len);
  bool submitFilename(const char *path);
  int legacyOp(uint8_t cmd, const char *path, uint32_t *resultOut);
  bool validPath(const char *path);
  bool appendChunk(const uint8_t *data, size_t len);
};

// C-style file API (mirrors Arduino File pattern).
extern "C" {
MicroSDExpFile *openFile(I2CmicroSD *b, const char *path, const char *mode);  // "r" | "w" | "a"
size_t fileWrite(MicroSDExpFile *f, const uint8_t *buf, size_t len);
size_t fileRead(MicroSDExpFile *f, uint8_t *buf, size_t len);
bool fileSync(MicroSDExpFile *f);
bool fileClose(MicroSDExpFile *f);
}

#endif  // I2C_MICROSD_EXPANSION_H


