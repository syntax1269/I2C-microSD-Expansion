// Print a one-page health report from the I2C microSD Expansion module.
// Works on any board with a Serial monitor; no failure path is needed —
// diagnostics() reports n/a for fields it could not read.
#include <Wire.h>
#include <I2C_microSD_Expansion.h>

I2CmicroSD microSDexp;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin();

  bool found = microSDexp.begin(0x6E, 400000);
  if (!found) {
    Serial.println(F("Module not found at 0x6E (or 0x6A-0x6F)"));
    Serial.println(F("Showing partial diagnostics anyway:"));
  }
  microSDexp.diagnostics(Serial);
}

void loop() {}
