// Two modules on one bus: each instance keeps its own file handle.
#include <Wire.h>
#include <I2C_microSD_Expansion.h>

I2CmicroSD moduleA, moduleB;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin();

  bool a = moduleA.begin(0x6E, 400000);
  bool b = moduleB.begin(0x6C, 400000);
  Serial.print(F("module A: ")); Serial.println(a ? F("yes") : F("no"));
  Serial.print(F("module B: ")); Serial.println(b ? F("yes") : F("no"));

  MicroSDExpFile *fa = moduleA.activeFile();
  MicroSDExpFile *fb = moduleB.activeFile();
  Serial.println(fa == fb ? F("FAIL: shared handle") : F("PASS: independent handles"));
}

void loop() {}
