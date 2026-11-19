// NanoMinimal.ino — Nano (ATmega328P) demo for -DI2CSDMODULE_MINIMAL builds.
// Only core APIs: begin, openFile("r"/"w"/"a"), fileRead/fileWrite/fileClose,
// fileSize/exists/isDirectory. No mkdir/listDir/version/cardInfo etc.
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

I2CmicroSD microSDexp;
static uint8_t chunk[64];
static uint8_t buf[128];

static uint32_t crcUpd(uint32_t c, const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1); }
  return c;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin();  // A4/A5 defaults on Nano
  Serial.println("=== Nano minimal ===");
  if (!microSDexp.begin(0x6E, 400000)) { Serial.println("begin FAILED"); return; }
  // Give the module time to finish mounting the card after power-up;
  // the first write of a fresh boot otherwise races card initialisation.
  delay(8000);
  MicroSDExpFile *w = openFile(&microSDexp, "MINI64.BIN", "w");
  if (!w) { Serial.println("open w FAILED"); return; }
  uint32_t t0 = millis();
  for (uint32_t off = 0; off < 65536; off += 64) {
    for (size_t i = 0; i < 64; i++) chunk[i] = (uint8_t)(((off + i) * 7 + 3) & 0xFF);
    if (fileWrite(w, chunk, 64) != 64) { Serial.println("write FAILED"); return; }
  }
  uint32_t wms = millis() - t0;
  if (!fileClose(w)) { Serial.println("close w FAILED"); return; }
  Serial.print("wrote 65536 in "); Serial.print(wms); Serial.println(" ms");
  MicroSDExpFile *r = openFile(&microSDexp, "MINI64.BIN", "r");
  if (!r) { Serial.println("open r FAILED"); return; }
  uint32_t c = 0xFFFFFFFFUL, got = 0, mism = 0;
  uint32_t t1 = millis();
  while (got < 65536) {
    size_t want = (65536 - got) > sizeof(buf) ? sizeof(buf) : (65536 - got);
    size_t n = fileRead(r, buf, want);
    if (n == 0) break;
    for (size_t i = 0; i < n; i++) if (buf[i] != (uint8_t)(((got + i) * 7 + 3) & 0xFF)) mism++;
    c = crcUpd(c, buf, n);
    got += (uint32_t)n;
  }
  uint32_t rms = millis() - t1;
  fileClose(r);
  uint32_t hash = c ^ 0xFFFFFFFFUL;
  char line[96];
  snprintf(line, sizeof(line), "MINI64 Wms=%lu Rms=%lu got=%lu mism=%lu hash=%08lX %s",
    (unsigned long)wms, (unsigned long)rms, (unsigned long)got, (unsigned long)mism, (unsigned long)hash,
    (got == 65536 && mism == 0 && hash == 0xD660AF09UL) ? "PASS" : "FAIL");
  Serial.println(line);
  // append + queries
  MicroSDExpFile *a = openFile(&microSDexp, "NANO.TXT", "a");
  if (a) { const uint8_t m[] = {'!'}; fileWrite(a, m, 1); fileClose(a); Serial.println("append OK"); }
  Serial.print("size="); Serial.println((unsigned long)microSDexp.fileSize("NANO.TXT"));
  Serial.println(microSDexp.exists("NANO.TXT") ? "exists 1" : "exists 0");
  Serial.println(microSDexp.isDirectory("/") ? "isDir / 1" : "isDir / 0");
  Serial.println("=== Nano minimal done ===");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 5000) { last = millis(); Serial.println("heartbeat mini"); }
  delay(100);
}




