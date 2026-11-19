#ifndef I2C_MICROSD_EXPANSION_CONFIG_H
#define I2C_MICROSD_EXPANSION_CONFIG_H
// I2C_microSD_Expansion_Config.h — sketch knobs only. No MCU macros.
// Timeout scaling: timeoutScaled = base x 100000/sclHz, >=2x below 50 kHz Sm.
//   Bus-busy recovery is opt-in via recoverBus(); per-core Wire error table
//   also lands here / in protocol.h.

#include <stdint.h>

#define I2CMICROSD_DEFAULT_ADDRESS 0x6E
#define I2CMICROSD_CHUNK_BYTES 64        // master push size, normal build (module blocks are 128 B)
// Default bytes requested per READ drain (firmware v18.8.21 can stage 256).
// 128 keeps every host's default Wire receive buffer safe; call
// setReadChunkBytes(256) after Wire.setBufferSize(256) to use the full 256.
#define I2CMICROSD_READ_CHUNK_BYTES 128
#define I2CMICROSD_READ_CHUNK_MAX 256    // hard per-core cap (overridden below)
#define I2CMICROSD_POLL_MS 10            // STATUS poll pause
#define I2CMICROSD_FLOW_POLL_MS 2        // rxFree re-check while write-blocked
#define I2CMICROSD_OP_TIMEOUT_MS 15000   // x (100000/sclHz) at low clocks; >=2x below 50 kHz
#define I2CMICROSD_COMMIT_TIMEOUT_MS 30000
// A full FAT rebuild takes tens of seconds on a large card; format() waits on
// its own budget instead of I2CMICROSD_OP_TIMEOUT_MS.
#define I2CMICROSD_FORMAT_TIMEOUT_MS 300000
// CMD_VERSION answers with a fixed 112 B banner buffer on firmware >= 18.8.0.
#define I2CMICROSD_VERSION_BANNER_BYTES 112
#define I2CMICROSD_PATH_MAX 260          // normal (LFN) — Tinyi2cStorage overrides below

// Tinyi2cStorage (curtailed USI tiny build).
#ifdef Tinyi2cStorage
#undef I2CMICROSD_CHUNK_BYTES
#define I2CMICROSD_CHUNK_BYTES 16        // USI tiny: 16-B chunks (<<4 indexing, no div)
#undef I2CMICROSD_READ_CHUNK_BYTES
#define I2CMICROSD_READ_CHUNK_BYTES 16   // USI buffer-safe, one drain per request
#undef I2CMICROSD_READ_CHUNK_MAX
#define I2CMICROSD_READ_CHUNK_MAX 16
#undef I2CMICROSD_PATH_MAX
#define I2CMICROSD_PATH_MAX 25           // 8.3, split name rules, path <=24 incl. NUL=25
#define I2CMICROSD_POLLS_BEFORE_CLOSE 0  // culled diagnostics
#endif

// I2CSDMODULE_MINIMAL — sketch-compiled memory-optimized build for 328p (Nano/Uno).
// Keeps only: begin, open("r"/"w"/"a"), fileRead/fileWrite/fileClose/fileSync,
// fileSize/exists/isDirectory, lastError/fileOpen. Culls mkdir/rmdir/remove/
// listDir/version/temperatureK/adcPA5/cardInfo/setClockTime/reboot/format
// and buffers the write/read paths at 16/32 B for the AVR 32-B Wire slots.
// Enable with -DI2CSDMODULE_MINIMAL (or #define before #include). Tinyi2cStorage
// remains the separate USI tiny variant.
#ifdef I2CSDMODULE_MINIMAL
#undef I2CMICROSD_CHUNK_BYTES
#define I2CMICROSD_CHUNK_BYTES 16
#undef I2CMICROSD_READ_CHUNK_BYTES
#define I2CMICROSD_READ_CHUNK_BYTES 32   // AVR Wire receive buffer is 32 B
#undef I2CMICROSD_READ_CHUNK_MAX
#define I2CMICROSD_READ_CHUNK_MAX 32
#undef I2CMICROSD_PATH_MAX
#define I2CMICROSD_PATH_MAX 31
#endif

// Module-side bit-bang workspace size in bytes. The frame is clocked out from
// this buffer, so a frame is at most 255 bytes (85 GRB pixels).
#define I2CMICROSD_BITBANG_WORKSPACE 256

// Bit-bang (WS2812B) frame payload per workspace write, in bytes. One write
// is 1 command byte + 3 header bytes (offset LSB, offset MSB, length) +
// payload, and the module reads the header and the payload as one command:
// they must travel in the same bus transaction, and that transaction must fit
// the host's I2C transmit buffer. Capped per core with a small margin:
// TinyWireM 17 data bytes, AVR Wire 32 B, ESP/ESP8266 Wire 128 B.
#if defined(Tinyi2cStorage)
#undef I2CMICROSD_BITBANG_MAX
#define I2CMICROSD_BITBANG_MAX 12
#elif defined(__AVR__)
#undef I2CMICROSD_BITBANG_MAX
#define I2CMICROSD_BITBANG_MAX 26
#else
#undef I2CMICROSD_BITBANG_MAX
#define I2CMICROSD_BITBANG_MAX 120
#endif

// AVR 328p families (Nano/Uno, 2 KB SRAM): Arduino Wire hard-codes a 32-byte
// TX/RX buffer that cannot grow at runtime. Every transaction is 1 cmd byte
// + payload, so payload per transaction must be <= 31 bytes to stay inside
// the buffer. Use 16-B chunks (1+16=17 < 32, like Tiny) and cap paths to
// 30 chars (30+2=32). Anything larger is silently dropped by Wire.
#if defined(__AVR__) && !defined(Tinyi2cStorage)
#undef I2CMICROSD_CHUNK_BYTES
#define I2CMICROSD_CHUNK_BYTES 16
#undef I2CMICROSD_READ_CHUNK_BYTES
#define I2CMICROSD_READ_CHUNK_BYTES 32   // one CMD_READ drains one 32-B Wire slot
#undef I2CMICROSD_READ_CHUNK_MAX
#define I2CMICROSD_READ_CHUNK_MAX 32
#undef I2CMICROSD_PATH_MAX
#define I2CMICROSD_PATH_MAX 31
#endif

#endif  // I2C_MICROSD_EXPANSION_CONFIG_H



