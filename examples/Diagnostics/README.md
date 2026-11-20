# Diagnostics

Prints a one-page health report to the Serial monitor: module presence,
firmware and protocol version, card/filesystem state, capacity and free
space (now that the module reports a real `freeClusters` value), the bound
I²C address and configured clock, and a communication verdict.

```cpp
I2CmicroSD microSDexp;
void setup() {
  Serial.begin(115200);
  Wire.begin();
  microSDexp.begin(0x6E, 400000);
  microSDexp.diagnostics(Serial);
}
```

Example output:

```text
I2C microSD Expansion
---------------------
Module detected:   yes
Firmware version:  18.8.23
Protocol version:  2
SD card detected:  yes
Filesystem:        FAT32
Capacity:          3810.0 MiB / 3.72 GiB
Free space:        3801.9 MiB / 3.71 GiB
I2C address:       0x6E
I2C speed:         400 kHz (configured)
Communication:     OK
```

`diagnostics()` takes any `Print` stream (`Serial`, a `File`, `Client`, ...),
so it never pulls `Serial` or `String` into the library. It is safe to call
before `begin()`; unreadable fields are shown as `n/a`. In `I2CSDMODULE_MINIMAL`
and `Tinyi2cStorage` builds the version/card fields are shown as
`n/a (build flavor)` because those commands are compiled out of the library.
