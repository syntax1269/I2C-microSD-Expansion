// BasicReadWrite.ino — 64 KB flow-paced round-trip + CRC32.
// Proves the async engine end-to-end (hash D660AF09).
// Pattern matches the reference pattern: b[i] = (i*7+3)&0xFF, CRC32 IEEE-754.
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

I2CmicroSD microSDexp;

static uint32_t crc32_begin() { return 0xFFFFFFFFUL; }
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint32_t)data[i];
    for (int k = 0; k < 8; k++) {
      crc = (crc & 1) ? (0xEDB88320UL ^ (crc >> 1)) : (crc >> 1);
    }
  }
  return crc;
}
static uint32_t crc32_end(uint32_t c) { return c ^ 0xFFFFFFFFUL; }

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_ARCH_ESP32)
  // v18.8.21: the module can stage 256 B across both TX buffers. ESP32 Wire
  // clamps requestFrom() to its receive buffer, so grow it before begin().
  Wire.setBufferSize(256);
#endif
  Wire.begin(); // sketch owns pins: custom boards use Wire.begin(sda,scl)
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("BasicReadWrite: begin FAILED"));
    return;
  }
#if I2CMICROSD_READ_CHUNK_MAX > 128
  microSDexp.setReadChunkBytes(256);  // drain both 128 B module buffers per READ
#endif

  const uint32_t kBytes = 65536;
  const char *path = "Example64K.BIN";

  MicroSDExpFile *w = openFile(&microSDexp, path, "w");
  if (!w) {
    Serial.println(F("open \"w\" FAILED"));
    return;
  }
  uint32_t t0 = millis();
  uint8_t chunk[64];
  for (uint32_t off = 0; off < kBytes; off += 64) {
    size_t n = (kBytes - off) > sizeof(chunk) ? sizeof(chunk) : (kBytes - off);
    for (size_t i = 0; i < n; i++) chunk[i] = (uint8_t)(((off + i) * 7 + 3) & 0xFF);
    if (fileWrite(w, chunk, n) != n) {
      Serial.println(F("fileWrite short"));
      return;
    }
  }
  if (!fileClose(w)) {
    Serial.println(F("fileClose (write) FAILED"));
    return;
  }
  char b[64];
  snprintf(b, sizeof(b), "wrote %lu B (write+close) in %lu ms",
           (unsigned long)kBytes, (unsigned long)(millis() - t0));
  Serial.println(b);

  MicroSDExpFile *r = openFile(&microSDexp, path, "r");
  if (!r) {
    Serial.println(F("open \"r\" FAILED"));
    return;
  }
  uint32_t c = crc32_begin();
  uint32_t got = 0, mism = 0;
  uint8_t buf[256];
  while (got < kBytes) {
    size_t want = (kBytes - got) > sizeof(buf) ? sizeof(buf) : (kBytes - got);
    size_t n = fileRead(r, buf, want);
    if (n == 0) break;
    for (size_t i = 0; i < n; i++) {
      uint8_t expect = (uint8_t)(((got + i) * 7 + 3) & 0xFF);
      if (buf[i] != expect) mism++;
    }
    c = crc32_update(c, buf, n);
    got += (uint32_t)n;
  }
  fileClose(r);
  uint32_t hash = crc32_end(c);
  snprintf(b, sizeof(b), "read %lu B mismatches=%lu hash=%08lX",
           (unsigned long)got, (unsigned long)mism, (unsigned long)hash);
  Serial.println(b);
  Serial.println(got == kBytes && mism == 0 && hash == 0xD660AF09UL
                     ? F("BasicReadWrite: PASS")
                     : F("BasicReadWrite: FAIL"));
}

void loop() {}




