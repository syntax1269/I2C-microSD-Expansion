// WebSDModule.ino — ESP32 Wi-Fi WebServer + Explorer GUI for the I²C microSD Expansion
// I²C microSD Expansion module (firmware v18.7.6+, protocol rev 2).
//
// WHAT THIS SKETCH IS
// --------------------
// A functional demo *and* a tiny graphical explorer. The ESP32 is the
// I2C master on GPIO21(SDA)/GPIO22(SCL) to the module at 0x6E. When the
// sketch boots it (1) joins Wi-Fi, (2) asks the module for its version +
// card info so the Serial console proves the I2C path is alive, (3) starts a
// WebServer on port 80. Open http://<DHCP-IP>/ in a browser (or curl the
// /api/* endpoints) to explore every module function through forms instead of
// writing a new sketch. Each GUI panel shows the exact C++ call that the
// button triggers, so copying it into a sketch is one paste.
//
// ZERO-HEAP / LIBRARY RULES — also followed in this sketch
// --------------------------------------------------------------
//   * All paths/buffers are fixed `char[]` (260 B path, 128 B network chunk,
//     4 KB HTML page); `snprintf` is the only formatter used.
//   * No `malloc/free/realloc/calloc/new/delete`, no STL, no exceptions,
//     no RTTI, no iostream. `extern "C"` is used only by the library API.
//   * All multi-byte little-endian fields are packed by shifts (see
//     I2C_microSD_Expansion.cpp), never by unsafe struct copies.
//   * The WebServer framework does hold heap text temporaries internally
//     (`arg()/uri()` return heap strings); this sketch copies them into our
//     fixed buffers inside the copy loop and drops the temporary, so no
//     `String`/vector is ever stored in our code.
//   * I2C never blocks the Wi-Fi task indefinitely: every module call is one
//     flow-paced transaction with a clock-scaled timeout.
//
// HOW TO READ THIS FILE (for the next person)
// -------------------------------------------
//   main flow        → setup()/loop() at the bottom.
//   path helpers     → joinPath/dirOf/baseOf/mimeOf (pure char[], no heap).
//   transport glue   → copyArg/copyUri (only String touchpoints, immediate copy).
//   HTTP handlers    → handleRoot/handleList/handle* (each is tiny and
//                      self-contained; they do one module op, not two).
//   API handlers     → handleApi* (JSON for curl: status/version/info/size/list).
//   upload is special→ the two-callback multipart streaming pair (see
//                      handleUploadPiece/handleUploadPost comments).
//
// CIRCUIT (breadboard or PCB, no external shifter — the module PCB level-
// shifts both sides: 3.3 V rail to the SD side, host 3.3/5 V logic accepted)
//   ESP32 GPIO21 (SDA) ── PA2 (SDA)   module 0x6E
//   ESP32 GPIO22 (SCL) ── PA3 (SCL)
//   GND common, module powered from 5V OR 3.3V (one only, never both).
//   Pull-ups: 3.3k fitted on module (Fm+ sized); cut the underside trace
//   if the host also supplies its own.
//   g_microSDexp.begin(MICROSDEXP_ADDR, 1000000) below picks Fm+; use 400000
//   for Fm or 100000 for Sm. ATtiny85 Tinyi2cStorage is ≤100 kHz only.
//
// CURL SMOKE TESTS (replace <ip> with the DHCP serial line)
//   curl -s http://<ip>/api/version
//   curl -s http://<ip>/api/info
//   curl -s "http://<ip>/api/list?DIR=/"
//   curl -F file=@test.txt http://<ip>/upload
//   curl -o out.bin "http://<ip>/download?file=/test.txt"
//   curl -s -X POST -d "file=/test.txt" http://<ip>/deleteFile

#include <WiFi.h>
#include <WebServer.h>
#include <I2C_microSD_Expansion.h>

// ---------------------------------------------------------------------------
// Configuration. Change SSID/PASS to your access point. The demo keeps them
// here as strings; a real product would move them to
// NVS/EEPROM — out of scope for the demo).
// ---------------------------------------------------------------------------
#define MICROSDEXP_ADDR 0x6E
#define SSID_TEXT "YOUR_WIFI_SSID"
#define PASS_TEXT "YOUR_WIFI_PASSWORD"

// Buffers are compile-time sized so the linker proves no heap growth.
// MAX_PATH matches protocol.h (Filename[260] on the microSDexp).
#define MAX_PATH 260
#define MAX_ENTRIES 16          // one listing page fits in g_entries
#define IO_BUFSZ 256            // one HTTP chunk == one 256 B module TX drain
#define HTML_BUFSZ 16384        // largest single HTML page we ever build

// Global driver + HTTP server. `static` keeps construction order sane on ESP32.
static I2CmicroSD g_microSDexp;
static WebServer g_server(80);

// Upload streaming state: the ESP32 WebServer calls handleUploadPiece() once
// per multipart chunk, so the open MicroSDExpFile * must live across callbacks.
static MicroSDExpFile *g_upload = nullptr;
static bool g_upload_ok = false;
static uint32_t g_upload_bytes = 0;

// One listing page. DirEntry.name is 64 B (LFN truncated) — see protocol.h.
static DirEntry g_entries[MAX_ENTRIES];

// Copies a WebServer `arg` into a fixed buffer. The framework returns heap
// text; we copy it out char-by-char here so the temporary can die immediately
// (the sketch never stores heap text).
static void copyArg(const char *key, char *dst, size_t cap) {
  auto s = g_server.arg(key);
  const char *p = s.c_str();
  size_t n = 0;
  while (n + 1 < cap && p[n] != '\0') {
    dst[n] = p[n];
    n++;
  }
  dst[n] = '\0';
}

// Copies the request URI likewise. `const char*` is only valid until the next
// framework call, so we never keep a pointer across handler code.
static void copyUri(char *dst, size_t cap) {
  auto s = g_server.uri();
  const char *p = s.c_str();
  size_t n = 0;
  while (n + 1 < cap && p[n] != '\0') {
    dst[n] = p[n];
    n++;
  }
  dst[n] = '\0';
}


