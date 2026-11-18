# Arduino Library Wiki

Welcome to the technical documentation for the **I²C microSD Expansion Arduino
library**. The [project README](../README.md) is the place to start if you just
want to use the library; this wiki goes deeper — including how to talk to the
module directly with raw I²C commands if you would rather write your own driver.

## Quick orientation

| I want to… | Go to |
|---|---|
| Install and wire the module | [README: Wiring](../README.md#wiring) · [Wiring guide](../docs/wiring.md) |
| Write / read a file with the library | [README: Using the library](../README.md#using-the-library) |
| Understand the raw command set | [Protocol Reference](Protocol-Reference.md) |
| Send my own I²C commands | [Direct I²C Guide](Direct-I2C-Guide.md) |
| Implement file read/write myself | [File Operations](File-Operations.md) |
| Know when an operation is really done | [Status and Polling](Status-and-Polling.md) |
| Control PA5 / PB2 / PB3 | [Expansion Pins](Expansion-Pins.md) |
| Drive WS2812B or play a pin script | [Bit-Bang Engine](Bit-Bang-Engine.md) |
| Decode an error | [Error Codes](Error-Codes.md) |

## The 60-second summary

The module is an I²C peripheral at address **`0x6E`** (re-addressable to
`0x6A`–`0x6F`). It owns the SD card and FAT filesystem, so the host never runs a
FAT stack. Every exchange is one command byte plus an optional payload; reads use
a repeated START after the command byte.

```cpp
#include <I2C_microSD_Expansion.h>
#include <Wire.h>
I2CmicroSD microSDexp;

void setup() {
  Wire.begin();
  if (!microSDexp.begin(0x6E, 400000)) return;
  microSDexp.openWrite("HELLO.TXT");
  microSDexp.write((const uint8_t *)"HELLO-WORLD", 11);
  microSDexp.close();          // waits until bytes are committed to the card
}
void loop() {}
```

The most important fact about the protocol:

> **ACK ≠ committed.** A WRITE ACK only means the bytes reached the module's RAM.
> Track the `opDone` counter in the STATUS frame and wait for the `result` latch
> to reach your committed-byte target before you trust the write.

[Status and Polling](Status-and-Polling.md) explains this in detail.

## What lives on this page set

- **[Protocol Reference](Protocol-Reference.md)** — the complete command map,
  STATUS / CARD_INFO frame layouts, stream formats, version banner, path rules,
  and timeout math. The authoritative spec for drivers.
- **[Direct I²C Guide](Direct-I2C-Guide.md)** — transaction shapes, a first
  conversation, and common pitfalls. Start here if you are writing a driver.
- **[File Operations](File-Operations.md)** — exact sequences to create, write,
  read, list, and query files over raw I²C, including flow control.
- **[Status and Polling](Status-and-Polling.md)** — `opDone`, the `result` latch,
  NACK handling, timeouts, and crash recovery.
- **[Expansion Pins](Expansion-Pins.md)** — pin IDs, modes, control, ADC, EEPROM
  persistence, and address changes.
- **[Bit-Bang Engine](Bit-Bang-Engine.md)** — WS2812B frames and the token script
  language, with the 256-byte workspace and timing constraints.
- **[Error Codes](Error-Codes.md)** — normalized enums, STATUS errors, Wire code
  mapping, and troubleshooting.

## Firmware compatibility

| Feature | Minimum module firmware |
|---|---|
| File API | 18.5.x |
| Expansion pins + bit-bang | 18.5.2 |
| Banner-only frames + measured clock | 18.8.0 |

Check with `microSDexp.version(buf, sizeof(buf))` at startup.
