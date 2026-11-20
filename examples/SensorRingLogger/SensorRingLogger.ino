// SensorRingLogger.ino — high-frequency logging with a local SRAM ring buffer.
//
// A 500 Hz producer keeps sampling into a small ring in MCU SRAM while the
// consumer drains it with writeNow() / committedBytes() as fast as the module
// will take it. writeNow() returns 0 while the module's RX buffers are full
// (e.g. during a flash-page erase); the samples simply wait in the ring, so
// no SD-card stall ever delays the sampling loop.
//
// The ring is 512 bytes by default (128 samples of 4 bytes = 256 ms of slack).
// On an ATmega328P define RING_BYTES=128 before including this sketch to save
// RAM. The sketch never allocates and never blocks on I/O.
//
// Requires the non-blocking API (default; do NOT build with
// -DI2CMICROSD_NO_ASYNC=1). Full build: no Tiny/minimal-specific calls.

#include <Wire.h>
#include <string.h>
#include <I2C_microSD_Expansion.h>

#if I2CMICROSD_NO_ASYNC
#error "SensorRingLogger needs the non-blocking API (I2CMICROSD_NO_ASYNC=0)"
#endif

#ifndef RING_BYTES
#define RING_BYTES 512
#endif
#ifndef RUN_MS
#define RUN_MS 10000UL
#endif

I2CmicroSD microSDexp;
MicroSDExpOp op;

static uint8_t ring[RING_BYTES];
static uint16_t head = 0, tail = 0, used = 0;
static uint32_t produced = 0, dropped = 0, pushed = 0;
static uint32_t sampleMs = 0, startMs = 0, baseline = 0;
static bool opened = false, closing = false, done = false;

enum Phase { PRODUCE, DRAIN, CLOSE, FINISHED };
static Phase phase = PRODUCE;

// ---- Ring helpers (no heap, no String) ------------------------------------
static void ringPut(const uint8_t *b, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    ring[head++] = b[i];
    if (head == RING_BYTES) head = 0;
  }
  used += n;
}

// Peek at up to cap contiguous bytes; the caller advances only what the
// module actually accepted (writeNow() may take less than offered).
static size_t ringPeek(uint8_t *b, size_t cap) {
  size_t n = used < cap ? used : cap;
  size_t toEnd = RING_BYTES - tail;
  if (n > toEnd) n = toEnd;
  memcpy(b, &ring[tail], n);
  return n;
}

static void ringTake(size_t n) {
  tail += n;
  if (tail >= RING_BYTES) tail -= RING_BYTES;
  used -= n;
}

static void report(const char *msg) {
  char b[96];
  snprintf(b, sizeof(b), "%s produced=%lu dropped=%lu pushed=%lu",
           msg, (unsigned long)produced, (unsigned long)dropped,
           (unsigned long)pushed);
  Serial.println(b);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin();
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("SensorRingLogger: module not found"));
    done = true;
    return;
  }

  if (!microSDexp.beginOpenWrite("SENSOR.CSV", op)) {
    Serial.println(F("SensorRingLogger: open submit failed"));
    done = true;
    return;
  }
  while (microSDexp.pollOp(op) == MICROSDEXP_ASYNC_BUSY) { /* do other work */ }
  if (microSDexp.pollOp(op) != MICROSDEXP_ASYNC_OK || !microSDexp.lastCallOk()) {
    Serial.println(F("SensorRingLogger: open failed"));
    done = true;
    return;
  }
  opened = true;
  baseline = microSDexp.committedBytes();
  startMs = sampleMs = millis();
  Serial.println(F("SensorRingLogger: sampling at 500 Hz for 10 s"));
}

void loop() {
  if (done) return;

  // ---- Producer: one 4-byte sample every 2 ms -----------------------------
  uint32_t now = millis();
  if ((uint16_t)(now - sampleMs) >= 2U) {
    sampleMs = now;
    // Simulated sensor: a 10-bit ramp. Replace with analogRead() if you like.
    uint16_t value = 512U + (((uint16_t)(now / 4UL)) & 0x3FFU);
    uint8_t sample[4] = {
      (uint8_t)(now & 0xFF), (uint8_t)(now >> 8),
      (uint8_t)(value & 0xFF), (uint8_t)(value >> 8)
    };
    if (used + sizeof(sample) <= RING_BYTES) {
      ringPut(sample, sizeof(sample));
      produced++;
    } else {
      dropped++;  // only possible if an SD erase outlasts the ring's slack
    }
  }

  // ---- Consumer: push whatever RX can accept right now --------------------
  if (opened) {
    uint8_t chunk[64];
    size_t n = ringPeek(chunk, sizeof(chunk));
    if (n) {
      size_t accepted = microSDexp.writeNow(chunk, n);
      if (accepted) {
        ringTake(accepted);
        pushed += accepted;
      }  // 0 = module busy: leave the bytes in the ring and retry next pass
    }
  }

  // ---- Finish: drain the ring, wait for commit, close ---------------------
  if (phase == PRODUCE && (now - startMs) >= RUN_MS) {
    phase = DRAIN;
    Serial.println(F("SensorRingLogger: sampling stopped, draining ring"));
  }
  if (phase == DRAIN && used == 0) {
    phase = CLOSE;
    closing = true;
  }
  if (closing && microSDexp.committedBytes() >= baseline + pushed) {
    if (microSDexp.beginClose(op)) {
      int closeResult = MICROSDEXP_ASYNC_BUSY;
      while ((closeResult = microSDexp.pollOp(op)) == MICROSDEXP_ASYNC_BUSY) {
        /* wait */
      }
      report(closeResult == MICROSDEXP_ASYNC_OK
               ? "SensorRingLogger: DONE" : "SensorRingLogger: CLOSE ERROR");
    } else {
      report("SensorRingLogger: CLOSE SUBMIT ERROR");
    }
    done = true;
  }
}