// --- Output escaping -------------------------------------------------------
// Filenames and DIR arguments come from the card or the request. Never place
// them in HTML, attributes, JS literals or JSON without encoding first.
static void htmlEscape(const char *src, char *dst, size_t cap) {
  size_t o = 0;
  if (cap == 0) return;
  for (const char *p = src; *p && o + 6 < cap; ++p) {
    switch (*p) {
      case '&': memcpy(dst + o, "&amp;", 5); o += 5; break;
      case '<': memcpy(dst + o, "&lt;", 4); o += 4; break;
      case '>': memcpy(dst + o, "&gt;", 4); o += 4; break;
      case '"': memcpy(dst + o, "&quot;", 6); o += 6; break;
      case '\'': memcpy(dst + o, "&#39;", 5); o += 5; break;
      default: dst[o++] = *p;
    }
  }
  dst[o] = '\0';
}

// Percent-encode for href/query components and JavaScript string literals.
static void urlEncode(const char *src, char *dst, size_t cap) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  if (cap == 0) return;
  for (const char *p = src; *p && o + 3 < cap; ++p) {
    unsigned char c = (unsigned char)*p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~' || c == '/') {
      dst[o++] = (char)c;
    } else {
      dst[o++] = '%';
      dst[o++] = hex[c >> 4];
      dst[o++] = hex[c & 0x0F];
    }
  }
  dst[o] = '\0';
}

static void jsonEscape(const char *src, char *dst, size_t cap) {
  size_t o = 0;
  if (cap == 0) return;
  for (const char *p = src; *p && o + 6 < cap; ++p) {
    unsigned char c = (unsigned char)*p;
    switch (c) {
      case '"': memcpy(dst + o, "\\\"", 2); o += 2; break;
      case '\\': memcpy(dst + o, "\\\\", 2); o += 2; break;
      case '\n': memcpy(dst + o, "\\n", 2); o += 2; break;
      case '\r': memcpy(dst + o, "\\r", 2); o += 2; break;
      case '\t': memcpy(dst + o, "\\t", 2); o += 2; break;
      case '<': memcpy(dst + o, "\\u003c", 6); o += 6; break;
      case '>': memcpy(dst + o, "\\u003e", 6); o += 6; break;
      case '&': memcpy(dst + o, "\\u0026", 6); o += 6; break;
      default: dst[o++] = (char)c;
    }
  }
  dst[o] = '\0';
}

// Distinct slots so several values can be escaped in one snprintf call.
static char g_html[3][MAX_PATH];
static char g_url[3][MAX_PATH];
static char g_json[3][MAX_PATH];
#define H1(s) htmlEscape((s), g_html[0], sizeof(g_html[0]))
#define H2(s) htmlEscape((s), g_html[1], sizeof(g_html[1]))
#define H3(s) htmlEscape((s), g_html[2], sizeof(g_html[2]))
#define U1(s) urlEncode((s), g_url[0], sizeof(g_url[0]))
#define U2(s) urlEncode((s), g_url[1], sizeof(g_url[1]))
#define U3(s) urlEncode((s), g_url[2], sizeof(g_url[2]))
#define J1(s) jsonEscape((s), g_json[0], sizeof(g_json[0]))
#define J2(s) jsonEscape((s), g_json[1], sizeof(g_json[1]))

static size_t boundedLen(const char *p) {
  size_t n = 0;
  while (p != nullptr && p[n] != '\0' && n < MAX_PATH) n++;
  return n;
}

// Final join of dir + "/" + name, with SdFat semantics: "/" is the root
// (not "//"), trailing "/" is tolerated. No statics, no heap.
static void joinPath(const char *dir, const char *name, char *dst, size_t cap) {
  size_t n = 0;
  bool root = (dir[0] == '/' && dir[1] == '\0');
  if (!root) {
    while (dir[n] != '\0' && n + 1 < cap) {
      dst[n] = dir[n];
      n++;
    }
    if (n + 1 < cap) dst[n++] = '/';
  } else {
    dst[n++] = '/';
  }
  size_t i = 0;
  while (name[i] != '\0' && n + 1 < cap) {
    dst[n++] = name[i++];
  }
  dst[n] = '\0';
}

// Parent directory of `p` ("/a/b" -> "/a", "/a" -> "/").
static void dirOf(const char *p, char *dst, size_t cap) {
  int s = -1;
  for (int i = 0; p[i] != '\0' && i < MAX_PATH; i++) {
    if (p[i] == '/') s = i;
  }
  if (s <= 0) {
    dst[0] = '/';
    dst[1] = '\0';
    return;
  }
  for (int i = 0; i < s && (size_t)i + 1 < cap; i++) dst[i] = p[i];
  dst[s] = '\0';
}

// Basename only ("a/b/c.bin" -> "c.bin").
static void baseOf(const char *p, char *dst, size_t cap) {
  int s = -1;
  for (int i = 0; p[i] != '\0' && i < MAX_PATH; i++) {
    if (p[i] == '/') s = i;
  }
  size_t n = 0;
  for (int i = s + 1; p[i] != '\0' && n + 1 < cap; i++) dst[n++] = p[i];
  dst[n] = '\0';
}

// MIME by suffix. Case-insensitive tail compare via a bounded manual check,
// so no strcasecmp (locale) or heap is needed.
static const char *mimeOf(const char *p) {
  size_t n = boundedLen(p);
  if (n >= 4 && (p[n - 4] == '.' && (p[n - 3] == 'h' || p[n - 3] == 'H') &&
                 (p[n - 2] == 't' || p[n - 2] == 'T') &&
                 (p[n - 1] == 'm' || p[n - 1] == 'M'))) return "text/html";
  if (n >= 4 && p[n - 4] == '.' && (p[n - 3] == 'c' || p[n - 3] == 'C') &&
              (p[n - 2] == 's' || p[n - 2] == 'S') &&
              (p[n - 1] == 's' || p[n - 1] == 'S')) return "text/css";
  if (n >= 3 && p[n - 3] == '.' && (p[n - 2] == 'j' || p[n - 2] == 'J') &&
              (p[n - 1] == 's' || p[n - 1] == 'S')) return "application/javascript";
  if (n >= 4 && p[n - 4] == '.' && (p[n - 3] == 'p' || p[n - 3] == 'P') &&
              (p[n - 2] == 'n' || p[n - 2] == 'N') &&
              (p[n - 1] == 'g' || p[n - 1] == 'G')) return "image/png";
  if (n >= 4 && p[n - 4] == '.' && (p[n - 3] == 'j' || p[n - 3] == 'J') &&
              (p[n - 2] == 'p' || p[n - 2] == 'P') &&
              (p[n - 1] == 'g' || p[n - 1] == 'G')) return "image/jpeg";
  if (n >= 3 && p[n - 3] == '.' && (p[n - 2] == 't' || p[n - 2] == 'T') &&
              (p[n - 1] == 'x' || p[n - 1] == 'X')) return "text/plain";
  return "application/octet-stream";
}

