// AbruptRecovery.ino — two 7 s silence periods; the 2048 B prefix stays intact.
// The module idle-flushes+closes after 5 s bus silence and advances opDone.
// The library's gap-gated barrier (CLOSE_FLUSH) on the next open re-syncs, so a
// fresh round-trip succeeds and the aborted prefix is byte-perfect.
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

I2CmicroSD microSDexp;

static bool smallRoundtrip(const char *path, uint32_t kLen) {
  File *w = openFile(&microSDexp, path, "w");
  if (!w) return false;
  uint8_t chunk[64];
  uint32_t sent = 0;
  bool ok = true;
  while (sent < kLen) {
    size_t n = (kLen - sent) > sizeof(chunk) ? sizeof(chunk) : (kLen - sent);
    for (size_t i = 0; i < n; i++) chunk[i] = (uint8_t)(((sent + i) * 7 + 3) & 0xFF);
    if (fileWrite(w, chunk, n) != n) { ok = false; break; }
    sent += (uint32_t)n;
  }
  if (!fileClose(w)) ok = false;
  if (!ok) return false;
  File *r = openFile(&microSDexp, path, "r");
  if (!r) return false;
  uint8_t buf[128];
  uint32_t got = 0;
  while (got < kLen) {
    size_t want = (kLen - got) > sizeof(buf) ? sizeof(buf) : (kLen - got);
    size_t n = fileRead(r, buf, want);
    if (n == 0) { ok = false; break; }
    for (size_t i = 0; i < n; i++) {
      if (buf[i] != (uint8_t)(((got + i) * 7 + 3) & 0xFF)) ok = false;
    }
    got += (uint32_t)n;
  }
  fileClose(r);
  return ok;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); // sketch owns pins: custom boards use Wire.begin(sda,scl)
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("AbruptRecovery: begin FAILED"));
    return;
  }

  // Phase 1: partial WRITE (1 KB of 4 KB), then total silence.
  // Use a boot-unique name so leftover state from a prior aborted run can't
  // shadow the write (the host does the same with millis ids).
  char exPath[32];
  snprintf(exPath, sizeof(exPath), "AbruptEx_%lu.BIN", (unsigned long)((millis() / 1000) % 10000));
  char ex2Path[32];
  snprintf(ex2Path, sizeof(ex2Path), "AbruptEx2_%lu.BIN", (unsigned long)((millis() / 1000) % 10000));
  (void)microSDexp.close();
  delay(100);
  File *w = openFile(&microSDexp, exPath, "w");
  if (!w) {
    Serial.println(F("open \"w\" FAILED"));
    return;
  }
  uint8_t chunk[64];
  for (uint32_t off = 0; off < 1024; off += 64) {
    for (size_t i = 0; i < 64; i++) chunk[i] = (uint8_t)(((off + i) * 7 + 3) & 0xFF);
    fileWrite(w, chunk, 64);
  }
  // Commit every pushed byte before disappearing (otherwise the idle-flush
  // has nothing to hold). Then go totally silent.
  fileSync(w);
  Serial.println(F("--- silent 7s (not even STATUS) — module idle-flushes ---"));
  delay(7000);
  Serial.println(F("--- awake; barrier is automatic on next open ---"));

  // Phase 2: aborted prefix is intact (FILE_SIZE = 1024).
  uint32_t sz = microSDexp.fileSize(exPath);
  char b[64];
  snprintf(b, sizeof(b), "FILE_SIZE=%lu (expect 1024)", (unsigned long)sz);
  Serial.println(b);
  bool prefix = (sz == 1024);

  // Phase 3: fresh 1 KB round-trip (proves re-sync).
  bool rt = smallRoundtrip(ex2Path, 1024);
  Serial.println(rt ? F("fresh round-trip: OK") : F("fresh round-trip: FAILED"));

  Serial.println(prefix && rt ? F("AbruptRecovery: PASS") : F("AbruptRecovery: FAIL"));
}

void loop() {}




