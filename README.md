# I²C microSD Expansion — Arduino Library

**Add FAT file storage to any Arduino board with just two wires.**

The [I²C microSD Expansion](https://github.com/syntax1269/I2C-microSD-Expansion) module owns
the SD card and the FAT filesystem. Your sketch talks to it over plain I²C: open a
file, write bytes, read them back, list a directory — without an SD library, SPI
bus, level shifter, or a FAT stack in your sketch. The same module also exposes
three spare expansion pins and an on-board bit-bang engine that can drive a
WS2812B strip or play a timed pin script by itself.

This is the **Arduino host library**. There are also
[CircuitPython](https://github.com/syntax1269/I2C_microSD_Expansion_CircuitPython)
and [Qwiic Python](https://github.com/syntax1269/I2C_microSD_Expansion_Qwiic_Py)
ports with the same API and the same `microSDexp` instance name.

---

## Why use this module?

| Task | Without the module | With the module |
|---|---|---|
| Write a log file | SD library + SPI + 4+ pins + level shifting | `openWrite("log.csv")` → `write()` → `close()` |
| Read a config file | Same, plus FAT parsing and RAM buffers | `openRead("config.txt")` → `read()` |
| List a directory | Write your own FAT directory parser | `listDir("/")` returns ready-to-use entries |
| Check card health | Mount the volume, parse CSD registers | `cardInfo()` returns capacity, FAT type, SPI clock |
| Long filenames | LFN support costs code size | Works out of the box (paths up to 259 chars) |
| Drive a WS2812B strip | LED library + RMT/SPI + extra pins | `showNeoPixels(pin, grb, count)` |
| Pulse a spare pin | Another GPIO and a timer | `setPinMode()` / `writePin()` |

One file is open at a time by design. `close()` and `sync()` return only after the
bytes are committed to the card, so a sudden power-off cannot leave a half-written
file.

Not reentrant or interrupt-safe: use one instance from one call site. The single
file handle and shared staging buffer are not protected against nested calls.

---

## Requirements

- **Arduino-compatible board** with an I²C (Wire) peripheral — Uno/Nano, Mega,
  ESP32, ESP8266, RP2040, SAMD, ATtiny (via TinyWireM), and others.
- **I²C microSD Expansion module** with a FAT16/FAT32-formatted microSD card.
- **Module firmware 18.5.x or newer** for the file API; **18.5.2+** for the pin
  and bit-bang API; **18.8.0+** for the banner-only frame behavior and the
  measured-clock report. Call `version()` to see what your module runs.

---

## Wiring

Four wires connect the host to the module:

| Host | Module |
|---|---|
| SDA | SDA |
| SCL | SCL |
| GND | GND |
| 3.3 V **or** 5 V | Power (choose one — never both) |

- **No external level shifter is needed** — the module handles 3.3 V and 5 V
  bus logic.
- **Pull-ups:** 3.3 kΩ are fitted on the module. If your host already supplies
  pull-ups, remove the module's via the underside trace cut. 3.3 kΩ is chosen
  over 4.7 kΩ so 1 MHz (Fm+) works reliably.
- **Power: 5 V *or* 3.3 V — pick one.** Never tie both supplies to the module.
- The expansion pins PA5, PB2, PB3 are 3.3 V logic. A 5 V WS2812B strip needs a
  level shifter on its DIN.

On boards where Wire does not default to the pins you want, call
`Wire.begin(sda, scl)` **before** `microSDexp.begin()`. On boards with default
pins, a plain `Wire.begin()` is enough. The library never reconfigures your pins.

See [docs/wiring.md](docs/wiring.md) for the pinout and board-specific notes.

---

## Install

**Arduino IDE — Library Manager:** Sketch → Include Library → Manage Libraries →
search for `I2C_microSD_Expansion` → Install.

**Arduino IDE — from source:** copy this folder into your sketchbook's
`libraries/` directory (keep the folder name) and restart the IDE.

**PlatformIO:** add the library to `lib_deps`, or drop this folder into your
project's `lib/` directory.

---

## Quick start

```cpp
#include <I2C_microSD_Expansion.h>
#include <Wire.h>

I2CmicroSD microSDexp;

void setup() {
  Serial.begin(115200);
  Wire.begin();                       // or Wire.begin(sda, scl) on custom boards

  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("microSD expansion not found"));
    return;
  }

  char ver[32];
  microSDexp.version(ver, sizeof(ver));
  Serial.print(F("module firmware: "));
  Serial.println(ver);

  // Write a file
  if (microSDexp.openWrite("HELLO.TXT")) {
    microSDexp.write((const uint8_t *)"HELLO-WORLD", 11);
    microSDexp.close();               // returns once bytes are on the card
  }

  // Read it back
  if (microSDexp.openRead("HELLO.TXT")) {
    uint8_t buf[32] = {0};
    size_t n = microSDexp.read(buf, sizeof(buf));
    Serial.printf("read %u bytes: %s\r\n", (unsigned)n, (const char *)buf);
    microSDexp.close();
  }
}

void loop() {}
```

The default address is `0x6E` and the default bus speed is 400 kHz. Speeds of
100 kHz, 400 kHz, and 1 MHz are supported: `microSDexp.begin(0x6E, 1000000)`.
`microSDexp.address()` returns the address `begin()` actually bound, so you can
detect a validated fallback from a re-addressed module.

---

## Using the library

### Writing and reading files

```cpp
// Truncate/create and write
microSDexp.openWrite("data.log");
microSDexp.write((const uint8_t *)"a,b,c\n", 6);
microSDexp.sync();                    // optional: commit without closing
microSDexp.close();

// Append (each write() is an atomic append)
microSDexp.openAppend("log.csv");
microSDexp.write(row, len);
microSDexp.close();

// Read, in chunks
microSDexp.openRead("data.log");
uint8_t buf[64];
size_t got;
while ((got = microSDexp.read(buf, sizeof(buf))) > 0) {
  Serial.write(buf, got);
}
microSDexp.close();
```

`write()` flow-paces automatically: if the module's buffers fill, it waits and
retries instead of dropping bytes. `read()` returns `0` at end of file.

**C-style file API** (useful when you want a file handle):

```cpp
MicroSDExpFile *f = openFile(&microSDexp, "TEST.TXT", "w");
fileWrite(f, buf, len);
fileClose(f);
```

### Directories and queries

```cpp
microSDexp.mkdir("logs");
microSDexp.remove("old.tmp");
microSDexp.rmdir("empty");

if (microSDexp.exists("data.bin")) {
  uint32_t size = microSDexp.fileSize("data.bin");
  bool dir = microSDexp.isDirectory("data.bin");
}

DirEntry entries[16];
uint8_t n = microSDexp.listDir("/", entries, 16);
for (uint8_t i = 0; i < n; i++) {
  Serial.printf("%c %s (%u bytes)\r\n",
                entries[i].isDir ? 'D' : 'F', entries[i].name,
                (unsigned)entries[i].size);
}
```

### Card and module information

```cpp
CardInfo info;
if (microSDexp.cardInfo(&info)) {
  Serial.printf("mounted=%d fatType=%d sectors=%lu sck=%lu Hz\r\n",
                info.mounted, info.fatType,
                (unsigned long)info.sectorCount, (unsigned long)info.sckHz);
}

char v[32];
microSDexp.version(v, sizeof(v));        // e.g. "18.8.22"
uint16_t t = microSDexp.temperatureK();  // whole kelvin; 0xFFFF on error
uint16_t a = microSDexp.adcPA5();        // 0xFFFF on error/unavailable
```

`cardInfo().sckHz` is the negotiated card SPI clock; it is the best predictor of
throughput (see [Performance tips](#performance-tips)).

`temperatureK()` returns the module die temperature as a 2-byte little-endian
**integer in kelvin**, rounded to whole K (for example `294` ≈ 21 °C). It is not an
ADC count and has no fractional scaling; subtract 273.15 to get °C.
Both `temperatureK()` and `adcPA5()` return `0xFFFF` on any failure —
bus error, short read, or unavailable ADC channel — so check for it.

### Expansion pins

```cpp
#include <I2C_microSD_Expansion.h>
// Pin IDs: MICROSDEXP_PIN_PA5 (5), MICROSDEXP_PIN_PB2 (2), MICROSDEXP_PIN_PB3 (3)

microSDexp.setPinMode(MICROSDEXP_PIN_PB2, 0);  // mode 0 = digital output
microSDexp.writePin(MICROSDEXP_PIN_PB2, 1);    // level 1 = high
microSDexp.writePin(MICROSDEXP_PIN_PB2, 0);    // level 0 = low

uint16_t adc = microSDexp.adcPA5();            // PA5's 12-bit ADC input
```

The literal values are the module's protocol values; the library does not define
named aliases for them:

| Call | Value | Meaning |
|---|---|---|
| `setPinMode(pin, 0)` | `0` | Digital output — the only mode this library uses |
| `writePin(pin, 0)` | `0` | Drive the output low |
| `writePin(pin, 1)` | `1` | Drive the output high (any nonzero value means high) |
| `format(0)` | `0` | **Automatic** — keep the card's existing FAT type |
| `format(1)` | `1` | Force a FAT rebuild |

Modes apply **immediately in RAM** and the module never writes EEPROM on a mode
change. When your configuration is final, persist it once:

```cpp
microSDexp.savePINMode();   // writes modes + I²C address to EEPROM
```

### WS2812B (NeoPixel) strips

```cpp
uint8_t strip[8 * 3] = {0};
strip[0] = 0;    // G
strip[1] = 40;   // R
strip[2] = 0;    // B

microSDexp.setPinMode(MICROSDEXP_PIN_PA5, 0);  // 0 = digital output (required)
microSDexp.bitbangClear();                     // clear any longer earlier frame
microSDexp.showNeoPixels(MICROSDEXP_PIN_PA5, strip, 8);
delay(3);                                      // keep the bus quiet while it latches
```

Pass raw **GRB** bytes, three per pixel. The workspace is 256 bytes, so a frame
is at most 255 bytes = **85 pixels**. Always clear the workspace before a shorter
frame, and do not send any I²C command until the frame has been clocked out
(≈30 µs/pixel plus the latch reset). See `examples/NeoPixelBlit/`.

### Timed pin scripts

```cpp
static const uint8_t blink[] = {
  0x01, 0x04, 200,   // SET_HIGH, DELAY_MS 200
  0x02, 0x04, 200,   // SET_LOW,  DELAY_MS 200
  0x05, 0x00, 5,     // LOOP_GOTO offset 0, 5 passes
};
microSDexp.bitbangSetMode(MICROSDEXP_PIN_PA5);
microSDexp.bitbangBuffer(0, blink, sizeof(blink));
microSDexp.bitbangExecute(MICROSDEXP_BITBANG_PROFILE_SCRIPT);
delay(2100);         // module is playing it: keep the bus quiet
```

Scripts run on the module with interrupts masked — see
[docs/protocol.md](docs/protocol.md) and `examples/BitBangScript/` for the token
set (`SET_HIGH`, `SET_LOW`, `DELAY_US`, `DELAY_MS`, `LOOP_GOTO`).

### Housekeeping

```cpp
microSDexp.setClockTime(2026, 11, 18, 12, 0, 0);  // set module clock
microSDexp.format(0 /* automatic */);               // DESTRUCTIVE: full card format
microSDexp.reboot();                               // reboot the module
```

`format()` rebuilds the FAT and can take tens of seconds; it uses its own 300 s
timeout, not the 15 s operation timeout. Mode `0` is automatic (the module keeps the
card's current FAT type); mode `1` forces a FAT rebuild.

### Error handling

```cpp
if (microSDexp.lastError() != MICROSDEXP_OK) {
  Serial.printf("error %d\r\n", (int)microSDexp.lastError());
}
if (microSDexp.fileOpen()) microSDexp.close();
if (microSDexp.stickyError() != MICROSDEXP_OK) { /* real fault */ }
microSDexp.clearError();   // start a fresh error window
`lastError()` is the result of the *most recent* call and may be overwritten by
internal calls (for example, a `listDir()` drain). Use `stickyError()` for the
first failure since `clearError()` when you need an error that internal traffic
cannot hide. `version()` returns `0` with `MICROSDEXP_INVALID` when the caller's
buffer is too small to reach the dotted version token in the banner.

```

| Code | Meaning |
|---|---|
| `MICROSDEXP_OK` | No error |
| `MICROSDEXP_NO_MODULE` | Module not responding |
| `MICROSDEXP_NO_FILE` | No file open |
| `MICROSDEXP_OPEN_FAIL` | Open rejected (bad path) |
| `MICROSDEXP_CARD_ERR` | Card / filesystem error |
| `MICROSDEXP_QUEUE_FULL` | Module queue full |
| `MICROSDEXP_INVALID` | Bad argument |
| `MICROSDEXP_OVERFLOW` | Module buffers full |
| `MICROSDEXP_TIMEOUT` | Operation timed out |

---

## Performance tips

Typical sustained rates at a 1 MHz I²C bus and a card negotiating 10 MHz SPI are
around **30 KiB/s write / 30 KiB/s read** with the polled flow path; cards limited
to 1 MHz SPI sit near 18 KiB/s. The negotiated card clock (`cardInfo().sckHz`)
matters more than the card's capacity or FAT type.

- Use 1 MHz if your wiring supports it; 400 kHz is a safe default.
- For faster reads on ESP32, enlarge Wire's buffer and use the 256-byte drain:

  ```cpp
  Wire.setBufferSize(256);      // before Wire.begin()
  Wire.begin();
  microSDexp.begin(0x6E, 1000000);
  microSDexp.setReadChunkBytes(256);
  ```

  The default 128-byte chunk is safe on every board; 256 halves the number of
  STATUS polls during a bulk read.
- Small transfers are latency-bound; write in larger blocks when you can.
- The first file written after a module reset is slower while the card re-trains.

---

## Build flavors

| Flavor | How | What you get |
|---|---|---|
| Full (default) | — | Complete API; 64 B write chunks, 259-char paths |
| Minimal | `-DI2CSDMODULE_MINIMAL` | Core I/O + exists/fileSize/isDirectory; 16 B chunks, 31-char paths — fits an Uno/Nano (ATmega328P) |
| Tiny | `#define Tinyi2cStorage` | ATtiny85/84 via TinyWireM; 8.3 filenames, 16 B chunks |

The pin and bit-bang API is compiled in every flavor; only the bytes per
workspace write change with the host's I²C buffer
(`I2CMICROSD_BITBANG_MAX`: 12 B TinyWireM / 26 B AVR Wire / 120 B elsewhere).

---

## Examples

| Example | Shows |
|---|---|
| `BasicReadWrite` | 64 KB round trip with CRC32 verification |
| `FileSizeCheck` | Query size and read exactly that many bytes |
| `LongFileName` | Long filenames and nested directories |
| `AbruptRecovery` | Recover cleanly after mid-write power loss |
| `NeoPixelBlit` | WS2812B strip via the bit-bang engine |
| `BitBangScript` | Token scripts for timed pin edges |
| `WebSDModule` | ESP32 Wi-Fi file explorer (synchronous) |
| `WebSDModuleAsync` | ESP32 Wi-Fi file explorer (asynchronous) |
| `WebSDModuleEsp8266` | ESP8266 slim web file browser |
| `NanoMinimal` | Minimal build on Arduino Nano/Uno |
| `Tiny85_16B` | ATtiny85 with Tinyi2cStorage |

Open any example from **File → Examples → I2C_microSD_Expansion** in the IDE.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `begin()` returns false | Wrong address, no common GND, or mixed power | Scan for `0x6E`; common GND; power from 3.3 V **or** 5 V |
| Small file OK, large file truncates | Brown-out under SD write load | One power source only; short, decoupled wiring |
| `TIMEOUT` | Bus too slow for the operation | Try 100/400 kHz; timeouts scale with the clock |
| `OVERFLOW` | Writing faster than the module commits | Slow down; the library flow-paces automatically |
| Card not mounted | exFAT or unformatted card | Reformat to FAT16/FAT32 |
| Strip stays dark | Pin not an output, or wrong data pin | `setPinMode(pin, 0)` first; check the strip DIN |
| Last pixels show an old colour | I²C reached the module mid-frame | Wait a frame time after `showNeoPixels()` |

More detail in the [wiki](wiki/Home.md), especially
[Status and Polling](wiki/Status-and-Polling.md) and
[Direct I²C Guide](wiki/Direct-I2C-Guide.md).

---

## License

MIT. See [LICENSE](LICENSE). Not affiliated with the SD Association.