// Streams one file to the client: openFile("r") + set content length, then
// 256 B fileRead + sendContent chunks until size hits zero. The final empty
// sendContent() tells the HTTP layer the body is done (chunked framing).
// Abort-safe: the loop polls
// g_server.client().connected() each 256 B so a curl/browser abort (half-
// downloaded 5.6 MB file) frees the module instead of wedging handleClient().
static void serveFile(const char *path, bool download) {
  if (!g_microSDexp.exists(path)) {
    g_server.send(404, "text/html", "Not found");
    return;
  }
  const char *mime = download ? "application/octet-stream" : mimeOf(path);
  uint32_t sz = g_microSDexp.fileSize(path);
  MicroSDExpFile *f = openFile(&g_microSDexp, path, "r");
  if (!f) {
    g_server.send(500, "text/plain", "open failed");
    return;
  }
  g_server.setContentLength(sz);
  g_server.send(200, mime, "");
  {
    WiFiClient client = g_server.client();
    if (client) client.setTimeout(200);  // sendContent must not wedge on a dead socket
  }
  static uint8_t buf[IO_BUFSZ];
  uint32_t left = sz;
  bool err = false;
  while (left > 0) {
    // Abort guard: if the TCP client vanished (curl -m, browser close),
    // stop streaming immediately; the module is freed and the server stays
    // responsive when a transfer is aborted.
    WiFiClient client = g_server.client();
    if (client && !client.connected()) {
      Serial.println("download: client disconnected, abort stream + free module");
      err = true;
      break;
    }
    size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
    size_t n = fileRead(f, buf, want);
    if (n == 0) {
      err = true;
      break;
    }
    g_server.sendContent((const char *)buf, n);
    left -= (uint32_t)n;
    yield();  // let the TCP stack flush; without this big files stall
  }
  fileClose(f);
  if (!err) g_server.sendContent("");
  if (err) Serial.println("download stream error");
}

// --- Code-example snippets shown in the GUI ("what to paste into a sketch") ---
// Each <pre> mirrors the handler below it 1:1, so exploring the GUI *is*
// learning the API. They are escaped for HTML (&lt;) and copy with one click.
static const char kExampleList[] =
    "<pre>// List a directory (DirEntry: name[64], isDir, size)\n"
    "DirEntry e[16];\n"
    "uint8_t n = microSDexp.listDir(\"/\", e, 16);\n"
    "for (uint8_t i = 0; i &lt; n; i++) {\n"
    "  Serial.println(e[i].name);  // + e[i].isDir / e[i].size\n"
    "}</pre>";
static const char kExampleWrite[] =
    "<pre>// Write + append (append is always legacy 0x04, never 0x21)\n"
    "MicroSDExpFile * w = openFile(&amp;microSDexp, \"/LOG.TXT\", \"w\");\n"
    "fileWrite(w, (const uint8_t*)\"hello \", 6);\n"
    "fileClose(w);\n"
    "MicroSDExpFile * a = openFile(&amp;microSDexp, \"/LOG.TXT\", \"a\");\n"
    "fileWrite(a, (const uint8_t*)\"again\\n\", 6);\n"
    "fileClose(a);</pre>";
static const char kExampleQuery[] =
    "<pre>// Queries use the STATUS.result latch (v18.7.3)\n"
    "uint32_t sz = microSDexp.fileSize(\"/LOG.TXT\");\n"
    "bool ex = microSDexp.exists(\"/LOG.TXT\");\n"
    "bool d  = microSDexp.isDirectory(\"/DATA\");</pre>";
static const char kExampleDir[] =
    "<pre>// Directories (SdFat semantics: / absolute, case-insensitive)\n"
    "microSDexp.mkdir(\"/DATA\");\n"
    "microSDexp.rmdir(\"/DATA\");  // must be empty</pre>";

// Root page = the GUI shell. Keep it under HTML_BUFSZ: every form target is
// one of the handlers below, so clicking a button is itself the example.
// Tabs are pure CSS/JS (no server round-trip) so the listing stays live.
static const char kCss[] =
    "<style>"
    ":root{--bg:#f4f6fb;--card:#fff;--ink:#0f172a;--muted:#64748b;--line:#e2e8f0;"
    "--acc:#6366f1;--acc2:#a855f7;--ok:#10b981;--warn:#f59e0b;--bad:#ef4444;"
    "--radius:14px;--shadow:0 10px 25px rgba(15,23,42,.08)}"
    "body{font-family:Inter,ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
    "background:radial-gradient(1000px 500px at 20% -10%,#e0e7ff,transparent),var(--bg);"
    "color:var(--ink);margin:0;line-height:1.5}"
    "header.site{background:linear-gradient(135deg,#4f46e5,#9333ea);color:#fff;padding:22px 18px}"
    ".wrap{max-width:1100px;margin:auto}.pill{display:inline-flex;gap:6px;align-items:center;"
    "background:rgba(255,255,255,.18);padding:5px 10px;border-radius:999px;font-size:13px}"
    ".pill i{width:8px;height:8px;border-radius:50%;background:#4ade80;display:inline-block}"
    "nav.top{display:flex;gap:8px;padding:10px 18px;background:#111827;color:#fff;position:sticky;top:0;z-index:10}"
    "nav.top a{color:#cbd5e1;text-decoration:none;padding:7px 12px;border-radius:999px;font-size:14px}"
    "nav.top a:hover{background:rgba(255,255,255,.12);color:#fff}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin:14px 0}"
    ".stat{background:var(--card);border-radius:var(--radius);padding:12px 14px;box-shadow:var(--shadow);border:1px solid var(--line)}"
    ".stat b{display:block;font-size:22px}.stat span{color:var(--muted);font-size:13px}"
    ".card,.panel{background:var(--card);border:1px solid var(--line);border-radius:var(--radius);padding:16px;margin:12px 0;box-shadow:var(--shadow)}"
    ".card h3{margin:0 0 8px}"
    ".tabs button{background:#c7d2fe;color:#0f172a;border:0;padding:8px 14px;border-radius:999px;margin:2px;cursor:pointer;font-weight:600}"
    ".tabs button:hover{background:#a5b4fc}"
    ".tabs button.active{background:var(--acc);color:#fff}"
    ".tabs button.active:hover{background:var(--acc)}"
    ".stat .sub{display:block;color:var(--muted);font-size:13px}"
    ".stat .lbl{display:block;color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.06em}"
    ".stat .bar{display:block;height:8px;background:var(--line);border-radius:999px;margin:6px 0;overflow:hidden}"
    ".stat .bar i{display:block;height:100%;width:0%;background:linear-gradient(90deg,var(--acc),var(--acc2));border-radius:999px}"
    "table{border-collapse:collapse;width:100%}th,td{padding:9px 10px;text-align:left;border-bottom:1px solid var(--line)}"
    "thead th{color:var(--muted);font-weight:700;font-size:12px;text-transform:uppercase;letter-spacing:.06em}"
    "tbody tr:hover{background:#f8fafc}a{color:var(--acc)}button{border:0;border-radius:999px;padding:7px 13px;"
    "background:var(--acc);color:#fff;cursor:pointer;font-weight:600}button:hover{opacity:.92}"
    ".danger{background:var(--bad)}.ghost{background:var(--line);color:var(--ink)}"
    "input,select{border:1px solid var(--line);border-radius:10px;padding:8px 10px;font-size:14px}"
    ".badge{font-size:12px;background:#eef2ff;color:#3730a3;border-radius:999px;padding:2px 8px}"
    "pre{background:#0f172a;color:#e2e8f0;padding:12px;border-radius:12px;overflow:auto;font-size:12px}"
    ".crumbs{display:flex;gap:4px;flex-wrap:wrap;align-items:center;margin:8px 0;font-size:13px}"
    ".crumbs span{color:var(--muted)}"
    ".drop{border:2px dashed #a5b4fc;border-radius:12px;padding:18px;text-align:center;background:#eef2ff}"
    ".drop.drag{background:#e0e7ff}"
    ".toast{position:fixed;right:16px;bottom:16px;background:#111827;color:#fff;padding:10px 14px;border-radius:10px;transform:translateY(20px);opacity:0;transition:.25s}"
    ".toast.show{transform:none;opacity:1}"
    "</style>";

