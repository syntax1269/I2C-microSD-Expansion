# BasicReadWrite

Writes a 64 KB file, reads it back, and verifies it byte for byte with a CRC32
(hash `D660AF09`). This is the end-to-end proof that the streaming engine,
flow pacing, and commit tracking all work.

Open the sketch from the Arduino IDE (**File → Examples → I2C_microSD_Expansion
→ BasicReadWrite**), wire the module, and upload. Watch the serial monitor for
the per-chunk progress and the final result.

The test writes `BASICRW.BIN` and removes it when done. It uses the deterministic
pattern `b[i] = (i*7+3) & 0xFF`, so the CRC is reproducible on any card.

Speed scales with the I²C clock; try `400000` and `1000000` in `begin()` to
compare. On ESP32 you can call `Wire.setBufferSize(256)` and
`microSDexp.setReadChunkBytes(256)` for faster reads.
