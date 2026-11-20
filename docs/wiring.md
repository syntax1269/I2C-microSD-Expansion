# Wiring Guide

The module needs only four connections to your host. It handles logic-level
translation, so no external level shifter is required.

## Basic connection

| Host pin | Module pin | Notes |
|---|---|---|
| SDA | SDA | I²C data |
| SCL | SCL | I²C clock |
| GND | GND | Must be common between host and module |
| 3.3 V **or** 5 V | Power | Choose **one** supply; never connect both |

### Power rules

- Power the module from **5 V or 3.3 V**, but never both at the same time.
- The SD card draws short current peaks while writing. Use a short, adequately
  sized power lead; brown-outs under write load show up as truncated large files.
- Do not back-feed 5 V into the module's 3.3 V rail.

### Pull-ups

The module ships with **3.3 kΩ** I²C pull-ups fitted. They are sized for 1 MHz
(Fm+) operation. If your host already has pull-ups, you can disable the module's
by cutting the underside trace. Do not stack too many pull-up resistances — the
total bus capacitance matters at 1 MHz.

## Expansion pins

Three spare MCU pins are brought out: **PA5**, **PB2**, **PB3**.

| Signal | Pin ID | Capabilities |
|---|---|---|
| PA5 | `5` | Digital output, PWM, 12-bit ADC input |
| PB2 | `2` | Digital output, PWM |
| PB3 | `3` | Digital output (no ADC on this part) |

These are **3.3 V logic**. To drive a 5 V WS2812B strip, use a level shifter on
the strip's DIN. Pin IDs are the MCU port numbers, not connector positions.

See [Expansion Pins](../wiki/Expansion-Pins.md) for the raw commands.

## Board notes

- **Uno / Nano (ATmega328P):** SDA = A4, SCL = A5. Use the Minimal build
  (`-DI2CSDMODULE_MINIMAL`) if flash is tight. Wire default pins are used, so
  plain `Wire.begin()` works.
- **ESP32 / ESP32-S3:** default Wire pins vary by board; call
  `Wire.begin(sda, scl)` with the pins you wired before `microSDexp.begin()`.
  For 256-byte reads, call `Wire.setBufferSize(256)` before `Wire.begin()`.
- **ESP8266:** default SDA = GPIO4 (D2), SCL = GPIO5 (D1). The slim web example
  in `examples/WebSDModuleEsp8266/` shows a complete setup.
- **ATtiny85/84:** use TinyWireM and `#define Tinyi2cStorage`; filenames are 8.3
  only. See `examples/Tiny85_16B/`.
- **Other boards (SAMD, RP2040, etc.):** call `Wire.begin()` (or the platform's
  equivalent) before `microSDexp.begin()`.

## Raspberry Pi hosts (clock stretching)

The module uses standard I²C clock stretching (a few microseconds per byte).
Raspberry Pi hosts built on the **Broadcom BSC** peripheral
(**BCM2835–BCM2711**, Pi 1/2/3/4 and matching Compute Modules) have a **silicon
bug** that can corrupt transactions when a slave stretches the clock. Add
`dtoverlay=i2c-gpio` to `/boot/config.txt` (`/boot/firmware/config.txt` on
Bookworm) and reboot to use the bit-banged bus, or use a Pi 5, whose RP1
southbridge uses Synopsys DesignWare I²C and is unaffected. See
[Clock stretching and Raspberry Pi hosts](../wiki/Protocol-Reference.md#clock-stretching-and-raspberry-pi-hosts).

## Before you start

1. Format the microSD card as **FAT16 or FAT32** (exFAT is not supported).
2. Verify the module is visible with an I²C scanner — it should answer at
   `0x6E` (or one of `0x6A`–`0x6F` if re-addressed).
3. Check `version()` and `cardInfo()` in your sketch to confirm firmware and
   card mounting before writing data.

If `begin()` fails, the [troubleshooting table](../README.md#troubleshooting)
covers the usual causes.
