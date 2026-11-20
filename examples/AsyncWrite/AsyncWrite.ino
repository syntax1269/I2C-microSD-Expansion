// Non-blocking write: pump the module from loop(), never sleep on I/O.
#include <Wire.h>
#include <I2C_microSD_Expansion.h>

I2CmicroSD microSDexp;
MicroSDExpOp op;

enum Phase { PHASE_IDLE, PHASE_OPEN, PHASE_WRITE, PHASE_CLOSE };
Phase phase = PHASE_IDLE;

static const uint8_t kPayload[] = "NON-BLOCKING HELLO";
size_t sent = 0;
uint32_t expected = 0;
int verdict = -99;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin();
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("module not found"));
    return;
  }
  if (!microSDexp.beginOpenWrite("ASYNC.TXT", op)) {
    Serial.println(F("submit failed"));
    return;
  }
  phase = PHASE_OPEN;
}

void loop() {
  switch (phase) {
    case PHASE_IDLE:
      break;
    case PHASE_OPEN: {
      int r = microSDexp.pollOp(op);
      if (r == MICROSDEXP_ASYNC_BUSY) break;
      if (r != MICROSDEXP_ASYNC_OK) {
        Serial.print(F("open error ")); Serial.println(r);
        phase = PHASE_IDLE;
        break;
      }
      expected = microSDexp.committedBytes();
      phase = PHASE_WRITE;
      break;
    }
    case PHASE_WRITE:
      sent += microSDexp.writeNow(kPayload + sent, sizeof(kPayload) - sent);
      if (sent == sizeof(kPayload) &&
          microSDexp.committedBytes() >= expected + sizeof(kPayload)) {
        if (microSDexp.beginClose(op)) phase = PHASE_CLOSE;
        else phase = PHASE_IDLE;
      }
      break;
    case PHASE_CLOSE: {
      int r = microSDexp.pollOp(op);
      if (r == MICROSDEXP_ASYNC_BUSY) break;
      verdict = r;
      Serial.println(r == MICROSDEXP_ASYNC_OK ? F("DONE") : F("CLOSE ERROR"));
      phase = PHASE_IDLE;
      break;
    }
  }
}
