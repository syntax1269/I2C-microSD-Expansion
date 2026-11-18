// NeoPixelBlit.ino — drive a WS2812B strip from the module's bit-bang engine.
//
// The module bit-bangs one expansion pin; the library pushes raw GRB bytes
// into the module's 256-byte frame workspace and profile 0x01 latches them.
// No LED library, no heap: the frame is a plain array owned by the sketch.
//
// Wiring (module expansion header -> strip):
//   DATA  -> strip DIN        GND -> strip GND        V+ -> 3.3 V or 5 V
//   ~330 R in series with DIN and a 1000 uF cap across the strip supply.
//   A 5 V strip needs a level shifter on DIN (the module drives 3.3 V); a
//   3.3 V strip, or a jewel powered from 3.3 V, can be wired directly.
//
// Two rules the library cannot enforce for you:
//   1. Put the pin in digital-output mode before the first frame
//      (setPinMode). The bit-bang engine writes the port register directly,
//      so a pin left as an input or as the ADC input stays dark.
//   2. The module clocks the frame out from its own loop AFTER it ACKs the
//      execute command, so start no further I2C traffic until the frame is on
//      the wire. FRAME_MS below covers ~30 us/pixel plus the latch reset.
//      Without that pause a following command can split a frame, and the tail
//      pixels keep the colour they had from the frame before.
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

// Pick the pin your board's expansion header exposes: PA5 (pin 1, default),
// PB2 (HW v3 header) or PB3.
#define NEO_PIN MICROSDEXP_PIN_PA5
#define NEO_COUNT 8  // pixels in your strip
// Frame time (7 px ~ 210 us) plus the WS2812B latch reset (~300 us), rounded
// up to whole milliseconds.
#define FRAME_MS (2 + (NEO_COUNT * 32) / 1000)

I2CmicroSD microSDexp;
uint8_t frame[NEO_COUNT * 3];  // one GRB triplet per pixel, green first

// Values are raw 8-bit per channel; scale them yourself for a dimmer look
// (20 % of full green is 51). The module sends exactly what is in the frame.
static void fillFrame(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < NEO_COUNT; i++) {
    frame[i * 3 + 0] = g;
    frame[i * 3 + 1] = r;
    frame[i * 3 + 2] = b;
  }
}

static bool sendFrame() {
  bool ok = microSDexp.showNeoPixels(NEO_PIN, frame, NEO_COUNT);
  delay(FRAME_MS);  // let the module finish the blit (see the header note)
  if (!ok) {
    char b[64];
    snprintf(b, sizeof(b), "  frame FAILED err=%u", (unsigned)microSDexp.lastError());
    Serial.println(b);
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  Wire.begin();  // sketch owns the pins: custom boards use Wire.begin(sda, scl)
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("NeoPixelBlit: begin FAILED"));
    return;
  }
  if (!microSDexp.setPinMode(NEO_PIN, 0)) {  // 0 = digital output
    Serial.println(F("NeoPixelBlit: setPinMode FAILED (pin must be PA5, PB2 or PB3)"));
    return;
  }
  // The mode change queues an EEPROM write on the module; the NVM write halts
  // its CPU for a few ms, so give it room before the first frame.
  delay(150);
  // Zero the workspace: the module sends all buffered bytes, so a longer frame
  // from an earlier run would otherwise leave stale pixels behind this one.
  microSDexp.bitbangClear();

  bool ok = true;

  // 1. all off (also clears whatever the strip was showing)
  fillFrame(0, 0, 0);
  ok = sendFrame() && ok;
  delay(300);

  // 2. the three primaries, so the wire order (G, R, B) is easy to confirm
  fillFrame(255, 0, 0);
  ok = sendFrame() && ok;
  delay(400);
  fillFrame(0, 255, 0);
  ok = sendFrame() && ok;
  delay(400);
  fillFrame(0, 0, 255);
  ok = sendFrame() && ok;
  delay(400);

  // 3. a dim green dot chasing round the strip, twice
  for (int pass = 0; pass < 2 && ok; pass++) {
    for (int p = 0; p < NEO_COUNT && ok; p++) {
      fillFrame(0, 0, 0);
      frame[p * 3 + 1] = 24;  // ~10 % green
      ok = sendFrame() && ok;
      delay(90);
    }
  }

  // 4. all off again
  fillFrame(0, 0, 0);
  ok = sendFrame() && ok;

  Serial.println(ok ? F("NeoPixelBlit: PASS") : F("NeoPixelBlit: FAIL"));
}

void loop() {}
