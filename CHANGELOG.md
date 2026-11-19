# Changelog

All notable changes to the Arduino host library are documented here. The format
is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.42] - 2026-11-19

### Added

- **Sticky error API.** `stickyError()` reports the first failure since
  `clearError()`; `lastError()` keeps its per-call semantics for compatibility.
  Internal calls (such as a `listDir()` drain) can no longer hide a real fault
  from a caller that checks the sticky value.
- **Version truncation signal.** `version()` now returns `0` with
  `MICROSDEXP_INVALID` when the caller's buffer is too small to reach a dotted
  version token, instead of returning the first line of the banner (the clock).

## [0.1.41] - 2026-11-18

### Fixed

- **Corrected a false EEPROM claim in two examples.** Firmware confirms that
  `SET_MODE` applies the mode immediately in RAM and only marks an EEPROM slot
  dirty; the NVM write happens solely on the explicit `SAVEEEPROM` command.
  `NeoPixelBlit` and `BitBangScript` no longer claim that a mode change queues an
  EEPROM write, and their unjustified `delay(150)` calls were removed. The
  README/wiki contract (RAM-only applies, explicit save) was correct.

## [0.1.40] - 2026-11-18

### Fixed

- **Tiny85 example build instructions are self-contained.** Replaced the dangling
  "see platformio.ini tiny envs / docs" reference with the exact
  `-DTinyi2cStorage` build flag and the `#define` alternative. The published
  repository intentionally ships no PlatformIO configuration.
- **Documented the 8-second wait in `NanoMinimal`.** It gives the module time to
  finish mounting the card after power-up before the first write.

## [0.1.39] - 2026-11-18

### Fixed

- **`read()` no longer stores its byte count in `_written`.** That member tracks
  bytes written for the open file and is used by `sync()` to compute its commit
  target. The read path was overwriting it with bytes read; the assignment is
  removed because `read()` already returns the length. This also prevents a
  corrupt commit target if `read()` is called on a write handle.

## [0.1.38] - 2026-11-18

### Fixed

- **Consistent sensor error sentinel.** `temperatureK()` now returns `0xFFFF` on
  any failure instead of `0`, and `adcPA5()` uses `0xFFFF` for bus errors and
  short reads as well as for an unavailable channel. `0` is a valid ADC reading
  and could previously be confused with an error. Both functions now share one
  documented sentinel, matching their Python ports.

## [0.1.37] - 2026-11-18

### Fixed

- **Failed STATUS pre-reads no longer alias stale completions.** Every wait loop
  captures `opDone` before submitting its command. If that pre-read fails, the
  library now retries a bounded number of times and fails with
  `MICROSDEXP_NO_MODULE` instead of falling back to `0` — a nonzero `opDone`
  from an unrelated earlier operation would otherwise satisfy the
  `opDone != baseline` test instantly and return a false success. Applies to
  `legacyOp()`, `barrierCloseFlush()`, `format()`, `listDir()`, `appendChunk()`,
  the `openWrite()` / `openRead()` retry loops, and `close()`.

## [0.1.36] - 2026-11-18

### Added

- **`address()` accessor.** Returns the I²C address actually bound by `begin()`,
  including a validated fallback, so a sketch can detect a re-addressed module.

### Documentation

- Stated in the header and README that the library is **not reentrant or
  interrupt-safe**: one instance, one call site. The single file handle and
  shared staging buffer are not protected against nested calls.

## [0.1.35] - 2026-11-18

### Fixed

- **`begin()` no longer adopts arbitrary devices from the fallback address
  scan.** Previously any address in `0x6A`–`0x6F` that returned 12 bytes to
  STATUS was bound as the module. A fallback address must now present a rev-2
  STATUS frame with bounded state/error, and (in builds with the version API)
  a dotted version token from CMD `0x0F`, before it is adopted. The configured
  address is still probed first and unchanged.

## [0.1.34] - 2026-11-18

### Changed

