// DualModuleMirror.ino — two independent modules on one I²C bus.
//
// moduleA answers at 0x6E, moduleB at 0x6C. Every log frame is appended to
// both cards. If Card A starts failing (bad sector, card ejected, module
// unplugged) its stickyError() latches the first failure, and the sketch
// keeps logging to Card B without missing a frame.
//
// Provisioning (one time): both modules ship at 0x6E. This sketch can set
// moduleB to 0x6C itself — see PROVISION_MODULE_B below. The safe sequence is
// ALWAYS: disconnect module A, provision the remaining module, then reconnect
// A and reset. Changing the address with both modules attached would move both
// to 0x6C and leave the bus in conflict.
//
// Per-instance state is required: do not build with
// -DI2CMICROSD_GLOBAL_STATE=1 for this sketch.

#include <Wire.h>
#include <I2C_microSD_Expansion.h>

// One-time address provisioning. DISABLED by default. Uncomment the next
// line ONLY after disconnecting module A, run once, then comment it out
// again (or just leave it off and reconnect A + reset before normal use).
// #define PROVISION_MODULE_B

I2CmicroSD cardA;   // 0x6E
I2CmicroSD cardB;   // 0x6C

static bool aUp = false, bUp = false;
static uint32_t frameNo = 0;

static bool appendFrame(I2CmicroSD &m, const char *frame) {
  // Each append is its own atomic one-shot transaction on the module, so
  // close() needs no flush and even a sudden power loss costs at most one
  // frame. openAppend() keeps the path in this instance's RAM only.
  if (!m.openAppend("MIRROR.CSV")) return false;
  size_t len = strlen(frame), off = 0;
  while (off < len) {
    size_t n = m.write((const uint8_t *)frame + off, len - off);
    if (n == 0) break;
    off += n;
  }
  m.close();
  return off == len && m.lastCallOk();
}

static void status(const char *label, I2CmicroSD &m, bool up) {
  char b[96];
  snprintf(b, sizeof(b), "%s %s addr=0x%02X sticky=%u",
           label, up ? "UP  " : "DOWN", m.address(), (unsigned)m.stickyError());
  Serial.println(b);
}

#ifdef PROVISION_MODULE_B
// Changes EVERY module still answering at 0x6E, hence the explicit confirm.
bool provisionModuleB() {
  Serial.println(F("PROVISION: disconnect module A from the I2C bus now."));
  Serial.println(F("Type Y and Enter within 10 s to set the remaining module to 0x6C."));
  char answer = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 10000UL) {
    if (Serial.available()) { answer = (char)Serial.read(); break; }
    delay(10);
  }
  if (answer != 'Y' && answer != 'y') {
    Serial.println(F("PROVISION: cancelled — no address was changed"));
    return false;
  }

  Wire.beginTransmission(0x6E);
  Wire.write((uint8_t)MICROSDEXP_CMD_I2C_SET_ADDR);
  Wire.write((uint8_t)0x6C);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("PROVISION: SET_ADDR not acknowledged at 0x6E"));
    return false;
  }
  delay(20);  // the module applies the new address from the STOP condition
  Wire.beginTransmission(0x6C);
  Wire.write((uint8_t)MICROSDEXP_CMD_SAVEEEPROM);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("PROVISION: SAVEEEPROM failed at 0x6C"));
    return false;
  }
  Serial.println(F("PROVISION: module B is now 0x6C (saved to EEPROM)."));
  Serial.println(F("PROVISION: reconnect module A, then press Reset."));
  return true;
}
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin();

#ifdef PROVISION_MODULE_B
  if (!provisionModuleB()) {
    Serial.println(F("DualModuleMirror: provisioning aborted — stopping"));
    while (1) { delay(1000); }
  }
#endif

  aUp = cardA.begin(0x6E, 400000);
  bUp = cardB.begin(0x6C, 400000);
  if (!aUp) Serial.println(F("DualModuleMirror: module A (0x6E) not found"));
  if (!bUp) Serial.println(F("DualModuleMirror: module B (0x6C) not found"));

  // Start with a clean latch so the first real failure is easy to spot.
  cardA.clearError();
  cardB.clearError();
  status("A:", cardA, aUp);
  status("B:", cardB, bUp);
}

void loop() {
  if (!aUp && !bUp) {
    Serial.println(F("DualModuleMirror: no module available — stopping"));
    while (1) { delay(1000); }
  }

  char frame[64];
  snprintf(frame, sizeof(frame), "%lu,%lu\n",
           (unsigned long)++frameNo, (unsigned long)millis());

  if (aUp) {
    bool ok = appendFrame(cardA, frame);
    if (!ok || cardA.stickyError() != MICROSDEXP_OK) {
      aUp = false;  // latch once: from here on Card B serves every frame
      Serial.println(F("DualModuleMirror: Card A FAILED -> failing over to B"));
      status("A:", cardA, aUp);
    }
  }

  if (bUp) {
    bool ok = appendFrame(cardB, frame);
    if (!ok || cardB.stickyError() != MICROSDEXP_OK) {
      bUp = false;
      Serial.println(F("DualModuleMirror: Card B FAILED"));
      status("B:", cardB, bUp);
    }
  }

  Serial.print(F("frame "));
  Serial.print(frameNo);
  Serial.println(aUp ? F(" -> A+B") : F(" -> B only"));
  delay(500);
}
