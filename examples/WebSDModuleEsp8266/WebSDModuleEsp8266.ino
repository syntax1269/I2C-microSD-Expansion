// WebSDModuleEsp8266.ino — ESP01-S Wi-Fi WebServer slim web test for I²C microSD Expansion module.
// ESP8266 DRAM (80KB) cannot hold the ESP32 Explorer 16 KB HTML page
// (ld: .bss outside dram0). This twin streams HTML in 512 B chunks and
// keeps only the metric routes + minimal browser UI: /api/* JSON,
// /upload, /download. Pins: SDA GPIO0 / SCL GPIO2 (sketch owns Wire).
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <I2C_microSD_Expansion.h>

#define MICROSDEXP_ADDR 0x6E
#define SSID_TEXT "YOUR_WIFI_SSID"
#define PASS_TEXT "YOUR_WIFI_PASSWORD"
#define MAX_PATH 260
#define MAX_ENTRIES 32
#define IO_BUFSZ 128
// Bus ceiling = lowest max among devices on the bus. Sole-device absolute
// stability for ESP8266 (web path) is 400 kHz Fm: raw I2C tolerates 1 MHz
// Fm+ at 160 MHz CPU (PASS D660AF09), but multipart web upload at 1 MHz
// corrupted the stored file while 400 kHz was byte-identical. Bulk upload
// therefore runs at 400 kHz; idle/other ops stay at 1 MHz (also clamped
// to 400 kHz at 80 MHz CPU). Any added device caps the bus at its own max.
#define I2C_IDLE_HZ 1000000
#define I2C_UPLOAD_HZ 400000

static I2CmicroSD g_microSDexp;
static ESP8266WebServer g_server(80);
static File *g_upload = nullptr;
static bool g_upload_ok = false;
static uint32_t g_upload_bytes = 0;
static DirEntry g_entries[MAX_ENTRIES];

static void copyArg(const char *key, char *dst, size_t cap) {
  auto s = g_server.arg(key);
  const char *p = s.c_str();
  size_t n = 0;
  while (n + 1 < cap && p[n] != '\0') { dst[n] = p[n]; n++; }
  dst[n] = '\0';
}
static void copyUri(char *dst, size_t cap) {
  auto s = g_server.uri();
  const char *p = s.c_str();
  size_t n = 0;
  while (n + 1 < cap && p[n] != '\0') { dst[n] = p[n]; n++; }
  dst[n] = '\0';
}
static void joinPath(const char *dir, const char *name, char *dst, size_t cap) {
  size_t n = 0; bool root = (dir[0] == '/' && dir[1] == '\0');
  if (!root) {
    while (dir[n] != '\0' && n + 1 < cap) { dst[n] = dir[n]; n++; }
    if (n + 1 < cap) dst[n++] = '/';
  } else {
    dst[n++] = '/';
  }
  size_t i = 0; while (name[i] != '\0' && n + 1 < cap) dst[n++] = name[i++];
  dst[n] = '\0';
}
static void dirOf(const char *p, char *dst, size_t cap) {
  int s = -1; for (int i = 0; p[i] != '\0' && i < MAX_PATH; i++) if (p[i] == '/') s = i;
  if (s <= 0) { dst[0] = '/'; dst[1] = '\0'; return; }
  for (int i = 0; i < s && (size_t)i + 1 < cap; i++) { dst[i] = p[i]; dst[s] = '\0'; }
}
static void baseOf(const char *p, char *dst, size_t cap) {
  int s = -1; for (int i = 0; p[i] != '\0' && i < MAX_PATH; i++) if (p[i] == '/' || p[i] == '\\') s = i;
  size_t n = 0; for (int i = s + 1; p[i] != '\0' && n + 1 < cap; i++) dst[n++] = p[i]; dst[n] = '\0';
}
static const char *mimeOf(const char *p) {
  size_t n = 0; while (p[n] != '\0' && n < MAX_PATH) n++;
  if (n >= 4 && p[n-4] == '.' && (p[n-3]=='h'||p[n-3]=='H') && (p[n-2]=='t'||p[n-2]=='T') && (p[n-1]=='m'||p[n-1]=='M')) return "text/html";
  if (n >= 4 && p[n-4] == '.' && p[n-3]=='p' && p[n-2]=='n' && p[n-1]=='g') return "image/png";
  if (n >= 4 && p[n-4] == '.' && p[n-3]=='j' && p[n-2]=='p' && p[n-1]=='g') return "image/jpeg";
  if (n >= 3 && p[n-3] == '.' && (p[n-2]=='t'||p[n-2]=='T') && (p[n-1]=='x'||p[n-1]=='X')) return "text/plain";
  return "application/octet-stream";
}

