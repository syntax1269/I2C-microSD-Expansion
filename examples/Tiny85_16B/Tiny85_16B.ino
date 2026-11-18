// Tiny85_16B.ino — M4 curtailed USI build (Tinyi2cStorage).
// 8.3 names only, one '/' max, 16 B cast-in-place chunks, static buffers.
// Flow: create -> write 4x16 B (64 B) -> close -> size -> read -> verify -> remove.
// Wire 85 to the module: PB0=SDA, PB2=SCL @ 100 kHz (fallback 50/25/10 kHz Sm).
// Define -DTinyi2cStorage (see platformio.ini tiny envs / docs).
#if !defined(Tinyi2cStorage)
#error "Tiny85_16B needs #define Tinyi2cStorage before #include"
#endif
#include <I2C_microSD_Expansion.h>

static I2CmicroSD microSDexp;
static uint8_t tx32[16];
static uint8_t rx32[16];

void setup() {
  // No Serial on PB2 (SCL). Debug via PB1/PB3 LEDs in the final bring-up.
  pinMode(1, OUTPUT);
  if (!microSDexp.begin(0x6E, 100000)) {
    digitalWrite(1, HIGH);  // solid = begin failed
    return;
  }
  digitalWrite(1, LOW);  // off = began

  // create + write 4x16 B (64 B total), pattern b[i] = 0x40+i
  File *w = openFile(&microSDexp, "T.BIN", "w");
  if (!w) {
    digitalWrite(1, HIGH);
    return;
  }
  bool ok = true;
  for (uint8_t round = 0; round < 4; round++) {
    for (uint8_t i = 0; i < 16; i++) {
      tx32[i] = (uint8_t)(((round << 4) | i) & 0xFF);  // <<4 indexing, no div
    }
    size_t n = fileWrite(w, tx32, 16);
    if (n != 16) ok = false;
  }
  ok = ok && fileClose(w);
  if (!ok) {
    digitalWrite(1, HIGH);
    return;
  }

  // size must be 64
  uint32_t sz = microSDexp.fileSize("T.BIN");
  if (sz != 64) {
    digitalWrite(1, HIGH);
    while (1) {}
  }

  // read back + verify byte-perfect
  File *r = openFile(&microSDexp, "T.BIN", "r");
  if (!r) {
    digitalWrite(1, HIGH);
    return;
  }
  ok = true;
  for (uint8_t round = 0; round < 4; round++) {
    size_t n = fileRead(r, rx32, 16);
    if (n != 16) ok = false;
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t want = (uint8_t)(((round << 4) | i) & 0xFF);
      if (rx32[i] != want) ok = false;
    }
  }
  ok = ok && fileClose(r);
  ok = ok && microSDexp.remove("T.BIN");
  if (!ok) {
    digitalWrite(1, HIGH);
    return;
  }
  // PASS: blink PB1 3x
  for (uint8_t b = 0; b < 3; b++) {
    digitalWrite(1, HIGH);
    delay(200);
    digitalWrite(1, LOW);
    delay(200);
  }
}

void loop() {}




