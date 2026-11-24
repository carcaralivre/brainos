// BrainOS: a voice notebook for the Seeed reTerminal Sticky. Built on Sticky OS (MIT).
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "board.h"
#include "capture.h"
#include "chat.h"
#include "cost.h"
#include "dates.h"
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

// Held OK: a new note. Held Up: a new chat note. Held Down: in a note, a recording appended to
// it; in the passing chat, the next question; anywhere else, a new passing chat.
enum RecKind { REC_NOTE, REC_CHAT_NOTE, REC_DOWN };
static RecKind recording;
static bool keepRecording() {
  if (recording == REC_NOTE ? okDown() : recording == REC_CHAT_NOTE ? upDown() : downDown()) return true;
  if (uiListeningDone()) uiRender(false);   // "Saving…" the moment the button is let go
  return false;
}

static void record(int how) {   // a RecKind (an int: the sketch's generated prototypes come before the enum)
  RecKind kind = (RecKind)how;
  Screen before = screen;
  if (before == SCREEN_LISTENING || before == SCREEN_KEYBOARD) return;   // typing: a held button records nothing, so the text isn't lost
  Note *into = nullptr;   // Down in a note: append to it
  if (kind == REC_DOWN && before == SCREEN_NOTE) {
    into = notesFind(uiOpenNoteId());
    if (into && into->archived) into = nullptr;   // an archived note isn't added to: a passing chat instead
  }
  if (kind == REC_DOWN && ((into && into->busy()) || (before == SCREEN_CHAT && chat.waiting()))) { uiSay("Still transcribing the last one"); return; }
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < 1200000) { uiNotice("Storage full"); return; }   // a 30 s recording needs about 1 MB
  if (before == SCREEN_EXPORT) { exportDone(true); screenGhostReset(); }
  recording = kind;
  uiGo(SCREEN_LISTENING);
  String path, id;
  int secs = captureRecord(keepRecording, uiListeningFull, path, id);
  okConsume();
  lastActivity = millis();   // a recording counts as a touch: the sleep timer starts after it
  if (!secs) {   // say why for a moment, then back where it was
    Serial.printf("capture: %s\n", captureError());
    String why = captureError();
    if (why == "Couldn't save the recording") why = "Storage full";
    if (why.length()) uiNotice(why.c_str());
    uiGo(before == SCREEN_EXPORT ? SCREEN_SETTINGS : before);
    return;
  }
  if (kind == REC_DOWN && into) {
    into = notesFind(uiOpenNoteId());   // the list may have moved while recording
    if (into) {
      int at = into->text.length() ? into->text.length() + 2 : 0;
      into->audio = path; into->secs = secs; into->modified = time(nullptr);
      if (!notesSave()) { into->audio = ""; into->secs = 0; uiNotice(NOTES_SAVE_FAIL); LittleFS.remove(path); uiGo(before == SCREEN_EXPORT ? SCREEN_SETTINGS : before); return; }
      uiOpenNote(into->id, at);   // the page where the addition starts
      return;
    }
  }
  if (kind == REC_DOWN) {
    if (before != SCREEN_CHAT) chatClear();
    chatRecorded(path, secs);
    uiOpenChat(before);
    return;
  }
  chatClear();   // a new note leaves the passing chat behind
  Note *n = notesAdd("", path, secs, id);   // the file is named after the id
  if (!n) { uiNotice(NOTES_SAVE_FAIL); LittleFS.remove(path); uiGo(before == SCREEN_EXPORT ? SCREEN_SETTINGS : before); return; }
  if (kind == REC_CHAT_NOTE) { n->asked = true; if (!notesSave()) n->asked = false; }
  uiOpenNote(n->id, 0);
}

// Settings -> Update: a check, or an install, run on the loop (Update writes flash: not a PSRAM-stack task).
static bool updCheckAsk, updInstallAsk, updBusyInstalling;
bool updateQueued() { return updCheckAsk || updInstallAsk; }
bool updateInstalling() { return updBusyInstalling; }
void updateRequest(bool install) { if (install) updInstallAsk = true; else updCheckAsk = true; uiRender(false); }
static void updateTick() {
  if (updCheckAsk || updInstallAsk) {
    bool install = updInstallAsk;
    updCheckAsk = updInstallAsk = false;
    if (!netUp()) { Serial.println("update: no wifi"); updNoWifi(); uiRender(false); return; }
    if (install) { updBusyInstalling = true; uiRender(false); updInstall(); updBusyInstalling = false; }   // "Installing" stays on screen; a success restarts
    else updCheck();
    uiRender(false);
  }
}