static void serveFile(const char *path, bool download) {
  if (!g_microSDexp.exists(path)) { g_server.send(404, "text/html", "Not found"); return; }
  const char *mime = download ? "application/octet-stream" : mimeOf(path);
  uint32_t sz = g_microSDexp.fileSize(path);
  File *f = openFile(&g_microSDexp, path, "r");
  if (!f) { g_server.send(500, "text/plain", "open failed"); return; }
  g_server.setContentLength((size_t)-1);
  g_server.send(200, mime, "");
  static uint8_t buf[IO_BUFSZ];
  uint32_t left = sz; bool err = false;
  while (left > 0) {
    if (!g_server.client().connected()) { err = true; break; }
    size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
    size_t n = fileRead(f, buf, want);
    if (n == 0) { err = true; break; }
    g_server.sendContent((const char *)buf, n);
    left -= (uint32_t)n; yield();
  }
  fileClose(f);
  if (!err) g_server.sendContent("");
  else Serial.println("download stream error");
}

static void handleRoot() {
  char v[24]; g_microSDexp.version(v, sizeof(v));
  g_server.setContentLength((size_t)-1);
  g_server.send(200, "text/html", "");
  g_server.sendContent("<!DOCTYPE html><html><head><title>module Esp8266</title></head><body>");
  g_server.sendContent("<h2>ESP01-S I²C microSD Expansion</h2>");
  char b[128];
  snprintf(b, sizeof(b), "<p>module <b>0x6E</b> fw <b>%s</b> SDA0/SCL2 1MHz</p>", v);
  g_server.sendContent(b);
  g_server.sendContent("<p><a href='/listSDCard?DIR=/'>Files</a> <a href='/api/info'>/api/info</a> <a href='/api/version'>/api/version</a> <a href='/api/status'>/api/status</a> <a href='/api/temp'>/api/temp</a></p>");
  g_server.sendContent("<form method='POST' action='/upload' enctype='multipart/form-data'><div class=drop id=dropUpload><input type='file' name='file' id=fileInput> <button type='submit'>Upload to /</button></div></form>");
  g_server.sendContent("<p>Curl: speed test via /upload and /download</p>");
  // table root listing streamed
  uint8_t count = g_microSDexp.listDir("/", g_entries, MAX_ENTRIES);
  char table[512];
  snprintf(table, sizeof(table), "<table border=1><tr><th>Type</th><th>Name</th><th>Size</th></tr>");
  g_server.sendContent(table);
  for (uint8_t i = 0; i < count; i++) {
    char entry[MAX_PATH]; joinPath("/", g_entries[i].name, entry, sizeof(entry));
    char row[1024];
    if (g_entries[i].isDir) snprintf(row, sizeof(row), "<tr><td>[D]</td><td><a href='/listSDCard?DIR=%s'>%s/</a></td><td>---</td></tr>", entry, g_entries[i].name);
    else snprintf(row, sizeof(row), "<tr><td>[F]</td><td><a href='/download?file=%s'>%s</a></td><td>%lu</td></tr>", entry, g_entries[i].name, (unsigned long)g_entries[i].size);
    g_server.sendContent(row);
  }
  g_server.sendContent("</table>");
  g_server.sendContent("<script>function showPanel(id,btn){}const drop=document.getElementById('dropUpload');drop.addEventListener('dragover',e=>e.preventDefault());drop.addEventListener('drop',async e=>{e.preventDefault();const f=e.dataTransfer.files[0];if(!f)return;const fd=new FormData();fd.append('file',f);fd.append('dest','/');const r=await fetch('/upload',{method:'POST',body:fd});alert(await r.text());});</script>");
  g_server.sendContent("</body></html>");
  g_server.sendContent("");
}