static void handleRoot() {
  static char buf[HTML_BUFSZ];
  char dir[32] = "/";
  uint8_t count = g_microSDexp.listDir(dir, g_entries, MAX_ENTRIES);
  char table[4096];
  int tn = snprintf(table, sizeof(table),
                    "<table id=fileTable><thead><tr><th>Type</th><th>Name</th><th>Size</th></tr></thead><tbody>");
  if (tn < 0) tn = 0;
  if ((size_t)tn >= sizeof(table)) tn = sizeof(table) - 1;
  for (uint8_t i = 0; i < count; i++) {
    char entry[MAX_PATH];
    joinPath(dir, g_entries[i].name, entry, sizeof(entry));
    int k = g_entries[i].isDir ? snprintf(table + tn, sizeof(table) - (size_t)tn,
                                          "<tr><td>[D]</td><td><a href='/listSDCard?DIR=%s'>%s/</a></td><td>---</td></tr>",
                                          U1(entry), H2(g_entries[i].name))
                               : snprintf(table + tn, sizeof(table) - (size_t)tn,
                                          "<tr><td>[F]</td><td><a href='/download?file=%s'>%s</a></td><td>%lu</td></tr>",
                                          U1(entry), H2(g_entries[i].name),
                                          (unsigned long)g_entries[i].size);
    if (k <= 0) break;
    tn += k;
    if ((size_t)tn >= sizeof(table)) break;
  }
  snprintf(table + tn, sizeof(table) - (size_t)tn, "</tbody></table>");
  int n = snprintf(buf, sizeof(buf),
                   "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name=viewport content='width=device-width,initial-scale=1'>"
                   "<title>module Explorer — I²C microSD Expansion</title>%s</head><body>"
                   // -- Gradient header: title + live status pill --
                   "<header class=site><div class=wrap>"
                   "<h1 style='margin:0;font-size:26px'>🌉 I²C microSD Expansion — Explorer</h1>"
                   "<p style='margin:6px 0 0;opacity:.92'>module at <b>0x6E</b> · ESP32 (GPIO21/22 · 400 kHz) · every button is a live example</p>"
                   "<p>status: <span id=status class=pill>…</span></p>"
                   "</div></header>"
                   "<nav class=top><div class=wrap>"
                   "<a href='/'>Home</a><a href='/listSDCard?DIR=/'>Files</a><a href='/api/info'>/api/info</a>"
                   "<a href='/api/version'>/api/version</a><a href='/api/status'>/api/status</a></div></nav>"
                   "<main class=wrap>"
                   // -- Stat cards (populated by the table below via JS tick) --
                   "<section class=grid id=stats>"
                   "<div class=stat><b id=st-version>—</b><span>module version (0x0F)</span></div>"
                   "<div class=stat><b id=st-fat>—</b><span class=sub id=st-card>—</span><span class=lbl>FAT / card</span></div>"
                   "<div class=stat><b id=st-free>—</b><span class=sub id=st-total>—</span><span class=bar><i id=st-bar></i></span><span class=lbl>Free space</span></div>"
                   "<div class=stat><b id=st-state>—</b><span>STATUS state</span></div>"
                   "<div class=stat><b id=st-temp>—</b><span>Die temperature (°F)</span></div>"
                   "</section>"
                   "<div class=tabs><button class='tabB active' onclick=\"showPanel('files',this)\">📁 Files</button>"
                   "<button class='tabB' onclick=\"showPanel('actions',this)\">⚙️ Actions</button>"
                   "<button class='tabB' onclick=\"showPanel('code',this)\">💻 Code examples</button></div>"
                   // -- Panel: Files --
                   "<div id=panel-files class=panel><h2>📁 Browse files <span class=badge>/</span></h2>"
                   "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0'>"
                   "<input id=qRoot placeholder='Search files…' oninput=\"filterTable(this.value)\">"
                   "<button class=ghost onclick=\"location.href='/listSDCard?DIR=/'\">↗ Full explorer</button></div>"
                   "<div class=crumbs><span>/</span></div>"
                   "%s"
                   "<form method='POST' action='/upload' enctype='multipart/form-data' id=uploadForm>"
                   "<div class=drop id=dropUpload><input type='file' name='file' id=fileInput> <button type='submit'>Upload to /</button>"
                   "<div style='margin-top:6px;color:var(--muted);font-size:13px'>Drag &amp; drop a file onto this box to upload instantly.</div></div></form>"
                   "<form method='POST' action='/action/mkdir'>"
                   "<input name='dir' placeholder='/NEWDIR'><button>mkdir</button></form></div>"
                   // -- Panel: Actions --
                   "<div id=panel-actions class=panel style='display:none'><h2>⚙️ module actions</h2>"
                   "<div class=grid>"
                   "<div class=card style='box-shadow:none'><h3>Append</h3><p class=muted>legacy append 0x04 (never truncates)</p>"
                   "<form method='POST' action='/action/append'><input name='file' placeholder='/LOG.TXT' required>"
                   "<input name='text' placeholder='text'><button>append</button></form>"
                   "<pre>// openFile(&amp;microSDexp, \"/LOG.TXT\", \"a\");\n// fileWrite(a, text, n); fileClose(a);</pre></div>"
                   "<div class=card style='box-shadow:none'><h3>Query</h3><p class=muted>exists / isDir / size</p>"
                   "<form method='GET' action='/api/exists'><input name=file placeholder='/LOG.TXT'><button>exists?</button></form>"
                   "<form method='GET' action='/api/size'><input name=file placeholder='/LOG.TXT'><button>size?</button></form>"
                   "<pre>// size: microSDexp.fileSize(\"/LOG.TXT\");</pre></div>"
                   "<div class=card style='box-shadow:none'><h3>Directories</h3><p class=muted>mkdir / rmdir (empty only)</p>"
                   "<form method='POST' action='/action/mkdir'><input name='dir' placeholder='/NEWDIR'><button>mkdir</button></form>"
                   "<form method='POST' action='/action/rmdir'><input name='dir' placeholder='/DATA'><button>rmdir</button></form>"
                   "<pre>// microSDexp.mkdir(\"/DATA\");  microSDexp.rmdir(\"/DATA\");</pre></div>"
                   "<div class=card style='box-shadow:none'><h3>Version</h3><p class=muted>0x0F read</p>"
                   "<p><a href='/api/version'>open /api/version</a></p>"
                   "<pre>// char v[24]; microSDexp.version(v, 24);</pre></div>"
                   "</div>"
                   "<p class=muted>Try-it (curl):<br><code>curl 'http://&lt;ip&gt;/api/status'</code> · "
                   "<code>curl 'http://&lt;ip&gt;/api/info'</code></p>"
                   // -- Panel: Code examples --
                   "<div id=panel-code class=panel style='display:none'><h2>💻 What to paste into a sketch</h2>"
                   "<h3>listDir</h3>%s<h3>write/append</h3>%s<h3>queries</h3>%s<h3>directories</h3>%s"
                   "<h3>curl</h3><pre>curl -F file=@test.txt http://&lt;ip&gt;/upload\ncurl -o test.txt 'http://&lt;ip&gt;/download?file=/test.txt'\n"
                   "curl 'http://&lt;ip&gt;/listSDCard?DIR=/'</pre></div>"
                   "</main><footer style='padding:16px;text-align:center;color:#64748b'>I²C microSD Expansion v18.7.6 · protocol rev 2 · zero-heap host library</footer>"
                   "<div id=toast class=toast></div><script>"
                   "function showPanel(id,btn){for(const p of ['files','actions','code']){"
                   "document.getElementById('panel-'+p).style.display=(p===id?'block':'none');}"
                   "for(const b of document.querySelectorAll('.tabs button'))b.classList.remove('active');"
                   "if(btn)btn.classList.add('active');}"
                   "async function tick(){try{const r=await fetch('/api/status');const j=await r.json();"
                   "const s=document.getElementById('status');s.textContent=j.state+' opDone='+j.opDone;"
                   "document.getElementById('st-state').textContent=j.state;}catch(e){}}"
                   "async function stats(){try{const v=await (await fetch('/api/version')).json();"
                   "document.getElementById('st-version').textContent=v.version;}catch(e){}}"
                   "async function info(){try{const v=await (await fetch('/api/info')).json();"
                   "const fat=(v.fatType==12||v.fatType==1)?'FAT12':(v.fatType==16||v.fatType==2)?'FAT16':(v.fatType==32||v.fatType==3)?'FAT32':'FAT'+v.fatType;"
                   "const card=(v.cardType==1)?'SD1':(v.cardType==2)?'SD2':(v.cardType==3)?'SDHC':'card '+v.cardType;"
                   "document.getElementById('st-fat').textContent=fat;"
                   "document.getElementById('st-card').textContent=card;"
                   "const fmt=b=>{const gb=b/1073741824, mb=b/1048576;return gb>=1?gb.toFixed(2)+' GB':mb.toFixed(1)+' MB';};"
                   "document.getElementById('st-free').textContent=fmt(v.freeBytes)+' free';"
                   "document.getElementById('st-total').textContent=fmt(v.totalBytes||v.freeBytes)+' total';"
                   "const u=v.totalBytes?Math.max(0,Math.min(100,(1-v.freeBytes/v.totalBytes)*100)):0;"
                   "document.getElementById('st-bar').style.width=u.toFixed(1)+'%%';}catch(e){}}"
                   "async function temp(){try{const v=await (await fetch('/api/temp')).json();"
                   "const f=(v.kelvin-273.15)*9/5+32;"
                   "document.getElementById('st-temp').textContent=f.toFixed(1)+' °F';}catch(e){}}"
                   "async function poll(){await tick();await stats();await info();await temp();}"
                   "setInterval(poll,2000);poll();"
                   // -- drag & drop upload --
                   "const drop=document.getElementById('dropUpload');"
                   "drop.addEventListener('dragover',e=>{e.preventDefault();drop.classList.add('drag')});"
                   "drop.addEventListener('dragleave',()=>drop.classList.remove('drag'));"
                   "drop.addEventListener('drop',async e=>{e.preventDefault();drop.classList.remove('drag');"
                   "const f=e.dataTransfer.files[0];if(!f)return;"
                   "const fd=new FormData();fd.append('file',f);fd.append('dest','/');"
                   "const r=await fetch('/upload',{method:'POST',body:fd});"
                   "const t=await r.text();alert(t);});"
                   "// search filter for the file table (client-side only)"
                   "function filterTable(q){for(const row of document.querySelectorAll('table tbody tr')){"
                   "row.style.display=row.textContent.toLowerCase().includes(q)?'':'none';}}"
                   "const t=document.createElement('div');t.innerHTML='';"
                   "</script></body></html>",
                   kCss, table, kExampleList, kExampleWrite, kExampleQuery, kExampleDir);
  if (n < 0) n = 0;
  if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
  g_server.send(200, "text/html", buf);
}