// Seconds to just after the next local midnight, when the timer wakes the board to change the
// date on the lock screen; 0 while the time is unknown.
static uint32_t secsToMidnight() {
  time_t t = time(nullptr);
  if (!clockKnown()) return 0;
  struct tm tm; localtime_r(&t, &tm);
  tm.tm_mday += 1; tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 30; tm.tm_isdst = -1;
  time_t next = mktime(&tm);
  return next > t ? next - t : 60;
}

// Asleep, everything is off: the lock screen stays on the e-paper with a padlock. OK wakes it
// (to the lock screen, without the padlock; another OK unlocks where it was), and so does
// midnight, for a moment, to change the date.
static void goToSleep() {
  updMarkGood("sleep");
  uiSleeping(true);
  uiLock();   // the passing chat, if open, is left behind
  uiRender(true);
  uiSaveForSleep();
  screenSleep();
  deepSleep(secsToMidnight());
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
    if (!strncmp(VERSION, "dev", 3)) Serial.printf("nettest: GET auth/key -> %d in %lu ms, %s %s\n", code, (unsigned long)(millis() - t), body.substring(0, 160).c_str(), err);
    else Serial.printf("nettest: GET auth/key -> %d in %lu ms %s\n", code, (unsigned long)(millis() - t), err);
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
  else if (!strcmp(line, "stats")) {
    logHeap("stats");
    Serial.printf("stats: refreshes full %d partial %d, uptime %lu s, screen %d, notes %d, pending %d\n", refreshCount(true), refreshCount(false), millis() / 1000, screen, notesCount(false), notesPending());
    Serial.printf("stats: key %s, transcription %s, assistant %s, panel %s, battery %d mV, free RAM %u KB, PSRAM %u KB, spent %s in all\n", sttConfigured() ? "set" : "not set",
                  cfg::str("stt.model").c_str(), cfg::str("ask.model").c_str(), panelName, now.millivolts, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), costText(costTotal()).c_str());
  }
  else if (!strcmp(line, "sleep")) goToSleep();
  else if (sscanf(line, "time %d-%d-%d %d:%d", &Y, &M, &D, &h, &m) == 5) {
    struct tm t = {}; t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = m;
    time_t secs = mktime(&t); struct timeval tv = {secs, 0}; settimeofday(&tv, nullptr);
    rtcFromSystem();
    Serial.println("time: set (local)");
    uiRender(false);
  } else if (!strncmp(VERSION, "dev", 3) && !strncmp(line, "note add ", 9)) { notesAdd(line + 9, "", 0); uiRender(false); Serial.println("note: added"); }
  else if (!strncmp(VERSION, "dev", 3) && !strcmp(line, "notes")) { for (auto &n : notesAll()) Serial.printf("%s %lu %s%s%s %s\n", n.id.c_str(), (unsigned long)n.created, n.pinned ? "P" : "-", n.archived ? "A" : "-", n.audio.length() ? "W" : "-", n.audio.length() ? n.audio.c_str() : n.text.c_str()); }
  else if (!strncmp(VERSION, "dev", 3) && !strcmp(line, "wipe")) { for (auto &n : notesAll()) if (n.audio.length()) LittleFS.remove(n.audio); notesAll().clear(); notesSave(); uiRender(false); Serial.println("notes: wiped"); }
  else if (sscanf(line, "tap %d %d", &x, &y) == 2) uiTap(x, y);
  else if (!strcmp(line, "ok")) uiOk();
  else if (sscanf(line, "move %d", &x) == 1) uiMove(x);
  else if (!strcmp(line, "lock")) uiGo(SCREEN_LOCK);
  else if (!strncmp(VERSION, "dev", 3) && !strncmp(line, "wifi add ", 9)) {   // wifi add <ssid> <password>  (typed by the owner in the terminal)
    char *sp = strchr(line + 9, ' ');
    if (!sp) { Serial.println("wifi add <ssid> <password>"); return; }
    *sp = 0;
    std::vector<WifiCred> nets = cfg::wifiNetworks();
    nets.push_back({String(line + 9), String(sp + 1)});
    String err; cfg::setWifi(nets, err);
    Serial.printf("wifi: %s\n", err.length() ? err.c_str() : "saved");
  } else if (!strcmp(line, "wifi")) { Serial.printf("wifi: %s %s %d dBm, %u networks saved, key %s, pending %d\n", netUp() ? "up" : "down", netAddress().c_str(), netSignal(), (unsigned)cfg::wifiNetworks().size(), sttConfigured() ? "set" : "none", notesPending()); }
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

