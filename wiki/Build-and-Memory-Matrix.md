# Build & memory matrix

## What these numbers are

This page records the resource cost of the library on every board and build
flavor we compile-test. Each row is one PlatformIO **environment** (`env`) from
the maintainers' compile matrix. The figures are printed by the toolchain
linker at the end of a successful build:

| Column | Meaning |
|---|---|
| **SRAM used** | Static RAM occupied by initialized data and zero-initialized buffers (`.data` + `.bss`) |
| **SRAM free** | Total SRAM on that MCU minus the static usage above — **not** the runtime heap/stack headroom |
| **Flash used** | Program code plus constant data linked into the image |
| **Flash free** | Remaining program space |
| **Flash %** | `Flash used / (Flash used + Flash free)` |

Why this matters here: the I²C microSD Expansion library is designed to run on
very small hosts. An ATtiny85 has **512 bytes of SRAM and 8 KB of flash**, so a
few dozen bytes matter. An ESP32 has megabytes, where the same library is
statistically invisible. The matrix makes that trade-off visible rather than
leaving it to assumption.

> **Note:** The compile matrix is a development/CI artifact used to validate the
> source on many cores. The Arduino Library Manager release contains only the
> library source, docs and examples — you do not need these envs to use the
> library.

## How to read the matrix

The envs fall into two groups:

1. **Library-only compile checks** — these build `src/` with a weak, empty
   `setup()`/`loop()`. They prove the library itself compiles cleanly for that
   core. Their tiny numbers are *not* representative of a real application;
   they are a baseline.
2. **Sketch builds** — these stage a real example or test sketch, so the totals
   include application code plus the library. **Use these rows for capacity
   planning.** Examples: `nano` (BasicReadWrite), `attiny85_example`,
   `esp8266_80` / `esp8266_160`, `esp32async` (the AsyncWebServer demo), and the
   API demos `diagnostics`, `two_instances`, `async_write`, `checked_queries`.

Build flavors also differ:

- **Default** — full API (file core, directory queries, card info, pin control,
  bit-bang, diagnostics, non-blocking API).
- **`I2CSDMODULE_MINIMAL`** — small-footprint build for 328p boards; culls the
  query and diagnostic commands.
- **`Tinyi2cStorage`** — ATtiny/USI build with strict 8.3 paths and small
  buffers.
- The `*_noasync` / `*_async` envs force the `I2CMICROSD_NO_ASYNC` flag on or
  off; the default is on for full builds and off for minimal/tiny builds.

The `_nolib` env omits the library entirely. It is included as a comparison
point, but removing the library also changes which weak symbols the linker
pulls in, so the totals are not a simple subtraction.

## The matrix

All **26 environments build successfully with zero compiler warnings** from our
code. (A few rows show warnings from third-party framework headers and Python
scripts in ESP8266/RP2040 toolchains — those are upstream, not library code.)

| Env | SRAM used | SRAM free | Flash used | Flash free | Flash % | Result |
|---|---:|---:|---:|---:|---:|---|
| esp32dev | 23,948 | 303,732 | 330,040 | 980,680 | 25.2% | PASS |
| statusprobe | 23,948 | 303,732 | 330,040 | 980,680 | 25.2% | PASS |
| roundtrip | 23,948 | 303,732 | 336,216 | 974,504 | 25.7% | PASS |
| attiny85 | 9 | 503 | 248 | 7,944 | 3.0% | PASS |
| attiny85_nolib | 46 | 466 | 542 | 7,650 | 6.6% | PASS |
| attiny84 | 9 | 503 | 246 | 7,946 | 3.0% | PASS |
| attiny45 | 9 | 247 | 248 | 3,848 | 6.1% | PASS |
| attiny25 | 9 | 119 | 244 | 1,804 | 11.9% | PASS |
| attiny85_example | 185 | 327 | 6,126 | 2,066 | 74.8% | PASS |
| nano | 615 | 1,433 | 12,520 | 18,200 | 40.8% | PASS |
| nano_minimal | 614 | 1,434 | 11,942 | 18,778 | 38.9% | PASS |
| uno | 122 | 1,926 | 1,636 | 30,620 | 5.1% | PASS |
| mega2560 | 122 | 8,070 | 2,242 | 251,710 | 0.9% | PASS |
| leonardo | 262 | 2,298 | 4,762 | 23,910 | 16.6% | PASS |
| esp8266 | 28,472 | 53,448 | 261,931 | 499,909 | 34.4% | PASS |
| pico | 41,332 | 229,004 | 4,054 | 2,093,098 | 0.2% | PASS |
| samd | 3,560 | 29,208 | 13,444 | 248,700 | 5.1% | PASS |
| esp8266_80 | 31,168 | 50,752 | 278,743 | 483,097 | 36.6% | PASS |
| esp8266_160 | 31,168 | 50,752 | 278,759 | 483,081 | 36.6% | PASS |
| esp32async | 90,200 | 237,480 | 1,136,935 | 173,785 | 86.7% | PASS |
| diagnostics | 535 | 1,513 | 9,078 | 21,642 | 29.6% | PASS |
| two_instances | 636 | 1,412 | 5,690 | 25,030 | 18.5% | PASS |
| nano_noasync | 614 | 1,434 | 12,520 | 18,200 | 40.8% | PASS |
| nano_minimal_async | 615 | 1,433 | 11,942 | 18,778 | 38.9% | PASS |
| async_write | 563 | 1,485 | 7,204 | 23,516 | 23.5% | PASS |
| checked_queries | 537 | 1,511 | 7,028 | 23,692 | 22.9% | PASS |

## Observations

- **The largest real sketch is still comfortable.** `esp32async` uses the most
  flash of any build at 86.7%, and still leaves about **170 KB free**. That row
  includes ESPAsyncWebServer and AsyncTCP, not just this library.
- **The tightest real sketch is the ATtiny85 example:** 74.8% flash and
  **327 bytes of SRAM free** on a 512-byte part. It uses the `Tinyi2cStorage`
  flavor, which exists precisely to make that possible.
- **The non-blocking API costs nothing when you don't use it.** `nano` and
  `nano_noasync` are byte-identical (12,520 B flash), as are `nano_minimal` and
  `nano_minimal_async`. The linker's section garbage collection removes the
  unused functions, so there is no penalty for shipping the API in the library.
- **Per-instance state is cheap.** `two_instances` (636 B SRAM) is only
  ~21 bytes above `nano` (615 B) — roughly the cost of a second file handle and
  scratch view on AVR.
- **A bare Nano sketch has plenty of room:** `nano` leaves ~1.4 KB SRAM and
  ~18 KB flash for your application.

## Reproducing a row

From a PlatformIO project that includes this library:

```sh
pio run -e <env>
```

PlatformIO prints the `RAM:` and `Flash:` summary on every successful build. Run
it for each env in `platformio.ini` to regenerate the table. Numbers will shift
slightly with library, framework and toolchain versions, so treat them as a
snapshot rather than a guarantee.

## Related reading

- [Home](Home.md) — wiki index
- [Build flavors and memory notes](../README.md#build-flavors) — what
  `I2CSDMODULE_MINIMAL` and `Tinyi2cStorage` change
- [Blocking behavior](../README.md#blocking-behavior-important) — timeouts and
  the non-blocking API
