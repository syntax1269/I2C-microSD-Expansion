// I2C_microSD_Expansion.cpp — protocol core for the I²C microSD Expansion
// Arduino host library. Transport: repeated-START reads, flow-paced writes,
// CLOSE_FLUSH barrier on open after any gap. Zero heap, fixed buffers,
// LE via shifts. Library never calls Serial — sketch owns output.
#include "I2C_microSD_Expansion.h"

#if defined(Tinyi2cStorage)
#define I2C_BUS TinyWireM
#else
#define I2C_BUS Wire
#endif

// Single active file handle (module is single-file; no heap).
static MicroSDExpFile gActiveFile;

// Shared staging buffer for all transient frames. The module is single-file
// and no two transports run concurrently, so readStatus/CardInfo/listDir
// drain can share one buffer instead of each allocating 12–128 B on the
// stack. Library never calls Serial (sketch owns output) — pure wrapper.
#if defined(__AVR__) || defined(Tinyi2cStorage)
static uint8_t g_shared[32];
#else
static uint8_t g_shared[128];
#endif

// Cheap dotted-token test for the CMD_VERSION answer; avoids string.h.
static bool hasVersionToken(const char *s) {
  bool dot = false;
  bool digit = false;
  for (; *s; ++s) {
    if (*s == '.') dot = true;
    if (*s >= '0' && *s <= '9') digit = true;
  }
  return dot && digit;
}

I2CmicroSD::I2CmicroSD()
    : _addr(I2CMICROSD_DEFAULT_ADDRESS),
      _sclHz(0),
      _pollMs(I2CMICROSD_POLL_MS),
      _flowPollMs(I2CMICROSD_FLOW_POLL_MS),
      _readChunk(I2CMICROSD_READ_CHUNK_BYTES),
      _lastError(MICROSDEXP_OK),
      _fileOpen(false),
      _appendMode(false),
      _readStarted(false),
      _wireBegun(false),
      _wireErrLast(0),
      _baseWritten(0),
      _written(0),
      _lastActiveMs(0) {
  for (int i = 0; i < 8; i++) {
    _pinLastMode[i] = 0xFF;  // 0xFF = "unknown"; first real apply is never a no-op
  }
}

uint8_t I2CmicroSD::address() const {
  return _addr;
}

void I2CmicroSD::setPollInterval(uint16_t ms) {
  _pollMs = ms;
}

void I2CmicroSD::setReadChunkBytes(size_t n) {
  if (n == 0) n = 1;
  if (n > I2CMICROSD_READ_CHUNK_MAX) n = I2CMICROSD_READ_CHUNK_MAX;
  _readChunk = n;
}

size_t I2CmicroSD::readChunkBytes() const {
  return _readChunk;
}

MicroSDExpError I2CmicroSD::lastError() const {
  return _lastError;
}

MicroSDExpError I2CmicroSD::stickyError() const {
  return _lastError.sticky;
}

void I2CmicroSD::clearError() {
  _lastError = MICROSDEXP_OK;
  _lastError.sticky = MICROSDEXP_OK;
}

bool I2CmicroSD::fileOpen() const {
  return _fileOpen;
}

// ---- Transport ----------

uint8_t I2CmicroSD::sendCmd(uint8_t cmd, const uint8_t *data, size_t len, bool stop) {
  _lastActiveMs = millis();
  I2C_BUS.beginTransmission(_addr);
  I2C_BUS.write(cmd);
  for (size_t i = 0; i < len; i++) {
    I2C_BUS.write(data[i]);
  }
  uint8_t err = I2C_BUS.endTransmission(stop);
  _wireErrLast = err;
  _lastError = normalizeWire(err);
  return err;
}

size_t I2CmicroSD::receiveBytes(uint8_t *buf, size_t len) {
#if defined(Tinyi2cStorage)
  // TinyWireM.requestFrom(addr, qty) returns 0 on success (Wire returns count).
  // Cap each request at 16 B (USI buffer-safe, 16 B tiny chunk).
  size_t n = 0;
  while (n < len) {
    size_t remaining = len - n;
    uint8_t req = (remaining > 16) ? 16 : (uint8_t)remaining;
    uint8_t err = TinyWireM.requestFrom(_addr, req);
    if (err != 0) break;
    // Drain whatever is actually available.
    size_t before = n;
    while (TinyWireM.available() > 0) {
      if (n >= len) break;
      buf[n++] = (uint8_t)TinyWireM.read();
    }
    if (n == before) break;  // no progress (short bus response)
  }
  return n;
#else
  size_t got = I2C_BUS.requestFrom((int)_addr, (int)len);
  // Some cores (notably ESP32) clamp requestFrom() to the Wire receive
  // buffer and return 0 when the ask is larger than bufferSize. Fall back to
  // the always-safe 128 once so a mis-sized host degrades instead of
  // spinning until the read timeout.
  if (got == 0 && len > 128) {
    got = I2C_BUS.requestFrom((int)_addr, 128);
  }
  size_t n = 0;
  while (I2C_BUS.available() && n < len && n < got) {
    buf[n++] = (uint8_t)I2C_BUS.read();
  }
  return n;
#endif
}

// Wire error normalization. AVR/Pico/SAMD/esp core codes:
// 0=OK, 1=data too long, 2=NACK address, 3=NACK data, 4=other, 5=bus timeout.
MicroSDExpError I2CmicroSD::normalizeWire(uint8_t wireErr) const {
  switch (wireErr) {
    case 0:
      return MICROSDEXP_OK;
    case 2:
      return MICROSDEXP_NO_MODULE;  // NACK on address
    case 3:
      return MICROSDEXP_INVALID;    // NACK on data (command rejected / no space)
    case 1:
      return MICROSDEXP_OVERFLOW;   // data too long
    case 5:
      return MICROSDEXP_TIMEOUT;    // bus timeout
    case 4:
      return MICROSDEXP_CARD_ERR;   // other bus error
    default:
      return MICROSDEXP_CARD_ERR;
  }
}

// Timeout scaling: base x 100000/sclHz, but never below base.
// Worker time (SPI 5 MHz, FAT mount) does not track I2C clock: at 1 MHz the
// host still waits 15 s fixed. Scale *up* only below 100 kHz; below 50 kHz
// Sm force >=2x base, cap 120 s.
uint32_t I2CmicroSD::scaleTimeout(uint32_t base) const {
  if (_sclHz == 0) return base;
  uint64_t scaled = (uint64_t)base * 100000UL / (uint64_t)_sclHz;
  if (scaled < (uint64_t)base) scaled = (uint64_t)base;
  if (_sclHz < 50000UL) {
    uint64_t dbl = (uint64_t)base << 1;
    if (scaled < dbl) scaled = dbl;
  }
  if (scaled > 120000UL) scaled = 120000UL;
  return (uint32_t)scaled;
}

