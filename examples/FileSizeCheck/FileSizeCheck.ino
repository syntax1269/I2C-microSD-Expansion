// FileSizeCheck.ino — FILE_SIZE latch + sized read (no blind reads).
// Writes P1, appends P2, queries the size, then reads exactly that many bytes.
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

I2CmicroSD microSDexp;

void setup() {
  Serial.begin(115200);
  Wire.begin(); // sketch owns pins: custom boards use Wire.begin(sda,scl)
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("FileSizeCheck: begin FAILED"));
    return;
  }

  const char *path = "SizeCheck.BIN";
  const uint8_t p1[] = {'H','E','L','L','O','-','1','2','3','4'};
  const uint8_t p2[] = {'+','A','P','P','E','N','D'};

  File *w = openFile(&microSDexp, path, "w");
  if (!w) {
    Serial.println(F("open \"w\" FAILED"));
    return;
  }
  fileWrite(w, p1, sizeof(p1));
  fileClose(w);

  File *a = openFile(&microSDexp, path, "a");
  if (!a) {
    Serial.println(F("open \"a\" FAILED"));
    return;
  }
  fileWrite(a, p2, sizeof(p2));
  fileClose(a);

  uint32_t sz = microSDexp.fileSize(path);
  char b[64];
  snprintf(b, sizeof(b), "FILE_SIZE=%lu (expect %lu)",
           (unsigned long)sz, (unsigned long)(sizeof(p1) + sizeof(p2)));
  Serial.println(b);
  if (sz != sizeof(p1) + sizeof(p2)) {
    Serial.println(F("FileSizeCheck: FAIL (size)"));
    return;
  }

  File *r = openFile(&microSDexp, path, "r");
  if (!r) {
    Serial.println(F("open \"r\" FAILED"));
    return;
  }
  uint8_t buf[64];
  size_t got = fileRead(r, buf, sz);
  fileClose(r);
  bool ok = got == sz;
  if (ok) {
    for (size_t i = 0; i < sizeof(p1); i++) if (buf[i] != p1[i]) ok = false;
    for (size_t i = 0; i < sizeof(p2); i++) if (buf[sizeof(p1) + i] != p2[i]) ok = false;
  }
  Serial.println(ok ? F("FileSizeCheck: PASS") : F("FileSizeCheck: FAIL"));
}

void loop() {}




