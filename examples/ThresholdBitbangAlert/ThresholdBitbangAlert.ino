// ThresholdBitbangAlert.ino — the module alone does the timed alert.
//
// Every 500 ms the host reads adcPA5() and temperatureK() from the module
// and appends a CSV row. When the ADC crosses THRESHOLD it loads a short
// strobe script into the module's bit-bang workspace and starts it on PB2.
// The module then plays the 300 ms pulse train by itself; the host does
// local work during that time and issues no I²C traffic until it is over.
//
// This is the unique hardware trick: precise, timed pin edges come from the
// module, so bus latency cannot blur them, while the host is free to do
// other non-I²C work. Keep the I²C bus quiet for the whole script — the
// module's script engine and the I²C slave share the same interrupt context.
//
// Wiring: LED + resistor (or a scope probe) on PB2. A sensor on PA5 makes
// the threshold meaningful; without one the raw ADC just floats.
// Full build only: adcPA5()/temperatureK() are not in Tiny/minimal builds.

#include <Wire.h>
#include <I2C_microSD_Expansion.h>

#ifndef THRESHOLD
#define THRESHOLD 3000U
#endif
#ifndef RUN_MS
#define RUN_MS 30000UL
#endif

#define ALERT_PIN MICROSDEXP_PIN_PB2

// Strobe: 3 x (50 ms high, 50 ms low) = 300 ms.
// Offsets: 0 SET_HIGH, 1 DELAY_MS(50), 3 SET_LOW, 4 DELAY_MS(50),
//          6 LOOP_GOTO(target 0, 3 passes).
static const uint8_t STROBE[] = {
  0x01, 0x04, 50,
  0x02, 0x04, 50,
  0x05, 0x00, 3
};
static const uint16_t STROBE_MS = 310;  // script + a little slack

I2CmicroSD microSDexp;
static uint32_t startMs = 0, alerts = 0;

// Non-I²C work the host can do while the module plays the script.
static volatile uint32_t workSink;  // one volatile write; no -Wvolatile warning
static void localWork(uint16_t ms) {
  uint32_t until = millis() + ms;
  uint32_t acc = 0;
  while (millis() < until) { acc++; }  // stand-in for real computation
  workSink = acc;
}

static bool playStrobe() {
  if (!microSDexp.bitbangBuffer(0, STROBE, (uint8_t)sizeof(STROBE))) return false;
  if (!microSDexp.bitbangExecute(MICROSDEXP_BITBANG_PROFILE_SCRIPT)) return false;
  localWork(STROBE_MS);  // NO I2C until the module has finished the script
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin();
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("ThresholdBitbangAlert: module not found"));
    while (1) { delay(1000); }
  }

  // Pin must be an output and selected for the bit-bang engine first.
  if (!microSDexp.setPinMode(ALERT_PIN, 0) ||
      !microSDexp.bitbangSetMode(ALERT_PIN)) {
    Serial.println(F("ThresholdBitbangAlert: pin setup failed"));
    while (1) { delay(1000); }
  }
  microSDexp.bitbangClear();  // no stale tokens behind the strobe

  if (!microSDexp.openWrite("ALERT.CSV")) {
    Serial.println(F("ThresholdBitbangAlert: open failed"));
    while (1) { delay(1000); }
  }
  static const uint8_t HEADER[] = "millis,adc,tempK,alert\n";
  microSDexp.write(HEADER, sizeof(HEADER) - 1);
  startMs = millis();
  Serial.println(F("ThresholdBitbangAlert: logging to ALERT.CSV"));
}

void loop() {
  if ((millis() - startMs) >= RUN_MS) {
    microSDexp.sync();
    microSDexp.close();
    char b[64];
    snprintf(b, sizeof(b), "ThresholdBitbangAlert: DONE alerts=%lu",
             (unsigned long)alerts);
    Serial.println(b);
    while (1) { delay(1000); }
  }

  uint16_t adc = microSDexp.adcPA5();
  uint16_t tempK = microSDexp.temperatureK();
  bool alert = (adc != 0xFFFF && adc >= THRESHOLD);

  char row[64];
  int n = snprintf(row, sizeof(row), "%lu,%u,%u,%u\n",
                   (unsigned long)millis(), (unsigned)adc,
                   (unsigned)tempK, (unsigned)(alert ? 1 : 0));
  if (n > 0) microSDexp.write((const uint8_t *)row, (size_t)n);

  if (alert) {
    alerts++;
    if (!playStrobe()) {
      Serial.println(F("ThresholdBitbangAlert: strobe FAILED"));
    }
  }
  delay(500);  // ~2 Hz sample cadence when no alert is firing
}
