// ConfigSaveLoad.ino — persistent settings with checked queries and a
// staged (temp + remove + rewrite) update.
//
// Shows:
//   * exists(path, &present) — a checked query: "missing" is not confused
//     with an I2C/card error;
//   * fileSize(path, &size)  — the checked overload, used before reading;
//   * a small, validated config format with a checksum so a half-written
//     file is detected at boot and replaced with defaults;
//   * write-to-temp, then remove() the live file, then rewrite it, so the
//     live settings are never truncated in place.
//
// NOTE: the module has no atomic rename command. A power loss in the narrow
// window between remove() and the final rewrite can leave no CONFIG.INI;
// the checksum check below makes that a safe fallback to defaults, never a
// corrupt setting that the sketch would act on.
//
// Full build only (remove() and fileSize() are compiled out in minimal builds).

#include <Wire.h>
#include <stdlib.h>
#include <string.h>
#include <I2C_microSD_Expansion.h>

static const char CFG_PATH[] = "CONFIG.INI";
static const char TMP_PATH[] = "CONFIG.TMP";

struct Config {
  uint16_t threshold;
  uint16_t intervalMs;
};
static Config cfg = { 512, 250 };

I2CmicroSD microSDexp;

// ---- Tiny validated text format -------------------------------------------
// "T=512,I=250,X=9A\n" — X is the XOR of every byte before it.
static uint8_t lineChecksum(const char *s, const char *end) {
  uint8_t sum = 0;
  for (const char *p = s; p < end; ++p) sum ^= (uint8_t)*p;
  return sum;
}

static void formatConfig(char *out, size_t cap, const Config &c) {
  char prefix[32];
  snprintf(prefix, sizeof(prefix), "T=%u,I=%u,",
           (unsigned)c.threshold, (unsigned)c.intervalMs);
  uint8_t sum = lineChecksum(prefix, prefix + strlen(prefix));
  snprintf(out, cap, "%sX=%02X\n", prefix, (unsigned)sum);
}

static bool parseConfig(const char *s, Config &out) {
  const char *mark = strchr(s, 'X');
  if (!mark || mark[1] != '=') return false;
  if ((uint8_t)strtoul(mark + 2, nullptr, 16) != lineChecksum(s, mark)) return false;

  const char *p = strchr(s, 'T');
  if (!p || p[1] != '=') return false;
  out.threshold = (uint16_t)strtoul(p + 2, nullptr, 10);

  p = strchr(s, 'I');
  if (!p || p[1] != '=') return false;
  out.intervalMs = (uint16_t)strtoul(p + 2, nullptr, 10);
  return true;
}

// ---- File helpers (all synchronous; no String, no heap) --------------------
static bool writeAll(const char *path, const char *text) {
  if (!microSDexp.openWrite(path)) return false;
  size_t len = strlen(text), off = 0;
  while (off < len) {
    size_t n = microSDexp.write((const uint8_t *)text + off, len - off);
    if (n == 0) { microSDexp.close(); return false; }
    off += n;
  }
  if (!microSDexp.sync()) { microSDexp.close(); return false; }
  return microSDexp.close();
}

static bool stageAndInstall(const char *text) {
  // 1. Stage the complete new file under a temporary name.
  if (!writeAll(TMP_PATH, text)) return false;
  // 2. Retire the old file if there is one (a fresh card has none, and
  //    REMOVE_FILE on a missing path would report an error).
  bool present = false;
  if (!microSDexp.exists(CFG_PATH, &present)) return false;
  if (present && !microSDexp.remove(CFG_PATH)) return false;
  // 3. Install the fully staged content as the live config.
  if (!writeAll(CFG_PATH, text)) return false;
  // 4. Best-effort cleanup of the staging file.
  microSDexp.remove(TMP_PATH);
  return true;
}

static bool loadConfig(Config &out) {
  bool present = false;
  if (!microSDexp.exists(CFG_PATH, &present)) {
    Serial.println(F("ConfigSaveLoad: exists() FAILED (bus/card error)"));
    return false;
  }
  if (!present) return false;

  uint32_t size = 0;
  if (!microSDexp.fileSize(CFG_PATH, &size) || size == 0 || size > 63) {
    return false;  // unreadable, empty or implausible -> defaults
  }
  char buf[64];
  if (!microSDexp.openRead(CFG_PATH)) return false;
  size_t n = microSDexp.read((uint8_t *)buf, (size_t)size);
  microSDexp.close();
  if (n == 0) return false;
  buf[n] = '\0';
  return parseConfig(buf, out);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Wire.begin();
  if (!microSDexp.begin(0x6E, 400000)) {
    Serial.println(F("ConfigSaveLoad: module not found"));
    return;
  }

  char line[48];
  if (loadConfig(cfg)) {
    Serial.println(F("ConfigSaveLoad: loaded existing config"));
  } else {
    cfg = Config{ 512, 250 };
    formatConfig(line, sizeof(line), cfg);
    if (!stageAndInstall(line)) {
      Serial.println(F("ConfigSaveLoad: failed to create defaults"));
      return;
    }
    Serial.println(F("ConfigSaveLoad: created default config"));
  }
  formatConfig(line, sizeof(line), cfg);
  Serial.println(line);  // trailing newline in the string is intentional

  // A settings change: bump the threshold and persist it safely.
  cfg.threshold = 768;
  formatConfig(line, sizeof(line), cfg);
  if (stageAndInstall(line)) {
    Serial.println(F("ConfigSaveLoad: settings updated"));
  } else {
    Serial.println(F("ConfigSaveLoad: update FAILED (old config kept until remove)"));
  }
}

void loop() {}