// A board fresh from the installer has an empty notes partition (all 0xFF): that is formatted
// without asking. Anything else that fails to mount may hold notes, so the screen asks first.
static bool fsNeverMade() {
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!p) return false;
  static uint8_t b[256];
  for (size_t off = 0; off < 8192; off += sizeof b) {   // LittleFS's two superblocks
    if (esp_partition_read(p, off, b, sizeof b) != ESP_OK) return false;
    for (uint8_t c : b) if (c != 0xFF) return false;
  }
  return true;
}
static bool fsFirstStart() {
  if (!fsNeverMade()) return false;
  Serial.println("fs: a new board: making the empty notebook");
  return LittleFS.begin(true);
}

void setup() {
  boardBegin();
  Serial.begin(115200);
  delay(50);
  Serial.printf("\nbrainos: %s starting\n", VERSION);
  updBegin();
  cfg::begin();
  setenv("TZ", cfg::str("device.timezone").c_str(), 1);
  tzset();
  if (!screenBegin()) Serial.println("screen: FAILED");
  else Serial.printf("screen: ok (%s)\n", panelName);
  screenRotation(PORTRAIT);
  sensorsBegin();
  readSensors();
  rtcBegin();
  rtcToSystem();
  if (!LittleFS.begin(false) && !fsFirstStart()) {
    Serial.println("fs: mount failed, nothing formatted");
    if (canvas) {
      canvas->fillScreen(PAPER);
      canvas->setTextColor(INK);
      textCentre(W / 2, 160, F_B12, "Notes couldn't be read");
      textCentre(W / 2, 220, F_S12, "Nothing was deleted.");
      textCentre(W / 2, 320, F_S12, "OK: try again");
      textCentre(W / 2, 370, F_S12, "Hold OK: erase all notes");
      show(true);
    }
    uint32_t holdAt = 0;
    bool holding = false;
    for (;;) {
      serialPoll();
      if (okDown()) {
        if (!holding) { holding = true; holdAt = millis(); }
        else if (millis() - holdAt > 2000) {
          if (LittleFS.begin(true)) { Serial.println("fs: formatted after a held OK"); break; }
          holding = false;
        }
      } else {
        if (holding && millis() - holdAt < 2000) {
          holding = false;
          if (LittleFS.begin(false)) break;
        }
        holding = false;
      }
      delay(10);
    }
  }
  notesBegin();
  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  if (why == ESP_SLEEP_WAKEUP_TIMER) {   // midnight: the new date, and back to sleep (an early wake just sleeps on)
    Serial.println("brainos: the midnight wake");
    uiRestoreAfterSleep();
    screen = SCREEN_LOCK;
    goToSleep();
  }
  costBegin();
  if (!touchBegin()) Serial.println("touch: FAILED");
  netBegin();
  sttBegin();
  webBegin();
  if (why == ESP_SLEEP_WAKEUP_EXT1 && uiRestoreAfterSleep()) uiRender(false);  // the lock screen stayed; unlocking goes back where it was
  else { screen = SCREEN_LOCK; uiRender(true); }
  lastActivity = millis();
  logHeap("ready");
  Serial.println("brainos: ready");
}

void loop() {
  static uint32_t lastMinute;
  TouchEvent te = touchPoll();
  if (te.kind == TOUCH_TAP) { Serial.printf("touch: tap %d, %d\n", te.x, te.y); uiTap(te.x, te.y); }
  else if (te.kind == TOUCH_SWIPE) uiSwipe(te.x, te.y, te.dx, te.dy);

  ButtonPoll bp = buttonsPoll();
  switch (bp.event) {
    case BTN_OK: uiOk(); break;
    case BTN_OK_HOLD: record(REC_NOTE); break;
    case BTN_UP_HOLD: record(REC_CHAT_NOTE); break;
    case BTN_DOWN_HOLD: record(REC_DOWN); break;
    case BTN_MOVE: uiMove(bp.move); break;
    case BTN_BOTH: uiLock(); break;
    case BTN_BOTH_LONG: goToSleep(); break;
    default: break;
  }

  if (redrawDue()) uiRender(false);
  serialPoll();
  netTick();
  uiTick();
  sttTick();
  webTick();
  exportTick();
  updateTick();
  if (updPendingVerify() && (netUp() || millis() > 60000)) updMarkGood(netUp() ? "online" : "60 s up");

  static long sleepMin = -1;
  if (sleepMin < 0 || millis() - lastMinute > 60000) {  // the battery, the date, the setting: once a minute
    lastMinute = millis();
    readSensors();
    uiMinute();
    sleepMin = cfg::num("sleep.min");
  }
  // On battery, asleep a few minutes after the last touch or button; a request in flight finishes first.
  uint32_t idle = (uint32_t)sleepMin * 60000UL;
  if (screen == SCREEN_NOTE || screen == SCREEN_CHAT || screen == SCREEN_KEYBOARD) idle *= 3;
  if (!now.usb && millis() - lastActivity > idle && !sttRunning()) goToSleep();
  delay(10);
}
