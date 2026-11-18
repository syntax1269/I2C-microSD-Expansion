// main.cpp — weak empty-library build target (PlatformIO needs a main to link).
// Sketches (tests/examples) define their own strong setup()/loop() which
// override these weak symbols, so Arduino library builds never duplicate them.
// Part of the I²C microSD Expansion host library.
#include <Arduino.h>
#include <I2C_microSD_Expansion.h>

__attribute__((weak)) void setup() {
  // header linkage only (real flow in tests/ sketches).
}

__attribute__((weak)) void loop() {
}



