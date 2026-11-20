# ThresholdBitbangAlert

Reads `adcPA5()` and `temperatureK()` every 500 ms into `ALERT.CSV`. When the
ADC crosses `THRESHOLD` the module runs a 300 ms strobe script on PB2 from its
bit-bang workspace — the host only starts it, then keeps the I²C bus quiet
while the module produces the timed edges.

Wiring: LED + resistor (or scope probe) on PB2; a sensor on PA5 makes the
threshold meaningful. Full builds only (`adcPA5()` / `temperatureK()` are not
available in Tiny/minimal builds). See the
[Bit-Bang Engine wiki](../../wiki/Bit-Bang-Engine.md) for the token table.
