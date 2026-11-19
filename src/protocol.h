#ifndef I2C_MICROSD_EXPANSION_PROTOCOL_H
#define I2C_MICROSD_EXPANSION_PROTOCOL_H
// protocol.h — I²C microSD Expansion wire-format constants and packed structs.
// Fixed-width types, packed wire structs, little-endian via shifts.

#include <stdint.h>

// ---- Legacy command family (0x01-0x0B, 0x0E) ----
#define MICROSDEXP_CMD_FILENAME     0x01  // write: filename bytes; STOP stores path
#define MICROSDEXP_CMD_WRITE_FILE   0x02  // write: payload + STOP -> create/overwrite, write, close
#define MICROSDEXP_CMD_READ_FILE    0x03  // STOP -> open for read, stream via TX buffers
#define MICROSDEXP_CMD_APPEND_FILE  0x04  // write: payload + STOP -> append, write, close
#define MICROSDEXP_CMD_FILE_SIZE    0x05  // STOP -> size (4 B LE) in STATUS.result
#define MICROSDEXP_CMD_FILE_EXISTS  0x06  // STOP -> 1=exists, 0=not found in STATUS.result
#define MICROSDEXP_CMD_IS_DIRECTORY 0x07  // STOP -> 1=directory, 0=not in STATUS.result
#define MICROSDEXP_CMD_LIST_DIR     0x08  // STOP -> TX-stream entries: 'F'/'D' + NUL basename + 4 B size LE, 0xFF term (v18.7.6)
#define MICROSDEXP_CMD_MAKE_DIR     0x09  // STOP -> create directory at Filename
#define MICROSDEXP_CMD_REMOVE_DIR   0x0A  // STOP -> remove empty directory
#define MICROSDEXP_CMD_REMOVE_FILE  0x0B  // STOP -> delete file at Filename
// 0x0C, 0x0D unused
#define MICROSDEXP_CMD_CLOCK_SET    0x0E  // write: 6 B [Y,M,D,h,m,s]; STOP applies
#define MICROSDEXP_CMD_VERSION      0x0F  // read: NUL-free version string ("18.7.6")

// ---- Pin / extension commands ----
#define MICROSDEXP_CMD_PA5_SET_MODE     0x10  // write: 1 B mode
#define MICROSDEXP_CMD_PA5_CONTROL      0x11  // write: mode-dependent control
#define MICROSDEXP_CMD_PB2_SET_MODE     0x12
#define MICROSDEXP_CMD_PB2_CONTROL      0x13
#define MICROSDEXP_CMD_PB3_SET_MODE     0x14
#define MICROSDEXP_CMD_PB3_CONTROL      0x15
#define MICROSDEXP_CMD_I2C_SET_ADDR     0x16  // write: 1 B new address
#define MICROSDEXP_CMD_TEMP_READ        0x17  // read: temperature
#define MICROSDEXP_CMD_ADC_READ_PA5     0x18  // read: 12-bit ADC (PA5)
#define MICROSDEXP_CMD_ADC_READ_PB3     0x19  // read: 0xFF (PB3 has no ADC on 3224)
#define MICROSDEXP_CMD_PIN_STATE_READ   0x1C  // read: pin-state mask
#define MICROSDEXP_CMD_SAVEEEPROM       0x1A  // STOP -> persist pin modes + I2C address to EEPROM

// ---- Async protocol (rev 2) ----
#define MICROSDEXP_CMD_STATUS       0x20  // read: 12-byte status frame
#define MICROSDEXP_CMD_OPEN_WRITE   0x21  // write: filename bytes; STOP enqueues OPEN (RDWR, truncate)
#define MICROSDEXP_CMD_WRITE        0x22  // write: payload block -> RX buffers
#define MICROSDEXP_CMD_READ         0x23  // write: request block; read: drain TX buffer
#define MICROSDEXP_CMD_CLOSE_FLUSH  0x24  // write: STOP enqueues CLOSE (flush + close)
#define MICROSDEXP_CMD_OPEN_READ    0x25  // write: filename bytes; STOP enqueues OPEN (read-only)
#define MICROSDEXP_CMD_DIAG         0x26  // read: buffer/queue/counter diagnostics (debug builds)
#define MICROSDEXP_CMD_DIAG2        0x27  // read: RX capture diagnostics (debug builds)
#define MICROSDEXP_CMD_FORMAT       0x28  // write: 1 B mode (0=auto, 1=force FAT); STOP enqueues format
#define MICROSDEXP_CMD_CARD_INFO    0x29  // read: 24-byte card/volume snapshot (le always)
#define MICROSDEXP_CMD_SD_DIAG      0x2A  // read: SD init diagnostics (debug builds)
#define MICROSDEXP_CMD_BITBANG_SET_MODE  0x2B  // write: 1 B pin ID
#define MICROSDEXP_CMD_BITBANG_BUFFER    0x2C  // write: [offLSB, offMSB, len, ...data]
#define MICROSDEXP_CMD_BITBANG_EXECUTE   0x2D  // write: 1 B profile ID
#define MICROSDEXP_CMD_BITBANG_CLEAR     0x2E  // write: flush workspace to zeros
#define MICROSDEXP_CMD_REBOOT       0x2F  // write: reboot module (WDT reset from loop)

