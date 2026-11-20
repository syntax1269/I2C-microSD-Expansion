// Compile+run check for the checked query overloads.
#include <Wire.h>
#include <I2C_microSD_Expansion.h>

I2CmicroSD microSDexp;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("module not found"));
    return;
  }

  bool present = false;
  if (microSDexp.exists("HELLO.TXT", &present) && microSDexp.lastCallOk()) {
    Serial.print(F("exists: ")); Serial.println(present ? F("yes") : F("no"));
  } else {
    Serial.println(F("exists query failed"));
  }

  uint32_t size = 0;
  if (microSDexp.fileSize("HELLO.TXT", &size)) {
    Serial.print(F("size: ")); Serial.println(size);
  }

  bool dir = false;
  if (microSDexp.isDirectory("DIR", &dir)) {
    Serial.print(F("dir: ")); Serial.println(dir ? F("yes") : F("no"));
  }
}

void loop() {}
