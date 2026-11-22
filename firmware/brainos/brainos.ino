// BrainOS: a voice notebook for the Seeed reTerminal Sticky. Built on Sticky OS (MIT).
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "board.h"
#include "capture.h"
#include "keyboard.h"
#include "config.h"
#include "export.h"
#include "net.h"
#include "notes.h"
#include "ota.h"
#include "rtc.h"
#include "screen.h"
#include "stt.h"
#include "touch.h"
#include "ui.h"
#include "web.h"

#define STR_(x) #x
#define STR(x) STR_(x)
#ifndef STICKY_VERSION
#define STICKY_VERSION dev-unknown
#endif
extern const char *const VERSION;
const char *const VERSION = STR(STICKY_VERSION);

static const int PORTRAIT = 3;          // quarter turns: buttons at the top (1 is upside down on this board)

void logf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
}

// ---------------------------------------------------------------- recording

static bool keepRecording() {
  if (okDown()) return true;
  if (uiListeningDone()) uiRender(false);   // "Saving…" the moment OK is let go
  return false;
}

static void recordNote() {
  beep(2600, 40);
  Screen before = screen == SCREEN_LISTENING ? SCREEN_NOTES : screen;
  uiGo(SCREEN_LISTENING);
  String path, id;
  int secs = captureRecord(keepRecording, path, id);
  okConsume();
  if (!secs) {
    Serial.printf("capture: %s\n", captureError());
    uiGo(before == SCREEN_LOCK ? SCREEN_LOCK : SCREEN_NOTES);
    return;
  }
  notesAdd("", path, secs, id);   // the file is named after the id
  beep(2000, 60);
  uiShowSaved(secs);
}

// Settings -> Update: a check, or an install, run on the loop (Update writes flash: not a PSRAM-stack task).
static bool updCheckAsk, updInstallAsk, updBusyInstalling;
bool updateQueued() { return updCheckAsk || updInstallAsk; }
bool updateInstalling() { return updBusyInstalling; }
void updateRequest(bool install) { if (install) updInstallAsk = true; else updCheckAsk = true; netTouch(); uiRender(false); }
static void updateTick() {
  if (updCheckAsk || updInstallAsk) {
    bool install = updInstallAsk;
    updCheckAsk = updInstallAsk = false;
    if (!netUp()) { Serial.println("update: no wifi"); uiRender(false); return; }
    if (install) { updBusyInstalling = true; uiRender(false); updInstall(); updBusyInstalling = false; }   // "Installing" stays on screen; a success restarts
    else updCheck();
    uiRender(false);
  }
}

static void goToSleep() {   // on battery: the lock screen stays on the e-paper; OK wakes, another OK unlocks where it was
  updMarkGood("sleep");
  uiLock();
  uiRender(true);
  uiSaveForSleep();
  screenSleep();
  deepSleep();
}

// `nettest`: a small GET to OpenRouter (key + TLS) on the loop task.
static void netTest() {
  if (!netUp()) { Serial.println("nettest: no wifi"); return; }
  {
    WiFiClientSecure tls; netTrust(tls); tls.setTimeout(20000);
    HTTPClient http; http.setTimeout(20000); http.setConnectTimeout(NET_CONNECT_MS);
    uint32_t t = millis();
    if (!http.begin(tls, "https://openrouter.ai/api/v1/auth/key")) { Serial.println("nettest: begin failed"); return; }
    http.addHeader("Authorization", "Bearer " + cfg::str("stt.api_key"));
    int code = http.GET();
    String body; if (code > 0) netReadBody(http, body, 4000);
    char err[80] = ""; tls.lastError(err, sizeof err);
    Serial.printf("nettest: GET auth/key -> %d in %lu ms, %s %s\n", code, (unsigned long)(millis() - t), body.substring(0, 160).c_str(), err);
    http.end();
  }
}

// ---------------------------------------------------------------- serial