// GET /listSDCard?DIR=/&page=&perPage= — directory table with a per-dir
// upload form. Pagination keeps the HTML under HTML_BUFSZ on huge roots.
static void handleList() {
  static char dir[MAX_PATH];
  if (g_server.hasArg("DIR")) copyArg("DIR", dir, sizeof(dir));
  else {
    dir[0] = '/';
    dir[1] = '\0';
  }
  int page = 1;
  if (g_server.hasArg("page")) page = g_server.arg("page").toInt();
  if (page < 1) page = 1;
  int perPage = 20;
  if (g_server.hasArg("perPage")) perPage = g_server.arg("perPage").toInt();
  if (perPage < 1) perPage = 20;
  static char html[HTML_BUFSZ];
  int n = snprintf(html, sizeof(html),
                   "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name=viewport content='width=device-width,initial-scale=1'>"
                   "<title>Files in %s — module Explorer</title>%s</head><body>"
                   "<header class=site><div class=wrap><h1 style='margin:0'>📁 %s</h1><p style='margin:6px 0 0;opacity:.9'>module 0x6E explorer · <a href='/' style='color:#fff;text-decoration:underline'>Home</a></p></div></header>"
                   "<nav class=top><div class=wrap><a href='/'>Home</a><a href='/listSDCard?DIR=/'>Files</a>"
                   "<a href='/api/status'>Status</a><a href='/api/info'>Card info</a></div></nav>"
                   "<main class=wrap><div class=card><div style='display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap'>"
                   "<h2 style='margin:0'>%s</h2>"
                   "<input id=q placeholder='Search in this folder…' style='max-width:260px'>"
                   "</div><div class=crumbs id=crumbs></div>",
                   H1(dir), kCss, H2(dir), H3(dir));
  if (n < 0) n = 0;
  if ((size_t)n >= sizeof(html)) n = sizeof(html) - 1;
  char parent[MAX_PATH];
  dirOf(dir, parent, sizeof(parent));
  if (!(dir[0] == '/' && dir[1] == '\0')) {
    snprintf(html + n, sizeof(html) - (size_t)n,
             "<p><a href='/listSDCard?DIR=%s'>&#8592; Up</a></p>", U1(parent));
  }
  n += (int)strlen(html + n);
  uint8_t total = g_microSDexp.listDir(dir, g_entries, MAX_ENTRIES);
  char row[1024];
  snprintf(html + n, sizeof(html) - (size_t)n,
           "<table id=fileTable><thead><tr><th>Type</th><th>Delete</th><th>Name</th><th>Size</th></tr></thead><tbody>");
  n += (int)strlen(html + n);
  // Slice the single DirEntry page for the request.
  int startIdx = (page - 1) * perPage;
  int endIdx = page * perPage;
  if (total == 0) {
    snprintf(html + n, sizeof(html) - (size_t)n,
             "<tr><td colspan='4'>(Directory is empty)</td></tr>");
  } else {
    for (int i = startIdx; i < endIdx && i < total; i++) {
      char entry[MAX_PATH];
      joinPath(dir, g_entries[i].name, entry, sizeof(entry));
      if (g_entries[i].isDir) {
        snprintf(row, sizeof(row),
                 "<tr><td>[D]</td><td>---</td><td><a href='./listSDCard?DIR=%s'>%s/</a></td><td>---</td></tr>",
                 U1(entry), H2(g_entries[i].name));
      } else {
        snprintf(row, sizeof(row),
                 "<tr><td>[F]</td><td><form method='POST' action='/deleteFile' style='display:inline'>"
                 "<input type='hidden' name='file' value='%s'><button>Del</button></form></td>"
                 "<td><a href='/download?file=%s'>%s</a></td><td>%lu</td></tr>",
                 U1(entry), H2(entry), H3(g_entries[i].name), (unsigned long)g_entries[i].size);
      }
      snprintf(html + n, sizeof(html) - (size_t)n, "%s", row);
      n += (int)strlen(html + n);
      if ((size_t)n >= sizeof(html)) break;
    }
  }
  int totalPages = (total + perPage - 1) / perPage;
  if (totalPages < 1) totalPages = 1;
  snprintf(html + n, sizeof(html) - (size_t)n,
           "</tbody></table><div class=crumbs>Page %d of %d</div>"
           "<form method='POST' action='/upload' enctype='multipart/form-data' id=uploadForm>"
           "<div class=drop id=dropUpload style='margin-top:12px'>Drop a file to upload into <b>%s</b><br>"
           "<input type='hidden' name='dest' value='%s'><input type='file' name='file' id=fileInput> <button type='submit'>Upload here</button></div></form>"
           "<p style='color:var(--muted);font-size:13px'>Ctrl-click drag & drop uses the same /upload handler (fileName → dest + base)</p>"
           "</main><div id=toast class=toast></div><script>"
           "const q=document.getElementById('q');q.addEventListener('input',()=>{const v=q.value.toLowerCase();"
           "for(const row of document.querySelectorAll('#fileTable tbody tr')){row.style.display=row.textContent.toLowerCase().includes(v)?'':'none';}});"
           "const drop=document.getElementById('dropUpload');"
           "drop.addEventListener('dragover',e=>{e.preventDefault();drop.classList.add('drag')});"
           "drop.addEventListener('dragleave',()=>drop.classList.remove('drag'));"
           "drop.addEventListener('drop',async e=>{e.preventDefault();drop.classList.remove('drag');"
           "const f=e.dataTransfer.files[0];if(!f)return;const fd=new FormData();fd.append('file',f);fd.append('dest','%s');"
           "const r=await fetch('/upload',{method:'POST',body:fd});const t=await r.text();alert(t);});"
           "</script></body></html>",
           page, totalPages, H1(dir), H2(dir), U1(dir));
  g_server.send(200, "text/html", html);
}