static void handleList() {
  static char dir[MAX_PATH];
  if (g_server.hasArg("DIR")) copyArg("DIR", dir, sizeof(dir)); else { dir[0]='/'; dir[1]='\0'; }
  g_server.setContentLength((size_t)-1); g_server.send(200, "text/html", "");
  char b[1024];
  snprintf(b, sizeof(b), "<!DOCTYPE html><html><head><title>Files</title></head><body><h2>%s</h2><p><a href='/'>Home</a></p>", dir);
  g_server.sendContent(b);
  char parent[MAX_PATH]; dirOf(dir, parent, sizeof(parent));
  if (!(dir[0] == '/' && dir[1] == '\0')) { snprintf(b, sizeof(b), "<p><a href='/listSDCard?DIR=%s'>Up</a></p>", parent); g_server.sendContent(b); }
  uint8_t total = g_microSDexp.listDir(dir, g_entries, MAX_ENTRIES);
  g_server.sendContent("<table border=1><tr><th>Type</th><th>Del</th><th>Name</th><th>Size</th></tr>");
  for (uint8_t i = 0; i < total; i++) {
    char entry[MAX_PATH]; joinPath(dir, g_entries[i].name, entry, sizeof(entry));
    char row[1024];
    if (g_entries[i].isDir) snprintf(row, sizeof(row), "<tr><td>[D]</td><td>---</td><td><a href='./listSDCard?DIR=%s'>%s/</a></td><td>---</td></tr>", entry, g_entries[i].name);
    else snprintf(row, sizeof(row), "<tr><td>[F]</td><td><form method='POST' action='/deleteFile' style='display:inline'><input type='hidden' name='file' value='%s'><button>Del</button></form></td><td><a href='/download?file=%s'>%s</a></td><td>%lu</td></tr>", entry, entry, g_entries[i].name, (unsigned long)g_entries[i].size);
    g_server.sendContent(row);
  }
  g_server.sendContent("</table>");
  snprintf(b, sizeof(b), "<form method='POST' action='/upload' enctype='multipart/form-data'><div class=drop id=dropUpload>Drop into <b>%s</b><br><input type='hidden' name='dest' value='%s'><input type='file' name='file' id=fileInput> <button type='submit'>Upload here</button></div></form>", dir, dir);
  g_server.sendContent(b);
  snprintf(b, sizeof(b), "<script>const drop=document.getElementById('dropUpload');drop.addEventListener('dragover',e=>e.preventDefault());drop.addEventListener('drop',async e=>{e.preventDefault();const f=e.dataTransfer.files[0];if(!f)return;const fd=new FormData();fd.append('file',f);fd.append('dest','%s');const r=await fetch('/upload',{method:'POST',body:fd});alert(await r.text());});</script>", dir);
  g_server.sendContent(b);
  g_server.sendContent("</body></html>");
  g_server.sendContent("");
}

