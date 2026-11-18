# Changelog

All notable changes to the Arduino host library are documented here. The format
is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.31] - 2026-11-18

### Fixed

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