// POST /deleteFile file=/… — one op, then redirect to the parent listing.
static void handleDelete() {
  if (g_server.method() != HTTP_POST) {
    g_server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  static char path[MAX_PATH];
  copyArg("file", path, sizeof(path));
  if (path[0] == '\0') {
    g_server.send(400, "text/plain", "Missing file argument");
    return;
  }
  bool ok = g_microSDexp.remove(path);
  if (ok) {
    static char parent[MAX_PATH];
    dirOf(path, parent, sizeof(parent));
    static char loc[300];
    snprintf(loc, sizeof(loc), "/listSDCard?DIR=%s", U1(parent));
    g_server.sendHeader("Location", loc, true);
    g_server.send(303, "text/plain", "");
  } else {
    g_server.send(500, "text/plain", "Failed to delete file");
  }
}

// --- Actions: mkdir / rmdir / append (all forms POST here) ---

// POST /action/mkdir dir=/NEWDIR — microSDexp.mkdir(), then list the new dir.
static void handleMkdir() {
  static char dir[MAX_PATH];
  copyArg("dir", dir, sizeof(dir));
  if (dir[0] == '\0') {
    g_server.send(400, "text/plain", "Missing dir argument");
    return;
  }
  if (g_microSDexp.mkdir(dir)) {
    static char loc[300];
    snprintf(loc, sizeof(loc), "/listSDCard?DIR=%s", U1(dir));
    g_server.sendHeader("Location", loc, true);
    g_server.send(303, "text/plain", "");
  } else {
    g_server.send(500, "text/plain", "Failed to mkdir");
  }
}

// POST /action/rmdir dir=/NEWDIR — empty dirs only (SdFat rule).
static void handleRmdir() {
  static char dir[MAX_PATH];
  copyArg("dir", dir, sizeof(dir));
  if (dir[0] == '\0') {
    g_server.send(400, "text/plain", "Missing dir argument");
    return;
  }
  if (g_microSDexp.rmdir(dir)) {
    static char parent[MAX_PATH];
    dirOf(dir, parent, sizeof(parent));
    static char loc[300];
    snprintf(loc, sizeof(loc), "/listSDCard?DIR=%s", U1(parent));
    g_server.sendHeader("Location", loc, true);
    g_server.send(303, "text/plain", "");
  } else {
    g_server.send(500, "text/plain", "Failed to rmdir (empty?)");
  }
}

// POST /action/append file=/LOG.TXT&text=… — legacy append 0x04 (see handleUploadPiece).
static void handleAppend() {
  static char path[MAX_PATH];
  copyArg("file", path, sizeof(path));
  static char text[256];
  copyArg("text", text, sizeof(text));
  if (path[0] == '\0') {
    g_server.send(400, "text/plain", "Missing file argument");
    return;
  }
  MicroSDExpFile *a = openFile(&g_microSDexp, path, "a");
  if (!a) {
    g_server.send(500, "text/plain", "append open failed");
    return;
  }
  size_t want = boundedLen(text);
  size_t got = fileWrite(a, (const uint8_t *)text, want);
  bool ok = fileClose(a) && got == want;
  g_server.send(ok ? 200 : 500, "text/plain", ok ? "appended" : "append failed");
}

// --- Upload (multipart streaming): two callbacks, one open handle, no buffering ---
// ESP32 WebServer delivers the body as a sequence of UPLOAD_FILE_* chunks.
// START opens the module file (openFile "w" truncates — the GUI's upload
// never appends), WRITE streams bytes as they arrive (fileWrite flow-paces
// them to the module in 64 B I2C blocks), END closes. HTTP state (302/200)
// is replied only by handleUploadPost, which runs after all pieces pass.
static void handleUploadPiece() {
  HTTPUpload &up = g_server.upload();
  if (up.status == UPLOAD_FILE_START) {
    char dest[MAX_PATH];
    if (g_server.hasArg("dest")) copyArg("dest", dest, sizeof(dest));
    else {
      dest[0] = '/';
      dest[1] = '\0';
    }
    char base[MAX_PATH];
    baseOf(up.filename.c_str(), base, sizeof(base));
    if (base[0] == '\0') {
      base[0] = 'u';
      base[1] = '.';
      base[2] = 'b';
      base[3] = 'i';
      base[4] = 'n';
      base[5] = '\0';
    }
    static char target[MAX_PATH];
    joinPath(dest, base, target, sizeof(target));
    Serial.printf("upload -> %s\n", target);
    g_upload = openFile(&g_microSDexp, target, "w");
    g_upload_ok = (g_upload != nullptr);
    g_upload_bytes = 0;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_upload && g_upload_ok) {
      if (fileWrite(g_upload, up.buf, up.currentSize) != up.currentSize) {
        g_upload_ok = false;
      } else {
        g_upload_bytes += up.currentSize;
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_upload) {
      if (g_upload_ok) fileClose(g_upload);
      g_upload = nullptr;
    }
    Serial.printf("upload end %lu %s\n", (unsigned long)g_upload_bytes,
                  g_upload_ok ? "OK" : "FAIL");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    // Client went away mid-upload: discard the
    // partial file, free the microSDexp, and let the next request proceed.
    Serial.println("upload aborted: discarding partial file");
    if (g_upload) {
      fileClose(g_upload);
      g_upload = nullptr;
    }
    g_upload_ok = false;
  }
}

// POST /upload finalizer (runs after handleUploadPiece finished anchoring).
static void handleUploadPost() {
  char msg[32];
  snprintf(msg, sizeof(msg), "%s %lu", g_upload_ok ? "OK" : "FAIL",
           (unsigned long)g_upload_bytes);
  g_server.send(g_upload_ok ? 200 : 500, "text/plain", msg);
}

// GET /download?file=/… or any /… that names an existing file (see
// handleNotFound). `?download=1` forces application/octet-stream.
static void handleDownload() {
  static char path[MAX_PATH];
  if (g_server.hasArg("file")) copyArg("file", path, sizeof(path));
  else copyUri(path, sizeof(path));
  if (path[0] == '\0') {
    g_server.send(400, "text/plain", "Missing file argument");
    return;
  }
  bool dl = g_server.hasArg("download");
  serveFile(path, dl);
}

// Catch-all: serve a file if the URI names one, else a 404 with the args.
static void handleNotFound() {
  static char uri[MAX_PATH];
  copyUri(uri, sizeof(uri));
  if (g_microSDexp.exists(uri)) {
    serveFile(uri, false);
    return;
  }
  char row[512];
  snprintf(row, sizeof(row), "Not found: %s\nMethod: %s\nArgs: %d", H1(uri),
           g_server.method() == HTTP_GET ? "GET" : "POST", (int)g_server.args());
  g_server.send(404, "text/html", row);
}

static void handleApiStatus() {
  StatusFrame st;
  if (!g_microSDexp.readStatus(st)) {
    g_server.send(503, "application/json", "{\"error\":\"no-module\"}");
    return;
  }
  static char b[256];
  snprintf(b, sizeof(b),
           "{\"rev\":%u,\"state\":%u,\"opDone\":%u,\"error\":%u,\"rxFree\":%u,"
           "\"txAvail\":%u,\"result\":%lu}",
           (unsigned)st.rev, (unsigned)st.state, (unsigned)st.opDone,
           (unsigned)st.error, (unsigned)st.rxFree, (unsigned)st.txAvail,
           (unsigned long)st.result);
  g_server.send(200, "application/json", b);
}

static void handleApiVersion() {
  char v[24];
  if (g_microSDexp.version(v, sizeof(v)) == 0) {
    g_server.send(500, "application/json", "{\"error\":\"no-module\"}");
    return;
  }
  char b[64];
  snprintf(b, sizeof(b), "{\"version\":\"%s\"}", J1(v));
  g_server.send(200, "application/json", b);
}

static void handleApiInfo() {
  CardInfo ci;
  if (!g_microSDexp.cardInfo(&ci)) {
    g_server.send(500, "application/json", "{\"error\":\"no-module\"}");
    return;
  }
  // freeBytes = freeClusters * bytesPerCluster (= sectorCount*512/clusterCount).
  uint64_t freeBytes = 0;
  uint64_t totalBytes = (uint64_t)ci.sectorCount * 512UL;
  if (ci.clusterCount != 0) freeBytes = (uint64_t)ci.freeClusters * totalBytes / (uint64_t)ci.clusterCount;
  char b[384];
  snprintf(b, sizeof(b),
           "{\"mounted\":%u,\"fatType\":%u,\"cardType\":%u,\"sectorCount\":%lu,"
           "\"freeClusters\":%lu,\"clusterCount\":%lu,\"sckHz\":%lu,"
           "\"freeBytes\":%llu,\"totalBytes\":%llu}",
           (unsigned)ci.mounted, (unsigned)ci.fatType, (unsigned)ci.cardType,
           (unsigned long)ci.sectorCount, (unsigned long)ci.freeClusters,
           (unsigned long)ci.clusterCount, (unsigned long)ci.sckHz,
           (unsigned long long)freeBytes, (unsigned long long)totalBytes);
  g_server.send(200, "application/json", b);
}

static void handleApiTemp() {
  uint16_t k = g_microSDexp.temperatureK();
  char b[64];
  snprintf(b, sizeof(b), "{\"kelvin\":%u,\"celsius\":%d}", (unsigned)k,
           (int)k - 273);
  g_server.send(200, "application/json", b);
}

static void handleApiSize() {
  char path[MAX_PATH];
  copyArg("file", path, sizeof(path));
  char b[320];
  snprintf(b, sizeof(b), "{\"file\":\"%s\",\"size\":%lu,\"exists\":%d}", J1(path),
           (unsigned long)g_microSDexp.fileSize(path),
           g_microSDexp.exists(path) ? 1 : 0);
  g_server.send(200, "application/json", b);
}

static void handleApiExists() {
  char path[MAX_PATH];
  copyArg("file", path, sizeof(path));
  char b[320];
  snprintf(b, sizeof(b), "{\"file\":\"%s\",\"exists\":%d,\"isDirectory\":%d}", J1(path),
           g_microSDexp.exists(path) ? 1 : 0,
           g_microSDexp.isDirectory(path) ? 1 : 0);
  g_server.send(200, "application/json", b);
}

static void handleApiList() {
  char dir[MAX_PATH];
  if (g_server.hasArg("DIR")) copyArg("DIR", dir, sizeof(dir));
  else {
    dir[0] = '/';
    dir[1] = '\0';
  }
  uint8_t n = g_microSDexp.listDir(dir, g_entries, MAX_ENTRIES);
  static char out[2048];
  int m = snprintf(out, sizeof(out), "{\"dir\":\"%s\",\"count\":%u,\"entries\":[", J1(dir), n);
  if (m < 0) m = 0;
  if ((size_t)m >= sizeof(out)) m = sizeof(out) - 1;
  for (uint8_t i = 0; i < n; i++) {
    int k = snprintf(out + m, sizeof(out) - (size_t)m,
                     "%s{\"isDir\":%s,\"name\":\"%s\",\"size\":%lu}", i ? "," : "",
                     g_entries[i].isDir ? "true" : "false", J2(g_entries[i].name),
                     (unsigned long)g_entries[i].size);
    if (k <= 0) break;
    m += k;
    if ((size_t)m >= sizeof(out)) break;
  }
  snprintf(out + m, sizeof(out) - (size_t)m, "]}");
  g_server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// setup()/loop(): Wi-Fi join (the DHCP line on Serial is the IP source for
// curl), one module probe (version), then routes. Everything else is behind
// the handlers above; loop() is just handleClient().
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("--- WebSDModule start ---");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID_TEXT, PASS_TEXT);
  Serial.print("WiFi connecting ");
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi failed");
  }
  // v18.8.21: let one READ drain both 128 B module TX buffers. ESP32 Wire
  // clamps requestFrom() to its receive buffer, so grow it before begin().
  Wire.setBufferSize(256);
  Wire.begin(); // sketch owns pins: custom boards use Wire.begin(sda,scl)
  g_microSDexp.setReadChunkBytes(256);
  if (!g_microSDexp.begin(MICROSDEXP_ADDR, 1000000)) {
    Serial.println(F("microSD begin FAILED @0x6E"));
  } else {
    char v[24];
    g_microSDexp.version(v, sizeof(v));
    Serial.print("microSD version: ");
    Serial.println(v);
  }
  g_server.on("/", handleRoot);
  g_server.on("/listSDCard", handleList);
  g_server.on("/upload", HTTP_POST, handleUploadPost, handleUploadPiece);
  g_server.on("/deleteFile", HTTP_POST, handleDelete);
  g_server.on("/action/mkdir", HTTP_POST, handleMkdir);
  g_server.on("/action/rmdir", HTTP_POST, handleRmdir);
  g_server.on("/action/append", HTTP_POST, handleAppend);
  g_server.on("/download", handleDownload);
  g_server.on("/api/status", handleApiStatus);
  g_server.on("/api/version", handleApiVersion);
  g_server.on("/api/info", handleApiInfo);
  g_server.on("/api/temp", handleApiTemp);
  g_server.on("/api/size", handleApiSize);
  g_server.on("/api/exists", handleApiExists);
  g_server.on("/api/list", handleApiList);
  g_server.onNotFound(handleNotFound);
  if (WiFi.status() == WL_CONNECTED) {
    g_server.begin();
    Serial.print("HTTP server started @ http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
  } else {
    Serial.println("HTTP server NOT started (no WiFi)");
  }
}

void loop() {
  g_server.handleClient();
}





