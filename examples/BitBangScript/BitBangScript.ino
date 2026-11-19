// BitBangScript.ino — run a token script on the module (bit-bang profile 0x02).
//
// The script lives in the module's 256-byte workspace and the module plays it
// itself after the execute command, so the host is not involved while it runs.
// That is how to make timed edges that I2C latency would blur.
//
// Wiring: whatever the pin should drive - an LED with a series resistor to
// GND, a scope probe, a small piezo, or a NeoPixel data line.
//
// Script tokens (ATtiny3224 bridge v18.5.2 and up):
//   0x01              SET_HIGH
//   0x02              SET_LOW
//   0x03  usL usH     DELAY_US     1..65535 us, counted in CPU cycles
//   0x04  ms          DELAY_MS     1..255 ms
//   0x05  target cnt  LOOP_GOTO    jump to byte offset <target>, <cnt> passes
//   0x00 (or any unrecognised byte) ends the script
//
// Three rules for a script that behaves:
//   1. Put the pin in digital-output mode (setPinMode) and select it for the
//      bit-bang engine (bitbangSetMode) first.
//   2. Clear the workspace before buffering, otherwise the script runs on into
//      whatever the previous script left in the workspace.
//   3. The module masks its millis tick while the script plays and its
//      watchdog is 8 s: keep scripts short, and start no I2C traffic until the
//      script has finished (the delay after each play below).
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

#define BB_PIN MICROSDEXP_PIN_PA5  // PA5 (pin 1), PB2 or PB3

// Blink: 200 ms high, 200 ms low, five passes (2 s).
// Byte offsets: 0 = SET_HIGH, 1 = DELAY_MS(200), 3 = SET_LOW,
//               4 = DELAY_MS(200), 6 = LOOP_GOTO(target 0, 5 passes)
static const uint8_t BLINK[] = {
    0x01, 0x04, 200,  // SET_HIGH, DELAY_MS 200
    0x02, 0x04, 200,  // SET_LOW,  DELAY_MS 200
    0x05, 0x00, 5,    // LOOP_GOTO target 0, five passes
};

// Burst: twenty 1 ms pulses (1 ms high, 1 ms low), then the line idles low.
// Byte offsets: 0 = SET_HIGH, 1 = DELAY_US(1000), 4 = SET_LOW,
//               5 = DELAY_US(1000), 8 = LOOP_GOTO(target 0, 20 passes)
static const uint8_t BURST[] = {
    0x01, 0x03, 0xE8, 0x03,  // SET_HIGH, DELAY_US 1000
    0x02, 0x03, 0xE8, 0x03,  // SET_LOW,  DELAY_US 1000
    0x05, 0x00, 20,          // LOOP_GOTO target 0, twenty passes
};

I2CmicroSD microSDexp;

// Buffer one script and play it once, then wait it out before returning.
static bool playScript(const uint8_t *script, uint8_t len, uint16_t playMs) {
  if (!microSDexp.bitbangBuffer(0, script, len)) {
    char b[64];
    snprintf(b, sizeof(b), "  buffer FAILED err=%u", (unsigned)microSDexp.lastError());
    Serial.println(b);
    return false;
  }
  if (!microSDexp.bitbangExecute(MICROSDEXP_BITBANG_PROFILE_SCRIPT)) {
    char b[64];
    snprintf(b, sizeof(b), "  execute FAILED err=%u", (unsigned)microSDexp.lastError());
    Serial.println(b);
    return false;
  }
  delay(playMs);  // the module is playing the script: keep the bus quiet
  return true;
}

void setup() {
  Serial.begin(115200);
  Wire.begin();  // sketch owns the pins: custom boards use Wire.begin(sda, scl)
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("BitBangScript: begin FAILED"));
    return;
  }
  if (!microSDexp.setPinMode(BB_PIN, 0)) {  // 0 = digital output
    Serial.println(F("BitBangScript: setPinMode FAILED (pin must be PA5, PB2 or PB3)"));
    return;
  }
  // Modes apply immediately in RAM; EEPROM is written only by savePINMode().
  if (!microSDexp.bitbangSetMode(BB_PIN)) {
    Serial.println(F("BitBangScript: bitbangSetMode FAILED"));
    return;
  }
  microSDexp.bitbangClear();  // no stale tokens behind the scripts below

  Serial.println(F("BitBangScript: 5 x 200 ms blink (2 s)"));
  bool ok = playScript(BLINK, (uint8_t)sizeof(BLINK), 2100);
  delay(200);

  Serial.println(F("BitBangScript: 20 x 1 ms pulse burst (40 ms)"));
  microSDexp.bitbangClear();
  ok = playScript(BURST, (uint8_t)sizeof(BURST), 100) && ok;

  Serial.println(ok ? F("BitBangScript: PASS") : F("BitBangScript: FAIL"));
}

void loop() {}