// Best-effort bus recovery. NOT called by begin(): these are I2C general-call
// writes to 0x00, which other slaves may answer and some cores reject outright.
// A real 9-clock + STOP recovery needs direct SDA/SCL pin control that Wire
// cannot express; begin()'s read-only address probe already clears the common
// stuck-bus case. Call this explicitly only when you know the bus is stuck.
void I2CmicroSD::recoverBus() {
  for (uint8_t i = 0; i < 9; i++) {
    I2C_BUS.beginTransmission(0x00);
    I2C_BUS.endTransmission(true);
  }
}

bool I2CmicroSD::begin(uint8_t address, uint32_t clockHz) {
  _addr = address;
  _sclHz = clockHz;

  // A re-begin may follow a module power cycle or reconnect: the module's RAM
  // pin modes are unknown again, so invalidate the dedup cache.
  for (int i = 0; i < 8; i++) {
    _pinLastMode[i] = 0xFF;
  }

  if (!_wireBegun) {
    // Pin selection is owned by the sketch: call Wire.begin(sda,scl) before
    // microSDexp.begin() on boards that differ from the core defaults (e.g.
    // ESP-01S SDA0/SCL2). Core Wire preserves those pins on re-begin;
    // this fallback keeps default-pin boards (ESP32 21/22, Uno, ...) working.
    I2C_BUS.begin();
    _wireBegun = true;
  }
  // Tiny: TinyWireM has no setClock in v1.1.4 (USI fixed); normal: use it.
#if !defined(Tinyi2cStorage)
  if (clockHz != 0) {
    I2C_BUS.setClock(clockHz);
  }
#endif

  // Address scan probe: configured address first, then 0x6A-0x6F (I2C_SET_ADDR
  // live range per wiring doc). Keeps sketches honest about the default 0x6E.
  StatusFrame st;
  if (readStatus(st)) {
    _lastError = MICROSDEXP_OK;
    return true;
  }
  // Fallback for re-addressed modules only. A bare 12-byte STATUS reply is not
  // enough to adopt an address: require a rev-2 STATUS frame with bounded
  // state/error, and (in builds with the version API) a dotted version token.
  for (uint8_t addr = 0x6A; addr <= 0x6F; addr++) {
    if (addr == address) continue;
    uint8_t saved = _addr;
    _addr = addr;
    bool plausible = readStatus(st) && st.rev == 2 &&
                     st.state <= MICROSDEXP_ST_OVERFLOW &&
                     st.error <= MICROSDEXP_ERR_OVERFLOW;
#if !defined(Tinyi2cStorage) && !defined(I2CSDMODULE_MINIMAL)
    if (plausible) {
      char ver[I2CMICROSD_VERSION_BANNER_BYTES];
      plausible = version(ver, sizeof(ver)) > 0 && hasVersionToken(ver);
    }
#endif
    if (plausible) {
      _lastError = MICROSDEXP_OK;
      return true;                     // _addr stays on the confirmed address
    }
    _addr = saved;
  }
  _lastError = MICROSDEXP_NO_MODULE;
  return false;
}

bool I2CmicroSD::readStatus(StatusFrame &out) {
  // Repeated-START: endTransmission(false) then requestFrom (never STOP
  // between command and read — module discards command on STOP, see fw v17).
  uint8_t err = sendCmd(MICROSDEXP_CMD_STATUS, nullptr, 0, false);
  if (err != 0) return false;
  size_t n = receiveBytes(g_shared, 12);
  if (n < 12) return false;
  out.rev = g_shared[0];
  out.state = g_shared[1];
  out.opDone = g_shared[2];
  out.error = g_shared[3];
  out.rxFree = (uint16_t)g_shared[4] | ((uint16_t)g_shared[5] << 8);
  out.txAvail = (uint16_t)g_shared[6] | ((uint16_t)g_shared[7] << 8);
  out.result = (uint32_t)g_shared[8] | ((uint32_t)g_shared[9] << 8) |
               ((uint32_t)g_shared[10] << 16) | ((uint32_t)g_shared[11] << 24);
  return true;
}

// Bounded pre-read of opDone. Every wait loop captures the completion
// counter BEFORE submitting its command; a failed read must never fall back
// to 0, because any nonzero opDone would then look like a fresh completion.
bool I2CmicroSD::readOpDone(uint8_t &opDone) {
  StatusFrame st;
  for (int i = 0; i < 4; i++) {
    if (readStatus(st)) {
      opDone = st.opDone;
      return true;
    }
    if (i < 3) delay(_pollMs);
  }
  _lastError = MICROSDEXP_NO_MODULE;
  return false;
}
int I2CmicroSD::waitForOpComplete(uint8_t doneBefore, uint32_t timeoutMs) {
  uint32_t start = millis();
  uint32_t limit = scaleTimeout(timeoutMs);
  while ((millis() - start) < limit) {
    StatusFrame st;
    if (!readStatus(st)) {
      delay(_pollMs);
      continue;
    }
    if (st.opDone != doneBefore) {
      _lastError = MICROSDEXP_OK;
      return (int)st.error;  // 0 = COMPLETE
    }
    delay(_pollMs);
  }
  _lastError = MICROSDEXP_TIMEOUT;
  return -1;
}

bool I2CmicroSD::waitForCommitted(uint32_t target, uint32_t timeoutMs) {
  uint32_t start = millis();
  uint32_t limit = scaleTimeout(timeoutMs);
  while ((millis() - start) < limit) {
    StatusFrame st;
    if (readStatus(st)) {
      if (st.state == MICROSDEXP_ST_ERROR) {
        _lastError = MICROSDEXP_CARD_ERR;
        return false;
      }
      if (st.result >= target) {
        return true;
      }
    }
    delay(_pollMs);
  }
  _lastError = MICROSDEXP_TIMEOUT;
  return false;
}

// ---- Async file core ----------

bool I2CmicroSD::barrierCloseFlush() {
  uint8_t db = 0;
  if (!readOpDone(db)) return false;
  uint8_t err = sendCmd(MICROSDEXP_CMD_CLOSE_FLUSH, nullptr, 0, true);
  if (err != 0) return false;
  return waitForOpComplete(db, I2CMICROSD_OP_TIMEOUT_MS) == 0;
}