// Module expansion pin IDs (as used by the bit-bang and pin-control commands).
#define MICROSDEXP_PIN_PA5  5
#define MICROSDEXP_PIN_PB2  2
#define MICROSDEXP_PIN_PB3  3
// Bit-bang engine profiles.
#define MICROSDEXP_BITBANG_PROFILE_NEOPIXEL 0x01
#define MICROSDEXP_BITBANG_PROFILE_SCRIPT   0x02

// ---- STATUS.state ----
#define MICROSDEXP_ST_IDLE      0  // no active storage operation
#define MICROSDEXP_ST_ACCEPTED  1  // request/payload safely copied into RAM
#define MICROSDEXP_ST_BUSY_RX   2  // RX buffers/queue occupied, being consumed
#define MICROSDEXP_ST_BUSY_SD   3  // worker is servicing SD/SPI
#define MICROSDEXP_ST_TX_READY  4  // response bytes staged for the master
#define MICROSDEXP_ST_COMPLETE  5  // last operation finished successfully
#define MICROSDEXP_ST_ERROR     6  // last operation failed (see error byte)
#define MICROSDEXP_ST_OVERFLOW  7  // RX buffering exceeded; accepted bytes preserved

// ---- STATUS.error ----
#define MICROSDEXP_ERR_NONE      0
#define MICROSDEXP_ERR_NO_FILE   1  // read/write with no file open
#define MICROSDEXP_ERR_OPEN      2  // OPEN failed (invalid path)
#define MICROSDEXP_ERR_CARD      3  // SdFat/card failure
#define MICROSDEXP_ERR_QUEUE     4  // request queue full; request rejected
#define MICROSDEXP_ERR_INVALID   5  // malformed request (e.g. READ with no file)
#define MICROSDEXP_ERR_OVERFLOW  6  // RX buffers full; bytes were NACKed

// ---- Wire structs (packed, little-endian) ----

// 12-byte STATUS frame: rev, state, opDone, error, rxFree(2), txAvail(2), result(4 LE).
typedef struct {
  uint8_t  rev;      // protocol revision (2 = rev 2)
  uint8_t  state;    // MICROSDEXP_ST_*
  uint8_t  opDone;   // op sequence that completed
  uint8_t  error;    // MICROSDEXP_ERR_*
  uint16_t rxFree;   // free RX bytes (LE)
  uint16_t txAvail;  // staged TX bytes (LE)
  uint32_t result;   // latched query result (LE; sticky until next op)
} __attribute__((packed)) StatusFrame;

// 24-byte CARD_INFO frame (all LE).
typedef struct {
  uint8_t  mounted;      // 1 = mounted, 0 = not
  uint8_t  fatType;      // SdFat fatType: 0=unmounted, 12=FAT12, 16=FAT16, 32=FAT32, 64=exFAT
  uint8_t  cardType;     // card type (e.g. 1=SDSC, 2=SDHC, ...)
  uint8_t  rsvd;         // reserved (0)
  uint32_t sectorCount;
  uint32_t freeClusters;
  uint16_t errCode;
  uint16_t errData;
  uint32_t clusterCount;
  uint32_t sckHz;        // SPI SCK ceiling in Hz
} __attribute__((packed)) CardInfoFrame;

// listDir entry (v15-style stream): 'F'/'D' + NUL basename + 4 B size LE, 0xFF terminator.
typedef struct {
  uint8_t  isDir;        // 1 = 'D', 0 = 'F'
  uint32_t size;         // 0 for dirs
  char     name[64];     // NUL-terminated (LFN cap; tiny build 13)
} DirEntry;

#endif  // I2C_MICROSD_EXPANSION_PROTOCOL_H



