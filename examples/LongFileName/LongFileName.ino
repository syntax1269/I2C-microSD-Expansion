// LongFileName.ino — 3-level LFN nesting, 500–2000 B files (alntest 5/5).
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

I2CmicroSD microSDexp;

// Deterministic LCG (no rand(), no heap): b = (seed>>8)&0xFF, seed = seed*1664525+1013904223.
static uint8_t lcgByte(uint32_t &seed) {
  seed = seed * 1664525UL + 1013904223UL;
  return (uint8_t)((seed >> 8) & 0xFF);
}

static bool roundtripOne(const char *path, uint32_t len, uint32_t seed) {
  File *w = openFile(&microSDexp, path, "w");
  if (!w) {
    char b[192];
    snprintf(b, sizeof(b), "  open \"w\" %s FAILED err=%d", path, (int)microSDexp.lastError());
    Serial.println(b);
    return false;
  }
  uint32_t s = seed;
  uint8_t chunk[64];
  uint32_t sent = 0;
  bool ok = true;
  while (sent < len) {
    size_t n = (len - sent) > sizeof(chunk) ? sizeof(chunk) : (len - sent);
    for (size_t i = 0; i < n; i++) chunk[i] = lcgByte(s);
    if (fileWrite(w, chunk, n) != n) { ok = false; break; }
    sent += (uint32_t)n;
  }
  if (!fileClose(w)) ok = false;
  if (!ok) return false;

  File *r = openFile(&microSDexp, path, "r");
  if (!r) return false;
  uint32_t s2 = seed;
  uint8_t buf[128];
  uint32_t got = 0;
  ok = true;
  while (got < len) {
    size_t want = (len - got) > sizeof(buf) ? sizeof(buf) : (len - got);
    size_t n = fileRead(r, buf, want);
    if (n == 0) { ok = false; break; }
    for (size_t i = 0; i < n; i++) {
      if (buf[i] != lcgByte(s2)) ok = false;
    }
    got += (uint32_t)n;
  }
  fileClose(r);
  return ok;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); // sketch owns pins: custom boards use Wire.begin(sda,scl)
  if (!microSDexp.begin(0x6E, 100000)) {
    Serial.println(F("LongFileName: begin FAILED"));
    return;
  }

  const char *d1 = "LongDirectoryName_LevelOne_0123456789";
  const char *d2 = "LongDirectoryName_LevelOne_0123456789/LongDirectoryName_LevelTwo_0123456789";
  const char *d3 = "LongDirectoryName_LevelOne_0123456789/LongDirectoryName_LevelTwo_0123456789/LongDirectoryName_LevelThree_0123456";
  bool m1 = microSDexp.mkdir(d1);
  delay(200);  // worker settle between legacy ops (burst pacing)
  bool m2 = microSDexp.mkdir(d2);
  delay(200);
  bool m3 = microSDexp.mkdir(d3);
  {
    char b[128];
    snprintf(b, sizeof(b), "mkdir L1=%d L2=%d L3=%d err=%d", m1 ? 1 : 0, m2 ? 1 : 0, m3 ? 1 : 0, (int)microSDexp.lastError());
    Serial.println(b);
    snprintf(b, sizeof(b), "isdir L1=%d L2=%d L3=%d", microSDexp.isDirectory(d1) ? 1 : 0, microSDexp.isDirectory(d2) ? 1 : 0, microSDexp.isDirectory(d3) ? 1 : 0);
    Serial.println(b);
  }

  int fails = 0;
  const uint32_t sizes[5] = {500, 1100, 800, 2000, 640};
  for (int k = 0; k < 5; k++) {
    char fp[192];
    snprintf(fp, sizeof(fp), "%s/LongFileName_TestData_%d_%lu.BIN", d3, k + 1, (unsigned long)sizes[k]);
    bool ok = roundtripOne(fp, sizes[k], 0x12345678UL + (uint32_t)k * 2654435761UL);
    char b[96];
    snprintf(b, sizeof(b), "  file %d %lu B: %s", k + 1, (unsigned long)sizes[k], ok ? "OK" : "FAIL");
    Serial.println(b);
    if (!ok) fails++;
  }

  // Listing: deepest dir has 5 files.
  DirEntry entries[8];
  uint8_t n = microSDexp.listDir(d3, entries, 8);
  char b[64];
  snprintf(b, sizeof(b), "LISTDIR deepest count=%u err=%d (expect 5)", (unsigned)n, (int)microSDexp.lastError());
  Serial.println(b);
  if (n != 5) fails++;

  Serial.println(fails == 0 ? F("LongFileName: PASS (5/5 + list)") : F("LongFileName: FAIL"));
}

void loop() {}