static void shot() {
  const uint8_t *b = canvas->getBuffer();
  Serial.printf("SHOT %d %d %d\n", NW, NH, rotation);
  static const char hex[] = "0123456789abcdef";
  char line[NW / 8 * 2 + 1];
  for (int y = 0; y < NH; y++) {
    for (int x = 0; x < NW / 8; x++) { uint8_t v = b[y * (NW / 8) + x]; line[x * 2] = hex[v >> 4]; line[x * 2 + 1] = hex[v & 15]; }
    line[NW / 8 * 2] = 0;
    Serial.println(line);
  }
  Serial.println("END");
}

static void serialCommand(char *line) {
  int Y, M, D, h, m, x, y;
  if (!strcmp(line, "shot")) shot();
  else if (!strcmp(line, "stats")) { logHeap("stats"); Serial.printf("stats: refreshes full %d partial %d, uptime %lu s, screen %d, notes %d, pending %d\n", refreshCount(true), refreshCount(false), millis() / 1000, screen, notesCount(false), notesPending()); }
  else if (!strcmp(line, "sleep")) goToSleep();
  else if (sscanf(line, "time %d-%d-%d %d:%d", &Y, &M, &D, &h, &m) == 5) {
    struct tm t = {}; t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = m;
    time_t secs = mktime(&t); struct timeval tv = {secs, 0}; settimeofday(&tv, nullptr);
    rtcFromSystem();
    Serial.println("time: set (local)");
    uiRender(false);
  } else if (!strncmp(line, "note add ", 9)) { notesAdd(line + 9, "", 0); uiRender(false); Serial.println("note: added"); }
  else if (!strcmp(line, "notes")) { for (auto &n : notesAll()) Serial.printf("%s %lu %s%s%s %s\n", n.id.c_str(), (unsigned long)n.created, n.pinned ? "P" : "-", n.archived ? "A" : "-", n.audio.length() ? "W" : "-", n.audio.length() ? n.audio.c_str() : n.text.c_str()); }
  else if (!strcmp(line, "wipe")) { for (auto &n : notesAll()) if (n.audio.length()) LittleFS.remove(n.audio); notesAll().clear(); notesSave(); uiRender(false); Serial.println("notes: wiped"); }
  else if (sscanf(line, "tap %d %d", &x, &y) == 2) uiTap(x, y);
  else if (!strcmp(line, "ok")) uiOk();
  else if (sscanf(line, "move %d", &x) == 1) uiMove(x);
  else if (!strcmp(line, "lock")) uiGo(SCREEN_LOCK);
  else if (!strncmp(line, "wifi add ", 9)) {   // wifi add <ssid> <password>  (typed by the owner in the terminal)
    char *sp = strchr(line + 9, ' ');
    if (!sp) { Serial.println("wifi add <ssid> <password>"); return; }
    *sp = 0;
    std::vector<WifiCred> nets = cfg::wifiNetworks();
    nets.push_back({String(line + 9), String(sp + 1)});
    String err; cfg::setWifi(nets, err);
    Serial.printf("wifi: %s\n", err.length() ? err.c_str() : "saved"); netTouch();
  } else if (!strcmp(line, "wifi")) { Serial.printf("wifi: %s %s %d dBm, wanted %d, %u networks saved, key %s, pending %d\n", netUp() ? "up" : "down", netAddress().c_str(), netSignal(), netWanted(), (unsigned)cfg::wifiNetworks().size(), sttConfigured() ? "set" : "none", notesPending()); }
  else if (!strcmp(line, "nettest")) netTest();
  else if (!strcmp(line, "wifi seen")) {   // are the saved networks in range? (prints no other names)
    std::vector<NetSeen> seen; String why; netScan(seen, why);
    for (auto &n : cfg::wifiNetworks()) { int rssi = 0; for (auto &x : seen) if (x.ssid == n.ssid) rssi = x.rssi; Serial.printf("wifi: %s %s\n", n.ssid.c_str(), rssi ? (String("in range, ") + rssi + " dBm").c_str() : "not in range"); }
  }
  else if (!strcmp(line, "sensors")) readSensors();
  else if (!strcmp(line, "fs")) { Serial.printf("fs: %u used of %u\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes()); File d = LittleFS.open("/queue"); File f; while ((f = d.openNextFile())) Serial.printf("  %s %u\n", f.name(), (unsigned)f.size()); }
  else Serial.printf("? %s\n", line);
}

static void serialPoll() {
  static char line[400];
  static int len;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (len) { line[len] = 0; serialCommand(line); len = 0; } }
    else if (len < (int)sizeof(line) - 1) line[len++] = c;
  }
}

