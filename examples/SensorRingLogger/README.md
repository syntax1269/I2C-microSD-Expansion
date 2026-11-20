# SensorRingLogger

A 500 Hz producer writes samples into a 512-byte SRAM ring while the consumer
drains it with `writeNow()` / `committedBytes()`. When the module is busy
(flash-page erase) `writeNow()` returns 0 and the samples simply wait in the
ring — no SD stall delays the sampling loop.

After 10 seconds the sketch stops producing, drains the ring, waits for the
committed-byte count to catch up, and closes `SENSOR.CSV`. The serial monitor
prints produced / dropped / pushed counts so you can size the ring for your
board (override `RING_BYTES` for ATmega328P builds).

Requires the non-blocking API (the default; do not build with
`I2CMICROSD_NO_ASYNC=1`).