static void handleDelete() {
  if (g_server.method() != HTTP_POST) { g_server.send(405, "text/plain", "Method Not Allowed"); return; }
  static char path[MAX_PATH]; copyArg("file", path, sizeof(path));
  if (path[0] == '\0') { g_server.send(400, "text/plain", "Missing file"); return; }
  if (g_microSDexp.remove(path)) { static char parent[MAX_PATH]; dirOf(path, parent, sizeof(parent)); static char loc[300]; snprintf(loc, sizeof(loc), "/listSDCard?DIR=%s", parent); g_server.sendHeader("Location", loc, true); g_server.send(303, "text/plain", ""); }
  else g_server.send(500, "text/plain", "Failed");
}
static void handleMkdir() { static char dir[MAX_PATH]; copyArg("dir", dir, sizeof(dir)); if (dir[0]=='\0') { g_server.send(400,"text/plain","Missing dir"); return; } if (g_microSDexp.mkdir(dir)) { static char loc[300]; snprintf(loc,sizeof(loc),"/listSDCard?DIR=%s",dir); g_server.sendHeader("Location",loc,true); g_server.send(303,"text/plain",""); } else g_server.send(500,"text/plain","Failed mkdir"); }
static void handleRmdir() { static char dir[MAX_PATH]; copyArg("dir", dir, sizeof(dir)); if (dir[0]=='\0') { g_server.send(400,"text/plain","Missing dir"); return; } if (g_microSDexp.rmdir(dir)) { static char parent[MAX_PATH]; dirOf(dir,parent,sizeof(parent)); static char loc[300]; snprintf(loc,sizeof(loc),"/listSDCard?DIR=%s",parent); g_server.sendHeader("Location",loc,true); g_server.send(303,"text/plain",""); } else g_server.send(500,"text/plain","Failed rmdir"); }
static void handleAppend() {
  static char path[MAX_PATH]; copyArg("file", path, sizeof(path));
  static char text[256]; copyArg("text", text, sizeof(text));
  if (path[0]=='\0') { g_server.send(400,"text/plain","Missing file"); return; }
  File *a = openFile(&g_microSDexp, path, "a");
  if (!a) { g_server.send(500,"text/plain","append open failed"); return; }
  size_t want = 0; while (text[want]!='\0' && want<sizeof(text)) want++;
  size_t got = fileWrite(a,(const uint8_t*)text,want); bool ok = fileClose(a) && got==want;
  g_server.send(ok?200:500,"text/plain",ok?"appended":"append failed");
}
static void handleUploadPiece() {
  HTTPUpload &up = g_server.upload();
  if (up.status == UPLOAD_FILE_START) {
    // Drop to absolute-stable 400 kHz for bulk multipart upload (lowest-
    // wins bus rule); restored in handleUploadPost.
    g_microSDexp.begin(MICROSDEXP_ADDR, I2C_UPLOAD_HZ);
    char dest[MAX_PATH]; if (g_server.hasArg("dest")) copyArg("dest",dest,sizeof(dest)); else { dest[0]='/'; dest[1]='\0'; }
    (void)dest; // root only in this twin; dest respected by list form via hidden but startup uses /
    char base[MAX_PATH]; baseOf(up.filename.c_str(), base, sizeof(base));
    if (base[0]=='\0') { base[0]='u'; base[1]='.'; base[2]='b'; base[3]='i'; base[4]='n'; base[5]='\0'; }
    static char target[MAX_PATH]; joinPath(dest, base, target, sizeof(target));
    Serial.printf("upload -> %s\n", target);
    g_upload = openFile(&g_microSDexp, target, "w"); g_upload_ok = (g_upload!=nullptr); g_upload_bytes=0;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_upload && g_upload_ok) { if (fileWrite(g_upload, up.buf, up.currentSize)!=up.currentSize) g_upload_ok=false; else g_upload_bytes+=up.currentSize; }
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_upload) { if (g_upload_ok) fileClose(g_upload); g_upload=nullptr; }
    Serial.printf("upload end %lu %s\n",(unsigned long)g_upload_bytes,g_upload_ok?"OK":"FAIL");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Serial.println("upload aborted"); if (g_upload) { fileClose(g_upload); g_upload=nullptr; } g_upload_ok=false;
    g_microSDexp.begin(MICROSDEXP_ADDR, I2C_IDLE_HZ);
  }
}
static void handleUploadPost() { char msg[32]; snprintf(msg,sizeof(msg),"%s %lu",g_upload_ok?"OK":"FAIL",(unsigned long)g_upload_bytes); g_microSDexp.begin(MICROSDEXP_ADDR, I2C_IDLE_HZ); g_server.send(g_upload_ok?200:500,"text/plain",msg); }
static void handleDownload() { static char path[MAX_PATH]; if (g_server.hasArg("file")) copyArg("file",path,sizeof(path)); else copyUri(path,sizeof(path)); if (path[0]=='\0') { g_server.send(400,"text/plain","Missing file"); return; } bool dl = g_server.hasArg("download"); serveFile(path,dl); }
static void handleNotFound() { static char uri[MAX_PATH]; copyUri(uri,sizeof(uri)); if (g_microSDexp.exists(uri)) { serveFile(uri,false); return; } char row[512]; snprintf(row,sizeof(row),"Not found: %s\nMethod: %s\nArgs: %d",uri,g_server.method()==HTTP_GET?"GET":"POST",(int)g_server.args()); g_server.send(404,"text/html",row); }