// Barrier only after a bus gap (abrupt host loss): if no successful I2C for >4 s the
// module idle-flushes and advances opDone, so the first wait would be
// mis-tracked. Normal back-to-back opens skip it (queue stays shallow at 1 MHz).
void I2CmicroSD::barrierIfGap() {
  uint32_t gap = millis() - _lastActiveMs;
  if (gap > 4000UL) (void)barrierCloseFlush();
}

// ---- Legacy helpers ----------

// Path table: the module's filesystem handles case/`/`/LFN; the library only
// length-checks (no silent truncation). Valid: "DIR/FILE", "/DIR", trailing
// "/" (directory), LFN <=259. Invalid: NULL/empty/overlong (no I2C sent).
// Tinyi2cStorage: strict 8.3 only (FILENAME.EXT or DIR/FILENAME.EXT, one '/'
// max, each component <=12 chars (8+dot+3), path <=24 incl. NUL=25).
bool I2CmicroSD::validPath(const char *path) {
  if (!path || path[0] == '\0') {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  size_t n = 0;
  while (path[n] != '\0') {
    n++;
    if (n >= (size_t)I2CMICROSD_PATH_MAX) {
      _lastError = MICROSDEXP_INVALID;
      return false;
    }
  }
#if defined(Tinyi2cStorage)
  // Strict 8.3, one '/' maximum, leading '/' rejected.
  if (path[0] == '/') {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  uint8_t slashes = 0;
  uint8_t compLen = 0;
  uint8_t dotSeen = 0;
  for (size_t i = 0; i < n; i++) {
    char c = path[i];
    if (c == '/') {
      if (compLen == 0) {
        _lastError = MICROSDEXP_INVALID;
        return false;  // empty component
      }
      slashes++;
      if (slashes > 1) {
        _lastError = MICROSDEXP_INVALID;
        return false;  // more than one '/'
      }
      compLen = 0;
      dotSeen = 0;
    } else if (c == '.') {
      compLen++;
      dotSeen++;
      if (compLen > 12 || dotSeen > 1) {
        _lastError = MICROSDEXP_INVALID;
        return false;
      }
    } else {
      compLen++;
      if (compLen > 12) {
        _lastError = MICROSDEXP_INVALID;
        return false;  // 8.3 component too long (8+dot+3)
      }
    }
  }
#endif
  return true;
}

// Store path via 0x01 (no worker op; consumed by the next op command).
bool I2CmicroSD::submitFilename(const char *path) {
  I2C_BUS.beginTransmission(_addr);
  I2C_BUS.write(MICROSDEXP_CMD_FILENAME);
  size_t i = 0;
  while (path[i] != '\0' && i < (size_t)(I2CMICROSD_PATH_MAX - 1)) {
    I2C_BUS.write((uint8_t)path[i]);
    i++;
  }
  I2C_BUS.write((uint8_t)0);
  uint8_t err = I2C_BUS.endTransmission(true);
  _wireErrLast = err;
  _lastError = normalizeWire(err);
  return err == 0;
}

// Legacy sync op: doneBefore -> 0x01 -> cmd ->
// waitForOpComplete -> STATUS.result latch read after completion.
int I2CmicroSD::legacyOp(uint8_t cmd, const char *path, uint32_t *resultOut) {
  uint8_t doneBefore = 0;
  if (!readOpDone(doneBefore)) { if (resultOut) *resultOut = 0; return 102; }
  if (!submitFilename(path)) return 100;
  if (sendCmd(cmd, nullptr, 0, true) != 0) return 101;
  int err = waitForOpComplete(doneBefore, I2CMICROSD_OP_TIMEOUT_MS);
  if (err < 0) {
    _lastError = MICROSDEXP_TIMEOUT;
    return err;
  }
  if (resultOut) {
    StatusFrame st;
    if (readStatus(st)) *resultOut = st.result;
    else *resultOut = 0;
  }
  if (err != 0) {
    _lastError = (err == MICROSDEXP_ERR_OPEN) ? MICROSDEXP_OPEN_FAIL : MICROSDEXP_CARD_ERR;
  } else {
    _lastError = MICROSDEXP_OK;
  }
  return err;
}

#ifndef I2CSDMODULE_MINIMAL
bool I2CmicroSD::mkdir(const char *path) {
  if (!validPath(path)) return false;
  return legacyOp(MICROSDEXP_CMD_MAKE_DIR, path, nullptr) == 0;
}

bool I2CmicroSD::rmdir(const char *path) {
  if (!validPath(path)) return false;
  return legacyOp(MICROSDEXP_CMD_REMOVE_DIR, path, nullptr) == 0;
}

bool I2CmicroSD::remove(const char *path) {
  if (!validPath(path)) return false;
  return legacyOp(MICROSDEXP_CMD_REMOVE_FILE, path, nullptr) == 0;
}
#endif

bool I2CmicroSD::exists(const char *path) {
  if (!validPath(path)) return false;
  uint32_t res = 0;
  if (legacyOp(MICROSDEXP_CMD_FILE_EXISTS, path, &res) != 0) return false;
  return res == 1;
}

#ifndef Tinyi2cStorage
bool I2CmicroSD::isDirectory(const char *path) {
  if (!validPath(path)) return false;
  uint32_t res = 0;
  if (legacyOp(MICROSDEXP_CMD_IS_DIRECTORY, path, &res) != 0) return false;
  return res == 1;
}
#endif

uint32_t I2CmicroSD::fileSize(const char *path) {
  if (!validPath(path)) return 0;
  uint32_t res = 0;
  if (legacyOp(MICROSDEXP_CMD_FILE_SIZE, path, &res) != 0) return 0;
  return res;
}

#if !defined(Tinyi2cStorage) && !defined(I2CSDMODULE_MINIMAL)
uint8_t I2CmicroSD::firmwareBanner(char *buf, uint8_t buflen) {
  if (!buf || buflen < 2) {
    _lastError = MICROSDEXP_INVALID;
    return 0;
  }
  // Firmware >= 18.8.0 answers with a 112-byte multi-line banner; older
  // firmware answers with the bare version string followed by NUL padding.
  size_t want = (size_t)buflen - 1;
  if (want > I2CMICROSD_VERSION_BANNER_BYTES) want = I2CMICROSD_VERSION_BANNER_BYTES;
  uint8_t err = sendCmd(MICROSDEXP_CMD_VERSION, nullptr, 0, false);
  if (err != 0) {
    buf[0] = '\0';
    return 0;
  }
  size_t n = receiveBytes((uint8_t *)buf, want);
  size_t out = 0;
  while (out < n && buf[out] != '\0') out++;
  buf[out] = '\0';
  _lastError = MICROSDEXP_OK;
  return (uint8_t)out;
}

uint8_t I2CmicroSD::version(char *buf, uint8_t buflen) {
  uint8_t n = firmwareBanner(buf, buflen);
  if (n == 0) return 0;
  // Accept both the bare "18.7.6" string and the >=18.8 banner, whose first
  // line is the measured clock and whose second line carries "v18.8.22".
  const char *p = buf;
  while (*p) {
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') ++p;
    const char *tok = start;
    size_t len = (size_t)(p - start);
    if (len && (*tok == 'v' || *tok == 'V')) { ++tok; --len; }
    while (len && (tok[len - 1] == ',' || tok[len - 1] == ';' ||
                   tok[len - 1] == '(' || tok[len - 1] == ')')) --len;
    bool dotted = len >= 3 && memchr(tok, '.', len) != nullptr &&
                  tok[0] >= '0' && tok[0] <= '9';
    for (size_t i = 0; i < len && dotted; ++i) {
      if (tok[i] != '.' && (tok[i] < '0' || tok[i] > '9')) dotted = false;
    }
    if (dotted) {
      if (tok != buf) memmove(buf, tok, len);
      buf[len] = '\0';
      return (uint8_t)len;
    }
  }
  // No dotted token: the buffer was too small to reach the version line
  // (or the answer was malformed). Return 0 with INVALID rather than a
  // raw banner fragment such as the first-line clock value.
  _lastError = MICROSDEXP_INVALID;
  return 0;
}

uint32_t I2CmicroSD::firmwareClockHz() {
  char banner[I2CMICROSD_VERSION_BANNER_BYTES];
  if (firmwareBanner(banner, sizeof(banner)) == 0) return 0;
  const char *p = banner;
  while (*p && *p != '\n' && *p != '\r') ++p;
  // First line: "Clock: 20000000 Hz (fuse OSCCFG=0x7E)".
  char first[32];
  size_t n = (size_t)(p - banner);
  if (n >= sizeof(first)) n = sizeof(first) - 1;
  memcpy(first, banner, n);
  first[n] = '\0';
  p = first;
  while (*p && (*p < '0' || *p > '9')) ++p;
  uint32_t hz = 0;
  while (*p >= '0' && *p <= '9') {
    hz = hz * 10UL + (uint32_t)(*p - '0');
    ++p;
  }
  return hz;
}

uint16_t I2CmicroSD::temperatureK() {
  uint8_t err = sendCmd(MICROSDEXP_CMD_TEMP_READ, nullptr, 0, false);
  if (err != 0) {
    _lastError = normalizeWire(err);
    return 0xFFFF;
  }
  if (receiveBytes(g_shared, 2) < 2) {
    _lastError = MICROSDEXP_INVALID;
    return 0xFFFF;
  }
  _lastError = MICROSDEXP_OK;
  return (uint16_t)g_shared[0] | ((uint16_t)g_shared[1] << 8);
}

uint16_t I2CmicroSD::adcPA5() {
  uint8_t err = sendCmd(MICROSDEXP_CMD_ADC_READ_PA5, nullptr, 0, false);
  if (err != 0) {
    _lastError = normalizeWire(err);
    return 0xFFFF;
  }
  if (receiveBytes(g_shared, 3) < 2) {
    _lastError = MICROSDEXP_INVALID;
    return 0xFFFF;
  }
  // 0xFF/0xFF marks an unavailable channel on this part.
  if (g_shared[0] == 0xFF && g_shared[1] == 0xFF) {
    _lastError = MICROSDEXP_INVALID;
    return 0xFFFF;
  }
  _lastError = MICROSDEXP_OK;
  return (uint16_t)g_shared[0] | ((uint16_t)g_shared[1] << 8);
}

bool I2CmicroSD::cardInfo(CardInfo *out) {
  if (!out) {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  uint8_t err = sendCmd(MICROSDEXP_CMD_CARD_INFO, nullptr, 0, false);
  if (err != 0) return false;
  if (receiveBytes(g_shared, 24) < 24) {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  out->mounted = g_shared[0];
  out->fatType = g_shared[1];
  out->cardType = g_shared[2];
  out->rsvd = g_shared[3];
  out->sectorCount = (uint32_t)g_shared[4] | ((uint32_t)g_shared[5] << 8) | ((uint32_t)g_shared[6] << 16) | ((uint32_t)g_shared[7] << 24);
  out->freeClusters = (uint32_t)g_shared[8] | ((uint32_t)g_shared[9] << 8) | ((uint32_t)g_shared[10] << 16) | ((uint32_t)g_shared[11] << 24);
  out->errCode = (uint16_t)g_shared[12] | ((uint16_t)g_shared[13] << 8);
  out->errData = (uint16_t)g_shared[14] | ((uint16_t)g_shared[15] << 8);
  out->clusterCount = (uint32_t)g_shared[16] | ((uint32_t)g_shared[17] << 8) | ((uint32_t)g_shared[18] << 16) | ((uint32_t)g_shared[19] << 24);
  out->sckHz = (uint32_t)g_shared[20] | ((uint32_t)g_shared[21] << 8) | ((uint32_t)g_shared[22] << 16) | ((uint32_t)g_shared[23] << 24);
  _lastError = MICROSDEXP_OK;
  return true;
}

void I2CmicroSD::setClockTime(uint8_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  g_shared[0] = y; g_shared[1] = mo; g_shared[2] = d;
  g_shared[3] = h; g_shared[4] = mi; g_shared[5] = s;
  uint8_t err = sendCmd(MICROSDEXP_CMD_CLOCK_SET, g_shared, 6, true);
  // No read-back; firmware applies STOP (silently ignores wrong length).
  if (err != 0) _lastError = normalizeWire(err);
  else _lastError = MICROSDEXP_OK;
}

void I2CmicroSD::reboot() {
  uint8_t err = sendCmd(MICROSDEXP_CMD_REBOOT, nullptr, 0, true);
  _lastError = normalizeWire(err);
}

bool I2CmicroSD::format(uint8_t mode) {
  if (mode > 1) {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  // Explicit-only (no token); guards live in sketch/UI.
  uint8_t doneBefore = 0;
  if (!readOpDone(doneBefore)) return false;
  g_shared[0] = mode;
  if (sendCmd(MICROSDEXP_CMD_FORMAT, g_shared, 1, true) != 0) return false;
  int err = waitForOpComplete(doneBefore, I2CMICROSD_FORMAT_TIMEOUT_MS);
  if (err != 0) {
    _lastError = (err == -1) ? MICROSDEXP_TIMEOUT : MICROSDEXP_CARD_ERR;
    return false;
  }
  _lastError = MICROSDEXP_OK;
  return true;
}

// listDir enumeration (v18.7.6 0x08 TX-stream, v15 wire format).
// Stream: 'F'/'D' + NUL basename + 4 B size LE; 0xFF terminates. The OP
// completes at stream start (like READ_FILE); drained via STATUS.txAvail +
// 0x23 repeated-START. Names capped at 63 chars (DirEntry.name[64]).
uint8_t I2CmicroSD::listDir(const char *path, DirEntry *out, uint8_t outCap) {
  if (!validPath(path) || !out || outCap == 0) {
    if (!path) _lastError = MICROSDEXP_INVALID;
    return 0;
  }
  uint8_t doneBefore = 0;
  if (!readOpDone(doneBefore)) return 0;
  if (!submitFilename(path)) return 0;
  if (sendCmd(MICROSDEXP_CMD_LIST_DIR, nullptr, 0, true) != 0) return 0;
  int opErr = waitForOpComplete(doneBefore, I2CMICROSD_OP_TIMEOUT_MS);
  if (opErr != 0) {
    MicroSDExpError e = (opErr == -1) ? MICROSDEXP_TIMEOUT : MICROSDEXP_OPEN_FAIL;
    (void)barrierCloseFlush();
    _lastError = e;
    return 0;
  }
  // module signals missing/file-as-dir as ST_ERROR with ERR_NONE (0): the
  // wait returned 0 but the state latched ERROR — treat as open-fail.
  {
    StatusFrame stchk;
    if (readStatus(stchk) && stchk.state == MICROSDEXP_ST_ERROR) {
      (void)barrierCloseFlush();
      _lastError = MICROSDEXP_OPEN_FAIL;
      return 0;
    }
  }
  // Drain + parse (streaming state machine, single shared buffer).
  enum : uint8_t { P_TYPE = 0, P_NAME = 1, P_SIZE = 2, P_END = 3 };
  uint8_t pstate = P_TYPE;
  uint8_t isDir = 0;
  uint32_t szAcc = 0;
  uint8_t szIdx = 0;
  uint8_t nameIdx = 0;
  bool overflow = false;
  uint8_t count = 0;
  bool finished = false;
  uint32_t start = millis();
  uint32_t limit = scaleTimeout(I2CMICROSD_COMMIT_TIMEOUT_MS);
  while (!finished) {
    if ((millis() - start) >= limit) {
      _lastError = MICROSDEXP_TIMEOUT;
      break;
    }
    StatusFrame st;
    if (!readStatus(st)) {
      delay(_pollMs);
      continue;
    }
    if (st.state == MICROSDEXP_ST_ERROR) {
      _lastError = MICROSDEXP_CARD_ERR;
      count = 0;
      break;
    }
    if (st.txAvail > 0) {
      if (sendCmd(MICROSDEXP_CMD_READ, nullptr, 0, false) != 0) {
        _lastError = MICROSDEXP_CARD_ERR;
        count = 0;
        break;
      }
      size_t want = st.txAvail > sizeof(g_shared) ? sizeof(g_shared) : st.txAvail;
#if defined(__AVR__) && !defined(Tinyi2cStorage)
      if (want > 32) want = 32;
#endif
      size_t n = receiveBytes(g_shared, want);
      for (size_t i = 0; i < n && !finished; i++) {
        uint8_t cb = g_shared[i];
        if (pstate == P_TYPE) {
          if (cb == 0xFF) {
            finished = true;  // end of stream
          } else if (cb == 'F' || cb == 'D') {
            isDir = (cb == 'D') ? 1 : 0;
            nameIdx = 0;
            szAcc = 0;
            szIdx = 0;
            pstate = P_NAME;
          } else {
            overflow = true;  // corrupt stream
            finished = true;
          }
        } else if (pstate == P_NAME) {
          if (cb == 0x00) {
            // Terminate the DirEntry name; overflow keeps counting.
            if (!overflow && count < outCap) {
              out[count].isDir = isDir;
              // name[] already accumulated; NUL written below.
            }
            pstate = P_SIZE;
            szIdx = 0;
          } else {
            if (!overflow && count < outCap) {
              if (nameIdx < 63) {
                out[count].name[nameIdx++] = (char)cb;
              } else {
                // Long LFN (<=259) truncated to 63; entry still counted.
                // Mark by keeping nameIdx at 63 and ignoring further bytes.
              }
            } else {
              // Beyond outCap: keep consuming the stream, don't store.
            }
          }
        } else if (pstate == P_SIZE) {
          szAcc |= ((uint32_t)cb) << (8 * szIdx);
          szIdx++;
          if (szIdx >= 4) {
            if (!overflow && count < outCap) {
              out[count].size = isDir ? 0 : szAcc;
              out[count].name[nameIdx < 64 ? nameIdx : 63] = '\0';
              // Ensure NUL for short names:
              if (nameIdx < 64) out[count].name[nameIdx] = '\0';
              count++;
            } else if (!overflow && count >= outCap) {
              // Stream has more entries than outCap: keep counting to end
              // but report outCap (contract: returns capped count).
              count++;
            }
            pstate = P_TYPE;
          }
        }
      }
      // NUL-terminate current entry name if we're mid-name at chunk end
      // (harmless; overwritten as name grows).
      if (!overflow && count < outCap && (pstate == P_NAME)) {
        if (nameIdx < 64) out[count].name[nameIdx] = '\0';
      }
      continue;
    }
    if (st.state == MICROSDEXP_ST_COMPLETE && pstate != P_END) {
      // Stream sealed (0xFF drained) or empty (single 0xFF).
      break;
    }
    delay(_pollMs);
  }
  // Barrier (CLOSE_FLUSH) to re-sync opDone after listing.
  // Preserve a drain error across the barrier (barrier resets lastError).
  MicroSDExpError preserved = _lastError;
  (void)barrierCloseFlush();
  if (overflow) {
    _lastError = MICROSDEXP_INVALID;
    return 0;
  }
  _lastError = preserved;
  if (count > outCap) count = outCap;
  return count;
}
#endif  // !Tinyi2cStorage && !I2CSDMODULE_MINIMAL

// ---- Bit-bang / NeoPixel (WS2812B) ----
//
// The module drives a WS2812B strip from one of its expansion pins: the
// host pushes raw GRB bytes into the module's frame workspace (0x2C) and
// then triggers the blit (0x2D). Frames are plain GRB triplets — one byte
// per channel, green first — so the host keeps full control of the colour
// order without pulling in an LED library.
//
// A 0x2C command is 1 command byte + 3 header bytes (offset LSB, offset
// MSB, payload length) + payload. The module counts the payload bytes of
// that command and rejects any extra byte, so a command never carries more
// payload than it announces, and the header plus the payload must travel
// in the same bus transaction (the module reads them as one command). The
// payload of a command is therefore capped by the host's I2C transmit
// buffer (I2CMICROSD_BITBANG_MAX); a longer write is split into several
// commands that continue at the next workspace offset.
bool I2CmicroSD::bitbangBuffer(uint16_t offset, const uint8_t *data, uint8_t len) {
  if (!data || len == 0) {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  // The module's workspace is 256 bytes. Reject out-of-range writes locally
  // instead of relying on the module to NACK or silently drop the extra bytes.
  if ((uint32_t)offset + (uint32_t)len > I2CMICROSD_BITBANG_WORKSPACE) {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  uint16_t sent = 0;
  while (sent < len) {
    uint8_t chunk = (uint8_t)(len - sent);
    if (chunk > I2CMICROSD_BITBANG_MAX) chunk = I2CMICROSD_BITBANG_MAX;
    uint16_t off = (uint16_t)(offset + sent);
    g_shared[0] = (uint8_t)(off & 0xFF);
    g_shared[1] = (uint8_t)((off >> 8) & 0xFF);
    g_shared[2] = chunk;
    for (uint8_t i = 0; i < chunk; i++) g_shared[3 + i] = data[sent + i];
    // Header and payload go out together: one command, one transaction.
    if (sendCmd(MICROSDEXP_CMD_BITBANG_BUFFER, g_shared, (size_t)3 + chunk, true) != 0) return false;
    sent = (uint16_t)(sent + chunk);
  }
  return true;
}

bool I2CmicroSD::bitbangSetMode(uint8_t pin) {
  g_shared[0] = pin;
  return sendCmd(MICROSDEXP_CMD_BITBANG_SET_MODE, g_shared, 1, true) == 0;
}

bool I2CmicroSD::bitbangExecute(uint8_t profile) {
  g_shared[0] = profile;
  return sendCmd(MICROSDEXP_CMD_BITBANG_EXECUTE, g_shared, 1, true) == 0;
}

void I2CmicroSD::bitbangClear() {
  // 0x2E takes no payload: the module zeroes its workspace on STOP.
  (void)sendCmd(MICROSDEXP_CMD_BITBANG_CLEAR, nullptr, 0, true);
}

// Convenience wrapper: set the data pin, push the whole GRB frame, latch it.
bool I2CmicroSD::showNeoPixels(uint8_t pin, const uint8_t *grb, uint16_t pixelCount) {
  if (!grb || pixelCount == 0) {
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  uint32_t bytes = (uint32_t)pixelCount * 3u;  // 3 bytes per pixel (GRB)
  if (bytes > 255u) {                        // frame length field is 1 byte
    _lastError = MICROSDEXP_INVALID;
    return false;
  }
  if (!bitbangSetMode(pin)) return false;
  // The frame length field of a workspace write is one byte, so a long
  // frame goes out as several writes that continue at the next offset.
  uint32_t sent = 0;
  while (sent < bytes) {
    uint8_t chunk = (uint8_t)((bytes - sent) > 255u ? 255u : (bytes - sent));
    if (!bitbangBuffer((uint16_t)sent, grb + sent, chunk)) return false;
    sent += chunk;
  }
  return bitbangExecute(MICROSDEXP_BITBANG_PROFILE_NEOPIXEL);
}

// Expansion-pin control: the mode/control command pair depends on the pin,
// so an unknown pin has no command to send.
bool I2CmicroSD::setPinMode(uint8_t pin, uint8_t mode) {
  uint8_t cmd;
  switch (pin) {
    case MICROSDEXP_PIN_PA5: cmd = MICROSDEXP_CMD_PA5_SET_MODE; break;
    case MICROSDEXP_PIN_PB2: cmd = MICROSDEXP_CMD_PB2_SET_MODE; break;
    case MICROSDEXP_PIN_PB3: cmd = MICROSDEXP_CMD_PB3_SET_MODE; break;
    default:
      _lastError = MICROSDEXP_INVALID;
      return false;
  }
  // Dedup: the value only applies in place once and only persists on an
  // explicit savePINMode(), so a redundant apply writes nothing and never
  // churns the module EEPROM (no auto-persist contract).
  if ((pin < 8) && (_pinLastMode[pin] == mode)) {
    return true;
  }
  g_shared[0] = mode;
  if (sendCmd(cmd, g_shared, 1, true) != 0) return false;
  // Cache only a confirmed ACK. A NACK (busy bit-bang engine, bus glitch,
  // module rebooting) must leave the cache alone so the next call retries.
  if (pin < 8) {
    _pinLastMode[pin] = mode;
  }
  return true;
}

// Persist the currently-configured pin modes + I2C address to the module's
// EEPROM (CMD_SAVEEEPROM, 0x1A). The module applies mode changes in place and
// only writes EEPROM on this explicit command, so a redundant save (nothing
// changed since the last save) is a harmless no-op on the board.
void I2CmicroSD::savePINMode() {
  sendCmd(MICROSDEXP_CMD_SAVEEEPROM, nullptr, 0, true);
}

bool I2CmicroSD::writePin(uint8_t pin, uint8_t value) {
  uint8_t cmd;
  switch (pin) {
    case MICROSDEXP_PIN_PA5: cmd = MICROSDEXP_CMD_PA5_CONTROL; break;
    case MICROSDEXP_PIN_PB2: cmd = MICROSDEXP_CMD_PB2_CONTROL; break;
    case MICROSDEXP_PIN_PB3: cmd = MICROSDEXP_CMD_PB3_CONTROL; break;
    default:
      _lastError = MICROSDEXP_INVALID;
      return false;
  }
  g_shared[0] = value;
  return sendCmd(cmd, g_shared, 1, true) == 0;
}

bool I2CmicroSD::openAppend(const char *path) {
  if (!validPath(path)) return false;
  // Legacy append 0x04 (MUST-have, all tiers): no module OPEN is issued now;
  // each fileWrite() is a separate atomic APPEND transaction (0x01 + 0x04 +
  // payload, worker opens+appends+closes).
  size_t i = 0;
  while (path[i] != '\0' && i + 1 < sizeof(_appendPath)) {
    _appendPath[i] = path[i];
    i++;
  }
  _appendPath[i] = '\0';
  _appendMode = true;
  _fileOpen = true;
  _readStarted = false;
  _written = 0;
  _lastError = MICROSDEXP_OK;
  return true;
}

// One legacy append chunk: 0x01 path + 0x04 + payload (≤64 B), atomic.
bool I2CmicroSD::appendChunk(const uint8_t *data, size_t len) {
  uint8_t doneBefore = 0;
  if (!readOpDone(doneBefore)) return false;
  if (!submitFilename(_appendPath)) return false;
  if (sendCmd(MICROSDEXP_CMD_APPEND_FILE, data, len, true) != 0) return false;
  int err = waitForOpComplete(doneBefore, I2CMICROSD_OP_TIMEOUT_MS);
  if (err != 0) {
    _lastError = (err == -1) ? MICROSDEXP_TIMEOUT : MICROSDEXP_CARD_ERR;
    return false;
  }
  _lastError = MICROSDEXP_OK;
  return true;
}

bool I2CmicroSD::openWrite(const char *path) {
  if (!validPath(path)) return false;
  _appendMode = false;
  for (int attempt = 0; attempt < 6; attempt++) {
    (void)barrierCloseFlush();
    uint8_t doneBefore = 0;
    if (!readOpDone(doneBefore)) return false;

    I2C_BUS.beginTransmission(_addr);
    I2C_BUS.write(MICROSDEXP_CMD_OPEN_WRITE);
    size_t i = 0;
    while (path[i] != '\0' && i < (size_t)(I2CMICROSD_PATH_MAX - 1)) {
      I2C_BUS.write((uint8_t)path[i]);
      i++;
    }
    I2C_BUS.write((uint8_t)0);
    uint8_t err = I2C_BUS.endTransmission(true);
    _wireErrLast = err;
    _lastError = normalizeWire(err);
    if (err != 0) {
      _fileOpen = false;
      delay(200);
      continue;
    }
    int opErr = waitForOpComplete(doneBefore, I2CMICROSD_OP_TIMEOUT_MS);
    if (opErr == 0) {
      StatusFrame st;
      (void)readStatus(st);
      _baseWritten = st.result;
      _written = 0;
      _fileOpen = true;
      _readStarted = false;
      _lastError = MICROSDEXP_OK;
      return true;
    }
    if (opErr == -1) _lastError = MICROSDEXP_TIMEOUT;
    else _lastError = MICROSDEXP_OPEN_FAIL;
    delay(200);
  }
  _fileOpen = false;
  return false;
}

bool I2CmicroSD::openRead(const char *path) {
  if (!validPath(path)) return false;
  _appendMode = false;
  for (int attempt = 0; attempt < 6; attempt++) {
    (void)barrierCloseFlush();
    StatusFrame before;
    uint8_t doneBefore = 0;
    if (!readOpDone(doneBefore)) return false;

    I2C_BUS.beginTransmission(_addr);
    I2C_BUS.write(MICROSDEXP_CMD_OPEN_READ);
    size_t i = 0;
    while (path[i] != '\0' && i < (size_t)(I2CMICROSD_PATH_MAX - 1)) {
      I2C_BUS.write((uint8_t)path[i]);
      i++;
    }
    I2C_BUS.write((uint8_t)0);
    uint8_t err = I2C_BUS.endTransmission(true);
    _wireErrLast = err;
    _lastError = normalizeWire(err);
    if (err != 0) {
      _fileOpen = false;
      delay(200);
      continue;
    }
    int opErr = waitForOpComplete(doneBefore, I2CMICROSD_OP_TIMEOUT_MS);
    if (opErr == 0) {
      uint8_t rerr = sendCmd(MICROSDEXP_CMD_READ, nullptr, 0, true);
      if (rerr != 0) _lastError = MICROSDEXP_NO_FILE;
      _readStarted = true;
      _fileOpen = true;
      _written = 0;
      (void)readStatus(before);
      _baseWritten = before.result;
      _lastError = MICROSDEXP_OK;
      return true;
    }
    _lastError = (opErr == -1) ? MICROSDEXP_TIMEOUT : MICROSDEXP_OPEN_FAIL;
    delay(200);
  }
  _fileOpen = false;
  return false;
}

// Flow-paced push: only when rxFree >= len, poll 2 ms
// while waiting, 0 blind sleeps. 400 attempts, 1 ms NACK retry.
bool I2CmicroSD::pushWriteBlock(const uint8_t *data, size_t len) {
  uint16_t maxAttempts = (_sclHz != 0 && _sclHz < 50000UL) ? 1200 : 400;
  for (uint16_t attempt = 0; attempt < maxAttempts; attempt++) {
    StatusFrame st;
    if (!readStatus(st)) {
      delay(_flowPollMs);
      continue;
    }
    if (st.state == MICROSDEXP_ST_ERROR) {
      _lastError = MICROSDEXP_CARD_ERR;
      return false;
    }
    if (st.rxFree < (uint16_t)len) {
      delay(_flowPollMs);
      continue;
    }
    uint8_t err = sendCmd(MICROSDEXP_CMD_WRITE, data, len, true);
    if (err == 0) return true;
    // rxFree said space but NACKed (split across buffers / worker claimed one):
    // count and re-check flow control instead of sleeping blind.
    delay(1);
  }
  _lastError = MICROSDEXP_TIMEOUT;
  return false;
}

size_t I2CmicroSD::write(const uint8_t *buf, size_t len) {
  if (!buf || len == 0 || !_fileOpen) {
    _lastError = MICROSDEXP_NO_FILE;
    return 0;
  }
  // Append mode: each ≤64 B chunk is one atomic legacy APPEND (0x04).
  if (_appendMode) {
    size_t off = 0;
    while (off < len) {
      size_t chunk = (len - off) > I2CMICROSD_CHUNK_BYTES ? I2CMICROSD_CHUNK_BYTES : (len - off);
      if (!appendChunk(buf + off, chunk)) {
        _written += (uint32_t)off;
        return off;
      }
      off += chunk;
    }
    _written += (uint32_t)off;
    _lastError = MICROSDEXP_OK;
    return off;
  }
  size_t off = 0;
  while (off < len) {
    size_t chunk = (len - off) > I2CMICROSD_CHUNK_BYTES ? I2CMICROSD_CHUNK_BYTES : (len - off);
    if (!pushWriteBlock(buf + off, chunk)) {
      _written += (uint32_t)off;
      return off;  // partial accept reported; caller syncs/closes
    }
    off += chunk;
  }
  _written += (uint32_t)off;
  _lastError = MICROSDEXP_OK;
  return off;
}

// Drain loop: STATUS -> txAvail -> repeated-START drain.
// Ends on requested length, COMPLETE (EOF), or ERROR.
size_t I2CmicroSD::read(uint8_t *buf, size_t len) {
  if (!buf || len == 0 || !_fileOpen) {
    if (!_fileOpen) _lastError = MICROSDEXP_NO_FILE;
    return 0;
  }
  if (_appendMode) {
    _lastError = MICROSDEXP_INVALID;  // append handle is write-only
    return 0;
  }
  size_t got = 0;
  uint32_t start = millis();
  uint32_t limit = scaleTimeout(I2CMICROSD_COMMIT_TIMEOUT_MS);
  while (got < len) {
    if ((millis() - start) >= limit) {
      _lastError = MICROSDEXP_TIMEOUT;
      return got;
    }
    StatusFrame st;
    if (!readStatus(st)) {
      delay(_pollMs);
      continue;
    }
    if (st.state == MICROSDEXP_ST_ERROR) {
      _lastError = MICROSDEXP_CARD_ERR;
      return got;
    }
    if (st.txAvail > 0) {
      uint8_t err = sendCmd(MICROSDEXP_CMD_READ, nullptr, 0, false);
      if (err != 0) {
        _lastError = MICROSDEXP_CARD_ERR;
        return got;
      }
      size_t want = st.txAvail;
      size_t remain = len - got;
      if (want > remain) want = remain;
      // v18.8.21 protocol: STATUS txAvail reports the total staged in both
      // ping-pong buffers (up to 256), not just the oldest (up to 128).
      // Cap each drain at the configured chunk so the host's Wire receive
      // buffer (requestFrom() clamps to bufferSize, default 128) can hold it.
      // Raise with setReadChunkBytes() after enlarging that buffer; the
      // config header keeps AVR/Tiny at 32/16 regardless.
      if (want > _readChunk) want = _readChunk;
      if (want == 0) continue;
      size_t n = receiveBytes(buf + got, want);
      got += n;
      continue;
    }
    if (st.state == MICROSDEXP_ST_COMPLETE) {
      break;  // EOF
    }
    delay(_pollMs);
  }
  _lastError = MICROSDEXP_OK;
  return got;                       // read length is the return value; _written is write-only
}

bool I2CmicroSD::sync() {
  if (!_fileOpen) return true;  // nothing open
  if (_appendMode) return true;  // append already committed per chunk
  if (_readStarted) return true;  // reads need no commit wait
  // Wait until every pushed byte is committed (result >= base + written).
  uint32_t target = _baseWritten + _written;
  bool ok = waitForCommitted(target, I2CMICROSD_COMMIT_TIMEOUT_MS);
  _lastError = ok ? MICROSDEXP_OK : MICROSDEXP_TIMEOUT;
  return ok;
}

bool I2CmicroSD::close() {
  if (!_fileOpen) return true;  // idempotent
  if (_appendMode) {
    // Each append chunk already opened+closed on the module; nothing to flush.
    _fileOpen = false;
    _appendMode = false;
    _readStarted = false;
    _lastError = MICROSDEXP_OK;
    return true;
  }
  bool ok = sync();
  uint8_t db = 0;
  if (!readOpDone(db)) {
    _fileOpen = false;
    _readStarted = false;
    return false;
  }
  uint8_t err = sendCmd(MICROSDEXP_CMD_CLOSE_FLUSH, nullptr, 0, true);
  if (err != 0) {
    _lastError = MICROSDEXP_CARD_ERR;
    _fileOpen = false;
    _readStarted = false;
    return false;
  }
  int cw = waitForOpComplete(db, I2CMICROSD_OP_TIMEOUT_MS);
  _fileOpen = false;
  _readStarted = false;
  if (cw != 0) {
    _lastError = (cw == -1) ? MICROSDEXP_TIMEOUT : MICROSDEXP_CARD_ERR;
    ok = false;
  } else {
    _lastError = MICROSDEXP_OK;
  }
  return ok;
}

// ---- File API (extern "C") ----------

extern "C" MicroSDExpFile *openFile(I2CmicroSD *b, const char *path, const char *mode) {
  if (!b || !path || !mode) return nullptr;
  char m = mode[0];
  bool ok = false;
  uint8_t mCode = 0;
  if (m == 'w' || m == 'W') {
    ok = b->openWrite(path);
    mCode = 1;
  } else if (m == 'a' || m == 'A') {
    // MUST-have: File::open(p,"a") -> legacy append 0x04 (never OPEN_WRITE).
    ok = b->openAppend(path);
    mCode = 2;
  } else if (m == 'r' || m == 'R') {
    ok = b->openRead(path);
    mCode = 0;
  } else {
    return nullptr;
  }
  if (!ok) return nullptr;
  gActiveFile.open = 1;
  gActiveFile.error = 0;
  gActiveFile.mode = mCode;
  gActiveFile._pad = 0;
  gActiveFile.len = 0;
  gActiveFile.dummy = 0;
  gActiveFile.microSDexp = b;
  return &gActiveFile;
}

extern "C" size_t fileWrite(MicroSDExpFile *f, const uint8_t *buf, size_t len) {
  if (!f || !f->open || !f->microSDexp) return 0;
  size_t n = f->microSDexp->write(buf, len);
  f->error = (n == len) ? 0 : (uint8_t)f->microSDexp->lastError();
  return n;
}

extern "C" size_t fileRead(MicroSDExpFile *f, uint8_t *buf, size_t len) {
  if (!f || !f->open || !f->microSDexp) return 0;
  size_t n = f->microSDexp->read(buf, len);
  f->error = (uint8_t)f->microSDexp->lastError();
  return n;
}

extern "C" bool fileSync(MicroSDExpFile *f) {
  if (!f || !f->open || !f->microSDexp) return false;
  return f->microSDexp->sync();
}

extern "C" bool fileClose(MicroSDExpFile *f) {
  if (!f || !f->open || !f->microSDexp) return false;
  bool ok = f->microSDexp->close();
  f->open = 0;
  return ok;
}



