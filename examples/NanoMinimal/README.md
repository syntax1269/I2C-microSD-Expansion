# NanoMinimal

Runs the Minimal build on an Arduino Nano / Uno (ATmega328P), where flash and RAM
are tight.

The sketch defines `I2CSDMODULE_MINIMAL` before including the library. That
build keeps core file I/O plus `exists()` / `fileSize()` / `isDirectory()`, with
16-byte write chunks and 31-character paths. Wire the module to A4 (SDA) and
A5 (SCL), with a common ground.

See the README's [Build flavors](../../README.md#build-flavors) for details.