- **`begin()` no longer broadcasts general-call writes.** The nine dummy
  transactions to I²C address `0x00` moved to an explicit `recoverBus()` method,
  documented as best-effort and never called by `begin()`. On a shared bus this
  stops other general-call-capable slaves from being triggered, and avoids nine
  error paths on cores that reject `0x00`. `begin()` is now a read-only STATUS
  and address probe.

## [0.1.33] - 2026-11-18

### Fixed

- **`bitbangBuffer()` workspace bounds.** The old guard rejected only
  `offset + len > 0xFFFF`, so writes between 256 and 65535 were sent to the
  module and silently dropped or NACKed instead of failing at the host. It now
  rejects any range past the 256-byte workspace (`I2CMICROSD_BITBANG_WORKSPACE`)
  locally with `MICROSDEXP_INVALID`, matching the documented 255-byte frame
  limit (85 GRB pixels).
- **`showNeoPixels()` frame-size check.** The early rejection used 255 × 3
  bytes (765) instead of 255 bytes; oversized frames now fail up front with
  `MICROSDEXP_INVALID` instead of partially filling the workspace.

## [0.1.32] - 2026-11-18

### Fixed

- **`setPinMode()` dedup cache could stick after a failed send.** The cache was
  written *before* `sendCmd()`, so a NACK (bit-bang engine busy, bus glitch, or
  module rebooting) left it claiming the mode had been applied; every later call
  with the same mode was then silently skipped and the pin could stay
  misconfigured indefinitely. The cache is now updated only after a confirmed
  ACK, and `begin()` clears all eight entries so a re-begin after a module power
  cycle re-applies modes from scratch.
- **Global `File` typedef collided with `SD.h` / `FS.h`.** `openFile()` now
  returns `MicroSDExpFile *` and the C-style helpers (`fileWrite`, `fileRead`,
  `fileSync`, `fileClose`) take `MicroSDExpFile *` directly; no global `File`
  alias is declared. All shipped examples and tests were updated.
- **Pull-up guidance contradiction.** The WebSDModule example claimed no
  pull-ups were fitted and to use host internal ones, contradicting the README
  and wiring guide. Docs and examples now give one rule: 3.3 kΩ pull-ups are
  fitted on the module; cut the underside trace only if the host also supplies
  pull-ups.

## [0.1.31] - 2026-11-18

### Fixed
- **Global `File` typedef removed.** `openFile()` and the four C-style helpers
  (`fileWrite`, `fileRead`, `fileSync`, `fileClose`) now take/return
  `MicroSDExpFile *` directly instead of aliasing a global `File` type. Sketches
  that also include `SD.h` or an `FS.h`-based component no longer hit a
  conflicting-declaration error. All shipped examples and tests were updated.

- **`version()` against firmware ≥ 18.8.** CMD `0x0F` answers with a 112-byte
  banner whose first line is the measured clock; the old 16-byte read returned
  `"20000000"` instead of the version. `version()` now scans the full banner for
  a dotted version token (`18.8.22` verified), while `firmwareBanner()` and
  `firmwareClockHz()` expose the full text and the measured clock. Legacy
  bare version strings (≤ 18.7.x) are still accepted.
- **`format()` timing.** A real FAT rebuild takes tens of seconds; it now waits
  on its own 300 s budget instead of the 15 s operation timeout.

## [0.1.30] - 2026-11-18

### Changed

- Standardized identifiers across the three host ports (`MICROSDEXP_*`,
  `MicroSDExpError`, `MicroSDExpFile`, `microSDexp`) and documented the unified
  identity in the README, examples, and wiki.
- Added `BitBangScript` and `NeoPixelBlit` examples and refreshed the wiring and
  protocol documentation.

## [0.1.29] - 2026-11-16

### Added

- Expansion-pin API (`setPinMode`, `writePin`, `savePINMode`), the bit-bang
  engine wrappers, and `showNeoPixels()`.
- Streaming file API with explicit `sync()` / `close()` commit semantics.

## [0.1.28] - 2026-11-08

### Added

- Banner-aware `version()` groundwork and `CardInfo.sckHz` reporting so hosts can
  see the negotiated card SPI clock.

## [0.1.0] - 2026-01-01

- Initial public release: file, directory, and query API over I²C.