// ---------------------------------------------------------------- setup and loop

void setup() {
  boardBegin();
  Serial.begin(115200);
  delay(50);
  Serial.printf("\nbrainos: %s starting\n", VERSION);
  updBegin();
  cfg::begin();
  setenv("TZ", cfg::str("device.timezone").c_str(), 1);
  tzset();
  soundOn = cfg::flag("sound");
  if (!screenBegin()) Serial.println("screen: FAILED");
  else Serial.printf("screen: ok (%s)\n", panelName);
  screenRotation(PORTRAIT);
  sensorsBegin();
  readSensors();
  rtcBegin();
  rtcToSystem();
  if (!touchBegin()) Serial.println("touch: FAILED");
  if (!LittleFS.begin(true)) Serial.println("fs: FAILED");
  notesBegin();
  netBegin();
  sttBegin();
  webBegin();
  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  if (why == ESP_SLEEP_WAKEUP_EXT1 && uiRestoreAfterSleep()) uiRender(false);  // the lock screen stayed; unlocking goes back where it was
  else { screen = SCREEN_LOCK; uiRender(true); }
  lastActivity = millis();
  logHeap("ready");
  Serial.println("brainos: ready");
}

void loop() {
  static uint32_t lastMinute, savedAt;
  TouchEvent te = touchPoll();
  if (te.kind != TOUCH_NONE) netTouch();
  if (te.kind == TOUCH_TAP) { Serial.printf("touch: tap %d, %d\n", te.x, te.y); uiTap(te.x, te.y); }
  else if (te.kind == TOUCH_SWIPE) uiSwipe(te.x, te.y, te.dx, te.dy);

  ButtonPoll bp = buttonsPoll();
  if (bp.event != BTN_NONE) netTouch();
  switch (bp.event) {
    case BTN_OK: beep(2600, 30); uiOk(); break;
    case BTN_OK_HOLD: recordNote(); break;
    case BTN_MOVE: beep(2600, 30); uiMove(bp.move); break;
    case BTN_BOTH: beep(2600, 30); uiLock(); break;
    case BTN_BOTH_LONG: beep(1300, 120); goToSleep(); break;
    default: break;
  }
  if (screen == SCREEN_SAVED) { if (!savedAt) savedAt = millis(); else if (millis() - savedAt > 2500) { savedAt = 0; uiGo(SCREEN_NOTES); } }
  else savedAt = 0;

  if (redrawDue()) uiRender(false);
  serialPoll();
  netTick();
  sttTick();
  webTick();
  exportTick();
  updateTick();
  if (updPendingVerify() && (netUp() || millis() > 60000)) updMarkGood(netUp() ? "online" : "60 s up");

  if (millis() - lastMinute > 60000) {  // the clock and the battery, with a cheap partial refresh
    lastMinute = millis();
    readSensors();
    if (screen == SCREEN_LOCK) uiRender(false);
  }
  static long sleepMin = -1;
  if (sleepMin < 0 || millis() - lastMinute < 20) sleepMin = cfg::num("sleep.min");   // read once a minute (and right after the page saved it)
  if (sleepMin > 0 && !now.usb && millis() - lastActivity > (uint32_t)sleepMin * 60000 && !sttRunning() && screen != SCREEN_EXPORT) {
    goToSleep();
  }
  delay(10);
}
