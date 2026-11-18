# Protocol Overview

This page summarizes what the library sends over the wire. For the full
command/frame reference, raw transaction recipes, and direct-I²C tutorials, see
the [wiki](../wiki/Home.md) — specifically
[Protocol Reference](../wiki/Protocol-Reference.md),
[Direct I²C Guide](../wiki/Direct-I2C-Guide.md), and
[Status and Polling](../wiki/Status-and-Polling.md).

## STATUS / opDone / result

- `STATUS (0x20)` returns a fixed **12-byte frame** from RAM:
  `rev | state | opDone | error | rxFree(2 LE) | txAvail(2 LE) | result(4 LE)`.
- **ACK ≠ committed.** A WRITE ACK means "copied to RAM". Track operations by
  capturing `opDone` before submitting and polling until it advances. Write
  progress is `STATUS.result` (cumulative committed bytes); the library waits
  for it to reach `baseline + bytes_written` before `close()`/`sync()` succeed.
- Legacy queries (`FILE_SIZE`, `FILE_EXISTS`, `IS_DIRECTORY`) read the sticky
  `STATUS.result` latch after completion. `READ_FILE` reads the size first, then
  drains exactly that many bytes.
- After **5 s** of total bus silence the module flushes, closes the open file,
  and advances `opDone` on its own. The library gates its close barrier on a
  **>4 s** activity gap, so recovery after a host reset does not mis-track the
  counter. See `examples/AbruptRecovery`.

## Directory listing stream

`LIST_DIR (0x08)` streams entries through the TX buffers, using the same drain as
`READ_FILE`. Each entry is `'F'`/`'D'` + NUL-terminated basename + 4-byte size
(LE), terminated by `0xFF`. An empty directory is a single `0xFF`; a missing path
or a file-as-directory returns `ERROR` with no stream.

## Expansion pins (0x10–0x1C)

| Cmd | Name | Payload | Behavior |
|---|---|---|---|
| `0x10` / `0x12` / `0x14` | `PA5/PB2/PB3_SET_MODE` | 1 B mode | `0` = digital output. RAM only; never writes EEPROM. |
| `0x1A` | `SAVEEEPROM` | — | Persist pin modes + I²C address (explicit only; STOP only) |
| `0x11` / `0x13` / `0x15` | `PA5/PB2/PB3_CONTROL` | 1 B value | Drive the pin; NACKed if not an output |
| `0x16` | `I2C_SET_ADDR` | 1 B address | Live address change (`0x6A`–`0x6F`) |
| `0x18` | `ADC_READ_PA5` | — | 12-bit ADC, 1.024 V VREF (~1 mV/LSB) |
| `0x19` | `ADC_READ_PB3` | — | Always `0xFF` (no ADC on PB3) |
| `0x1C` | `PIN_STATE_READ` | — | Mask: PA5 = bit 0, PB2 = bit 1, PB3 = bit 2 |

## Bit-bang engine (0x2B–0x2E, firmware 18.5.2+)

| Cmd | Name | Payload | Behavior |
|---|---|---|---|
| `0x2B` | `BITBANG_SET_MODE` | 1 B pin ID | Select data pin (`PA5=5`, `PB2=2`, `PB3=3`); does not set direction |
| `0x2C` | `BITBANG_BUFFER` | `[offL, offH, len, …data]` | Copy bytes into the 256 B workspace; bytes past the end are dropped |
| `0x2D` | `BITBANG_EXECUTE` | 1 B profile | `1` = NeoPixel, `2` = script; runs with interrupts masked |
| `0x2E` | `BITBANG_CLEAR` | — | Zero workspace and reset the buffered-length mark |

- **One command, one transaction.** The `0x2C` header and payload travel
  together; the library splits long frames into workspace writes sized by
  `I2CMICROSD_BITBANG_MAX` (12 B TinyWireM / 26 B AVR Wire / 120 B elsewhere).
- **Shared 512 B block.** The workspace aliases the formatter buffer, so `0x2C`
  NACKs while a format or probe runs; the master retries.
- **Only buffered bytes are sent.** A high-water mark limits the NeoPixel frame
  to exactly the bytes written, so a short frame cannot push stale pixels —
  clear the workspace before a shorter frame.
- **Frame limit:** 256 B workspace ⇒ at most 255 bytes = **85 pixels**.
- **Watchdog:** keep scripts well inside ~8.2 s; expect I²C NACKs until the frame
  or script finishes.

### Script tokens (profile 0x02)

| Byte | Token | Payload |
|---|---|---|
| `0x01` | `SET_HIGH` | — |
| `0x02` | `SET_LOW` | — |
| `0x03` | `DELAY_US` | `usL, usH` (1–65535) |
| `0x04` | `DELAY_MS` | `ms` (1–255) |
| `0x05` | `LOOP_GOTO` | `target, count` — jump to byte offset, `count` total passes |
| `0x00` | end | — |

`DELAY_US` counts cycles from the module's power-up clock measurement, so it is
accurate at both 10 MHz and 20 MHz. Execution stops at `0x00`, at an unknown
byte, or at the workspace end.

## Timeouts and clock

`timeout = base × 100000 / scl_hz`, never below `base` (worker time does not
track the bus clock), doubled below 50 kHz, capped at 120 s. `FORMAT` uses its own
300 s budget. Per NXP UM10204, Standard-mode starts at 0 Hz and I²C uses static
logic, so arbitrarily low clocks are valid; specifications call ≤100 kHz "Sm",
never "Low-Speed".

## Paths

Paths follow the module's SdFat semantics: case-insensitive 8.3 comparison, `/`
separator, `DIR/FILE` and `/DIR` equivalence, trailing `/` ⇒ directory, LFN up to
259 chars (normal) or strict 8.3 (tiny build). The library rejects overlong
paths before any I²C traffic.