static void handleApiStatus() { StatusFrame st; if (!g_microSDexp.readStatus(st)) { g_server.send(503,"application/json","{\"error\":\"no-module\"}"); return; } static char b[256]; snprintf(b,sizeof(b),"{\"rev\":%u,\"state\":%u,\"opDone\":%u,\"error\":%u,\"rxFree\":%u,\"txAvail\":%u,\"result\":%lu}",(unsigned)st.rev,(unsigned)st.state,(unsigned)st.opDone,(unsigned)st.error,(unsigned)st.rxFree,(unsigned)st.txAvail,(unsigned long)st.result); g_server.send(200,"application/json",b); }
static void handleApiVersion() { char v[24]; if (g_microSDexp.version(v,sizeof(v))==0) { g_server.send(500,"application/json","{\"error\":\"no-module\"}"); return; } char b[64]; snprintf(b,sizeof(b),"{\"version\":\"%s\"}",v); g_server.send(200,"application/json",b); }
static void handleApiInfo() { CardInfo ci; if (!g_microSDexp.cardInfo(&ci)) { g_server.send(500,"application/json","{\"error\":\"no-module\"}"); return; } uint64_t freeBytes=0; uint64_t totalBytes=(uint64_t)ci.sectorCount*512UL; if (ci.clusterCount!=0) freeBytes=(uint64_t)ci.freeClusters*totalBytes/(uint64_t)ci.clusterCount; char b[384]; snprintf(b,sizeof(b),"{\"mounted\":%u,\"fatType\":%u,\"cardType\":%u,\"sectorCount\":%lu,\"freeClusters\":%lu,\"clusterCount\":%lu,\"sckHz\":%lu,\"freeBytes\":%llu,\"totalBytes\":%llu}",(unsigned)ci.mounted,(unsigned)ci.fatType,(unsigned)ci.cardType,(unsigned long)ci.sectorCount,(unsigned long)ci.freeClusters,(unsigned long)ci.clusterCount,(unsigned long)ci.sckHz,(unsigned long long)freeBytes,(unsigned long long)totalBytes); g_server.send(200,"application/json",b); }
static void handleApiTemp() { uint16_t k=g_microSDexp.temperatureK(); char b[64]; snprintf(b,sizeof(b),"{\"kelvin\":%u,\"celsius\":%d}",(unsigned)k,(int)k-273); g_server.send(200,"application/json",b); }
static void handleApiSize() { char path[MAX_PATH]; copyArg("file",path,sizeof(path)); char b[384]; snprintf(b,sizeof(b),"{\"file\":\"%s\",\"size\":%lu,\"exists\":%d}",path,(unsigned long)g_microSDexp.fileSize(path),g_microSDexp.exists(path)?1:0); g_server.send(200,"application/json",b); }
static void handleApiExists() { char path[MAX_PATH]; copyArg("file",path,sizeof(path)); char b[384]; snprintf(b,sizeof(b),"{\"file\":\"%s\",\"exists\":%d,\"isDirectory\":%d}",path,g_microSDexp.exists(path)?1:0,g_microSDexp.isDirectory(path)?1:0); g_server.send(200,"application/json",b); }
static void handleApiList() { char dir[MAX_PATH]; if (g_server.hasArg("DIR")) copyArg("DIR",dir,sizeof(dir)); else { dir[0]='/'; dir[1]='\0'; } uint8_t n=g_microSDexp.listDir(dir,g_entries,MAX_ENTRIES); static char out[4096]; int m=snprintf(out,sizeof(out),"{\"dir\":\"%s\",\"count\":%u,\"entries\":[",dir,n); if(m<0)m=0; for(uint8_t i=0;i<n;i++){ int k=snprintf(out+m,sizeof(out)-(size_t)m,"%s{\"isDir\":%s,\"name\":\"%s\",\"size\":%lu}",i?",":"",g_entries[i].isDir?"true":"false",g_entries[i].name,(unsigned long)g_entries[i].size); if(k<=0)break; m+=k; if((size_t)m>=sizeof(out))break; } snprintf(out+m,sizeof(out)-(size_t)m,"]}");
  g_server.send(200,"application/json",out); }

void setup() {
  Serial.begin(115200); delay(500);
  Serial.println("--- WebSDModuleEsp8266 start ---");
  WiFi.mode(WIFI_STA); WiFi.begin(SSID_TEXT, PASS_TEXT);
  Serial.print("WiFi connecting "); uint8_t tries=0;
  while (WiFi.status()!=WL_CONNECTED && tries<40) { delay(500); Serial.print("."); tries++; }
  if (WiFi.status()==WL_CONNECTED) { Serial.println(); Serial.print("WiFi connected IP: "); Serial.println(WiFi.localIP()); } else Serial.println("\nWiFi failed");
  Wire.begin(0, 2);
  if (!g_microSDexp.begin(MICROSDEXP_ADDR, I2C_IDLE_HZ)) Serial.println(F("microSD begin FAILED @0x6E"));
  else { char v[24]; g_microSDexp.version(v,sizeof(v)); Serial.print("microSD version: "); Serial.println(v); }
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
  if (WiFi.status()==WL_CONNECTED) { g_server.begin(); Serial.print("HTTP server started @ http://"); Serial.print(WiFi.localIP()); Serial.println("/"); } else Serial.println("HTTP server NOT started (no WiFi)");
}
void loop() { g_server.handleClient(); }





