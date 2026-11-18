# NeoPixelBlit

Drives a WS2812B ("NeoPixel") strip from one of the module's expansion pins —
no LED library, RMT, or SPI peripheral on the host.

The sketch:

1. sets the pin to digital output with `setPinMode(pin, 0)`,
2. clears the bit-bang workspace with `bitbangClear()`,
3. fills a GRB frame and calls `showNeoPixels(pin, frame, count)`,
4. keeps the I²C bus quiet while the module clocks the frame out.

Connect the strip's DIN to PA5 (or PB2/PB3) through a level shifter if it is a
5 V strip. The workspace is 256 bytes, so a frame holds at most 85 pixels.
Always clear before a shorter frame, and never send another command until the
frame has latched (~30 µs/pixel). See the
[Bit-Bang Engine wiki](../../wiki/Bit-Bang-Engine.md).
