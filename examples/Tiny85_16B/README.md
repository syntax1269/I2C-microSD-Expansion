# Tiny85_16B

Runs the library on an ATtiny85 using TinyWireM and the Tiny build flavor.

The sketch creates a file, writes four 16-byte blocks, closes it, checks the
file size, reads it back, verifies it, and removes it. It uses about 64 bytes of
stack and blinks a PB1 LED to show progress.

This example defines `Tinyi2cStorage` to select the tiny build: 8.3-only
filenames, 16-byte chunks, and a culled API surface. See the README's
[Build flavors](../../README.md#build-flavors) for the other options.
