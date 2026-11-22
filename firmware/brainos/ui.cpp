#include "ui.h"
#include <time.h>
#include <algorithm>
#include <vector>
#include "board.h"
#include "config.h"
#include "export.h"
#include "keyboard.h"
#include "net.h"
#include "notes.h"
#include "ota.h"
#include "provjson.h"
#include "rtc.h"
#include "screen.h"
#include "stt.h"
#include "touch.h"
extern const char *const VERSION;

Screen screen = SCREEN_LOCK;
static const int BAR = UI_BAR;
static int page;                    // the list page shown
static String openId;               // the note on SCREEN_NOTE
static bool deleteArmed;            // Delete tapped once
static bool deleteAllArmed;
static int savedSecs;
static Keyboard kb;
bool updateQueued();       // brainos.ino: a check or install asked for, not started yet
bool updateInstalling();   // brainos.ino: the install is running on the loop (the screen cannot change)
static int kbPurpose;              // 1 edit the open note, 2 a Wi-Fi password
static Screen kbBack;
static std::vector<NetSeen> seen;  // the last scan
static String joinSsid, wifiMsg;
static int forgetArmed = -1;       // index of the saved network whose Forget was tapped
bool uiKeyboardShown() { return screen == SCREEN_KEYBOARD; }
static Screen lockFrom = SCREEN_NOTES;   // where a lock came from: unlocking goes back there
// Kept across deep sleep: the board wakes up where it was.
RTC_DATA_ATTR static int rtcScreen = -1;
RTC_DATA_ATTR static char rtcOpenId[12];
RTC_DATA_ATTR static int rtcPage, rtcLockFrom;

// ---------------------------------------------------------------- icons, drawn in code

static void iconBack(int x, int y) {        // an arrow pointing left
  canvas->fillRect(x + 4, y + 11, 18, 3, INK);
  for (int i = 0; i < 9; i++) { canvas->fillRect(x + 4 + i, y + 12 - i - 1, 3, 2, INK); canvas->fillRect(x + 4 + i, y + 12 + i, 3, 2, INK); }
}
static void iconPin(int x, int y) {         // 16 x 16: a push pin
  canvas->fillRect(x + 4, y + 1, 8, 5, INK);
  canvas->fillRect(x + 2, y + 6, 12, 3, INK);
  canvas->fillRect(x + 7, y + 9, 2, 7, INK);
}
static void iconX(int x, int y) {           // 16 x 16: a cross, for Forget
  for (int i = 0; i < 16; i++) { canvas->fillRect(x + i, y + i, 2, 2, INK); canvas->fillRect(x + 15 - i, y + i, 2, 2, INK); }
}
static void iconMic(int cx, int cy) {       // big, for the listening screen
  canvas->fillRoundRect(cx - 18, cy - 44, 36, 60, 18, INK);
  canvas->drawRoundRect(cx - 30, cy - 20, 60, 50, 24, INK);
  canvas->drawRoundRect(cx - 31, cy - 21, 62, 52, 25, INK);
  canvas->fillRect(cx - 24, cy - 24, 48, 8, PAPER);
  canvas->fillRect(cx - 2, cy + 30, 4, 14, INK);
  canvas->fillRect(cx - 14, cy + 44, 28, 4, INK);
}

// ---------------------------------------------------------------- text

// Word-wraps s into lines no wider than width, at most maxLines (the last one gets an ellipsis).
static std::vector<String> wrapLines(const GFXfont *font, const String &s, int width, int maxLines) {
  std::vector<String> out;
  String line, word;
  auto flush = [&]() { out.push_back(line); line = ""; };
  for (size_t i = 0; i <= s.length(); i++) {
    char c = i < s.length() ? s[i] : ' ';
    if (c == ' ' || c == '\n') {
      if (word.length()) {
        String tryLine = line.length() ? line + " " + word : word;
        if (textW(font, tryLine.c_str()) <= width) line = tryLine;
        else { if (line.length()) flush(); line = word; }
        word = "";
      }
      if (c == '\n') flush();
    } else word += c;
    if ((int)out.size() >= maxLines) break;
  }
  if (line.length() && (int)out.size() < maxLines) out.push_back(line);
  if ((int)out.size() > maxLines) out.resize(maxLines);
  if ((int)out.size() == maxLines && out.size()) {
    String &last = out.back();
    size_t used = 0; for (auto &l : out) used += l.length() + 1;
    if (used < s.length() + 1) { while (last.length() && textW(font, (last + "…").c_str()) > width) last.remove(last.length() - 1); last += "…"; }
  }
  return out;
}

static String timeOf(uint32_t t, bool withDate) {
  time_t secs = t; struct tm tm; localtime_r(&secs, &tm);
  char b[32];
  strftime(b, sizeof(b), withDate ? "%b %d, %H:%M" : "%H:%M", &tm);
  return String(b);
}

static String dayLabel(uint32_t t) {
  time_t secs = t, nowS = time(nullptr);
  struct tm a, b; localtime_r(&secs, &a); localtime_r(&nowS, &b);
  int da = a.tm_year * 366 + a.tm_yday, db = b.tm_year * 366 + b.tm_yday;
  if (da == db) return "Today";
  if (da == db - 1) return "Yesterday";
  static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  return String(MONTHS[a.tm_mon]) + " " + a.tm_mday + (a.tm_year != b.tm_year ? String(", ") + (a.tm_year + 1900) : "");
}

void uiTitle(const char *title) { textCentre(W / 2, 38, F_B12, title); }
static void topBar(const char *title, bool back) {
  if (back) iconBack(20, 16);
  uiTitle(title);
}
static const int FOOT_Y = UI_FOOT_Y;
static bool inFooter(int y) { return y >= FOOT_Y && y < FOOT_Y + UI_BTN; }
void uiFooter(const char *left, bool leftFilled, const char *right, bool rightFilled) {
  uiButton(UI_MARGIN, FOOT_Y, UI_HALF, UI_BTN, left, leftFilled);
  uiButton(UI_MARGIN + UI_HALF + UI_GAP, FOOT_Y, UI_HALF, UI_BTN, right, rightFilled);
}
int uiFooterHit(int x, int y) { return !inFooter(y) ? -1 : x < W / 2 ? 0 : 1; }
void uiButton(int x, int y, int w, int h, const char *label, bool filled) {
  if (filled) { canvas->fillRoundRect(x, y, w, h, 8, INK); canvas->setTextColor(PAPER); }
  else canvas->drawRoundRect(x, y, w, h, 8, INK);
  textCentre(x + w / 2, y + h / 2 + 6, F_B12, label);
  canvas->setTextColor(INK);
}
static void button(int x, int y, int w, int h, const char *label, bool filled) { uiButton(x, y, w, h, label, filled); }

static String waitingOr(const Note &n) {   // a recording still to be transcribed, or the text
  return n.audio.length() ? String("Recording, ") + n.secs + " s, waiting for Wi-Fi" : n.text;
}

// ---------------------------------------------------------------- the list (Notes and Archive)

struct Row { int noteIndex; int y, h; String day; };  // noteIndex -1: a day header
static std::vector<Row> rows;
static const int ROW_H = 66, HEAD_H = 32, LIST_TOP = BAR + 8, LIST_BOTTOM_PAD = 76;   // nine notes and two day headers fill the page down to the buttons

static std::vector<int> listed(bool archived) {
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) if (all[i].archived == archived) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return all[a].created > all[b].created; });   // newest first; a pinned note keeps its place and shows the pin
  return idx;
}


static int listPages(bool archived) {
  rows.clear();
  auto idx = listed(archived);
  auto &all = notesAll();
  String lastDay;
  int y = LIST_TOP, pageH = H - LIST_TOP - LIST_BOTTOM_PAD, pages = 1;
  for (int i : idx) {
    String day = dayLabel(all[i].created);
    int need = (day != lastDay ? HEAD_H : 0) + ROW_H;
    if (y - LIST_TOP + need > pageH * pages) { pages++; y = LIST_TOP + pageH * (pages - 1); lastDay = ""; }
    if (day != lastDay) { rows.push_back({-1, y, HEAD_H, day}); y += HEAD_H; lastDay = day; }
    rows.push_back({i, y, ROW_H, ""});
    y += ROW_H;
  }
  return pages;
}

static void drawList(bool archived) {
  auto &all = notesAll();
  int pages = listPages(archived), pageH = H - LIST_TOP - LIST_BOTTOM_PAD;
  if (page >= pages) page = pages - 1;
  if (page < 0) page = 0;
  int shift = page * pageH;
  bool any = false;
  const Row *last = nullptr;   // the last row on this page gets no line under it: the end looks the same whatever the headers
  for (auto &r : rows) if (r.y - shift >= LIST_TOP && r.y - shift + r.h <= LIST_TOP + pageH) last = &r;
  for (auto &r : rows) {
    int y = r.y - shift;
    if (y < LIST_TOP || y + r.h > LIST_TOP + pageH) continue;
    any = true;
    if (r.noteIndex < 0) {
      text(24, y + 22, F_B9, r.day.c_str());
      canvas->drawFastHLine(24, y + HEAD_H - 1, W - 48, INK);
      continue;
    }
    const Note &n = all[r.noteIndex];
    String body = waitingOr(n);
    auto lines = wrapLines(F_S12, body, W - 48 - 64 - (n.pinned ? 22 : 0), 2);
    for (size_t i = 0; i < lines.size(); i++) text(24, y + 25 + i * 24, F_S12, lines[i].c_str());
    textRight(W - 24, y + 23, F_S9, timeOf(n.created, false).c_str());
    if (n.pinned) iconPin(W - 44, y + 31);
    if (&r != last) canvas->drawFastHLine(24, y + r.h - 1, W - 48, INK);
  }
  if (!any) textCentre(W / 2, H / 2, F_S12, archived ? "The archive is empty" : "No notes yet");
  if (pages > 1) { String p = String(page + 1) + " / " + pages; textRight(W - 24, 38, F_S9, p.c_str()); }
}

static int rowAt(int y) {  // the note index under y on the current page, or -1
  int pageH = H - LIST_TOP - LIST_BOTTOM_PAD, shift = page * pageH;
  for (auto &r : rows) {
    int ry = r.y - shift;
    if (r.noteIndex >= 0 && y >= ry && y < ry + r.h && ry >= LIST_TOP && ry + r.h <= LIST_TOP + pageH) return r.noteIndex;
  }
  return -1;
}

// ---------------------------------------------------------------- screens

struct LockBox { int y, h; String id; };
static std::vector<LockBox> lockBoxes;   // the pinned notes on the lock screen, for taps
static void drawLock() {
  char buf[64];
  if (now.battery) snprintf(buf, sizeof(buf), "%d%%", now.percent); else strcpy(buf, "--%");
  textCentre(W / 2, 32 + 17, F_S12, buf);   // 32 px from the top edge of the text
  time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  bool known = t > 1700000000;
  if (known) strftime(buf, sizeof(buf), "%H:%M", &tm); else strcpy(buf, "--:--");
  canvas->setTextSize(2);
  text((W - textW(F_B24, buf) * 2) / 2, 190, F_B24, buf);
  canvas->setTextSize(1);
  static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (known) snprintf(buf, sizeof(buf), "%s, %s %d", DAYS[tm.tm_wday], MONTHS[tm.tm_mon], tm.tm_mday); else strcpy(buf, "Time not set");
  textCentre(W / 2, 240, F_S12, buf);
  lockBoxes.clear();
  {  // the pinned notes, newest first, stacked while they fit; a tap on one opens it
    auto idx = listed(false);
    int top = 300, bottom = H - 110;
    for (int i : idx) {
      const Note &p = notesAll()[i];
      if (!p.pinned) continue;
      int maxLines = (bottom - top - 30) / 26;
      if (maxLines < 1) break;
      auto lines = wrapLines(F_S12, waitingOr(p), W - 48 - 56, min(maxLines, 4));
      int height = 30 + 26 * max<int>(1, lines.size());
      canvas->drawRoundRect(24, top, W - 48, height, 8, INK);
      iconPin(W - 48, top + 10);
      for (size_t k = 0; k < lines.size(); k++) text(40, top + 34 + k * 26, F_S12, lines[k].c_str());
      lockBoxes.push_back({top, height, p.id});
      top += height + 12;
    }
  }
  int count = notesCount(false);
  snprintf(buf, sizeof(buf), "%d note%s", count, count == 1 ? "" : "s");
  textCentre(W / 2, H - 32, F_S12, buf);   // 32 px from the bottom
}

static void drawNotes() {
  topBar("Notes", false);
  drawList(false);
  uiFooter("New note", false, "Settings", false);
}

static void drawNote() {
  Note *n = notesFind(openId);
  topBar("Note", true);
  if (!n) { textCentre(W / 2, H / 2, F_S12, "Gone"); return; }
  text(24, BAR + 28, F_S9, timeOf(n->created, true).c_str());
  String body = waitingOr(*n);
  int bw = UI_HALF, by = n->archived ? FOOT_Y : FOOT_Y - UI_BTN - 12;
  auto lines = wrapLines(F_S12, body, W - 48, (by - 16 - (BAR + 60)) / 26);   // the text stops above the buttons
  for (size_t i = 0; i < lines.size(); i++) text(24, BAR + 60 + i * 26, F_S12, lines[i].c_str());
  if (n->archived) {   // an archived note is neither pinned nor edited: Restore | Delete only
    uiFooter("Restore", false, deleteArmed ? "Yes, delete" : "Delete", deleteArmed);
    return;
  }
  bool waiting = n->audio.length() > 0;   // a recording still to be transcribed is neither edited nor archived
  button(24, by, bw, 52, n->pinned ? "Unpin" : "Pin", n->pinned);
  if (!waiting) button(24 + bw + 16, by, bw, 52, "Edit", false);
  if (!waiting) button(24, by + 64, bw, 52, "Archive", false);
  button(24 + bw + 16, by + 64, bw, 52, deleteArmed ? "Yes, delete" : "Delete", deleteArmed);
}

// ---------------------------------------------------------------- settings

static const int SROW = 72;
static void settingsRow(int i, const char *label, const String &value) {
  int y = BAR + 12 + i * SROW;
  text(24, y + 44, F_S12, label);
  if (value.length()) textRight(W - 24, y + 44, F_S12, fitted(F_S12, value, W - 48 - textW(F_S12, label) - 16).c_str());
  canvas->drawFastHLine(24, y + SROW - 1, W - 48, INK);
}
static int settingsRowAt(int y) { int i = (y - BAR - 12) / SROW; return y < BAR + 12 ? -1 : i; }

static void drawSettings() {
  topBar("Settings", true);
  settingsRow(0, "Wi-Fi", netUp() ? netName() : String(cfg::wifiNetworks().size()) + " saved");
  settingsRow(1, "Export", "");
  settingsRow(2, "Archive", notesCount(true) ? String(notesCount(true)) : String(""));
  settingsRow(3, "About", "");
  textCentre(W / 2, H - 32, F_S12, "More at brain.local");
}

static void drawWifi() {
  topBar("Wi-Fi", true);
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  for (size_t i = 0; i < nets.size(); i++) {
    bool on = netUp() && netName() == nets[i].ssid;
    settingsRow(i, fitted(F_S12, nets[i].ssid, W - 48 - 130).c_str(), (int)i == forgetArmed ? "" : on ? "connected" : "");
    if ((int)i == forgetArmed) iconX(W - 40, BAR + 12 + i * SROW + 28);
  }
  if (nets.empty()) text(24, BAR + 56, F_S12, "No saved networks");
  button(24, FOOT_Y, W - 48, 52, "Add a network", false);
  if (wifiMsg.length()) textCentre(W / 2, FOOT_Y - 24, F_S9, fitted(F_S9, wifiMsg, W - 48).c_str());
}

static void iconSignal(int x, int y, int bars) {   // four bars, 4 px wide, rising to 16 px; the weak ones hollow
  for (int k = 0; k < 4; k++) {
    int h = 4 + 4 * k;
    if (k < bars) canvas->fillRect(x + k * 6, y + 16 - h, 4, h, INK);
    else canvas->drawRect(x + k * 6, y + 16 - h, 4, h, INK);
  }
}
static void iconLock(int x, int y) {   // 12 x 16: a small padlock
  canvas->drawRoundRect(x + 2, y, 8, 9, 3, INK);
  canvas->fillRect(x, y + 7, 12, 9, INK);
}
static void drawWifiScan() {
  topBar("Networks", true);
  if (seen.empty()) textCentre(W / 2, H / 2, F_S12, "None found");
  for (size_t i = 0; i < seen.size() && i < 8; i++) {
    int bars = seen[i].rssi > -55 ? 4 : seen[i].rssi > -67 ? 3 : seen[i].rssi > -78 ? 2 : 1;
    settingsRow(i, fitted(F_S12, seen[i].ssid, W - 48 - 70).c_str(), "");
    int y = BAR + 12 + i * SROW + 28;
    iconSignal(W - 24 - 22, y, bars);
    if (seen[i].secure) iconLock(W - 24 - 22 - 24, y);
  }
  button(24, FOOT_Y, W - 48, 52, "Scan again", false);
}

static void drawAbout() {
  topBar("About", true);
  String lines[] = {
    cfg::str("device.name"), String("BrainOS ") + VERSION,
    netUp() ? "Wi-Fi: " + netName() + ", " + netAddress() : "Wi-Fi: off",
    netUp() ? "Page: http://brain.local" : "",
    "Battery: " + String(now.percent) + "% (" + now.millivolts + " mV)" + (now.usb ? ", USB" : ""),
    "Notes: " + String(notesCount(false)) + ", archived " + notesCount(true) + ", waiting " + notesPending(),
    String("Key: ") + (sttConfigured() ? "set" : "not set") + ", model " + cfg::str("stt.model"),
    "Panel: " + String(panelName),
    "Free RAM: " + String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB, PSRAM " + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) + " KB",
  };
  int y = BAR + 40;
  for (auto &l : lines) { if (!l.length()) continue; auto w = wrapLines(F_S9, l, W - 48, 2); for (auto &t : w) { text(24, y, F_S9, t.c_str()); y += 24; } y += 6; }
  UpdInfo u = updInfo();
  if (u.status.length() && u.checkedMs) { auto w = wrapLines(F_S9, u.status, W - 48, 3); y += 10; for (auto &t : w) { text(24, y, F_S9, t.c_str()); y += 24; } }
  if (u.state == UPD_INSTALLING || updateInstalling()) button(24, FOOT_Y, W - 48, 52, "Installing, don't switch off", true);
  else if (u.available) {
    uiFooter(u.state == UPD_CHECKING || updateQueued() ? "Checking…" : "Check again", false, ("Install " + u.latest).c_str(), true);
  } else button(24, FOOT_Y, W - 48, 52, u.state == UPD_CHECKING || updateQueued() ? "Checking…" : "Check for update", false);
}

static void drawKeyboard() { keyboardDraw(kb); }

static void startWifiScan() {
  canvas->fillScreen(PAPER); topBar("Networks", false); textCentre(W / 2, H / 2, F_S12, "Scanning…"); show(false);
  String why;
  netScan(seen, why);
  uiGo(SCREEN_WIFI_SCAN);
}

static void joinNetwork(const String &ssid, const String &pass) {
  canvas->fillScreen(PAPER); topBar("Wi-Fi", false);
  textCentre(W / 2, H / 2, F_S12, fitted(F_S12, "Connecting to " + ssid + "…", W - 48).c_str()); show(false);
  String why;
  bool ok = netJoin(ssid, pass, why);
  if (ok) {
    std::vector<WifiCred> nets = cfg::wifiNetworks();
    for (size_t i = 0; i < nets.size(); i++) if (nets[i].ssid == ssid) nets.erase(nets.begin() + i--);
    nets.insert(nets.begin(), {ssid, pass});
    String err; cfg::setWifi(nets, err);
    wifiMsg = err.length() ? "Connected, but not saved: " + err : "Connected and saved";
    netTouch();
    uiGo(SCREEN_WIFI);
  } else {
    kb.note = why;
    uiGo(SCREEN_KEYBOARD);
  }
}

static void drawArchive() {
  topBar("Archive", true);
  drawList(true);
  if (notesCount(true)) button(24, FOOT_Y, W - 48, 52, deleteAllArmed ? "Yes, delete all" : "Delete all", deleteAllArmed);
}

static bool listeningDone;
bool uiListeningDone() { if (listeningDone) return false; listeningDone = true; return true; }
static void drawListening() {
  iconMic(W / 2, H / 2 - 60);
  textCentre(W / 2, H / 2 + 60, F_B18, listeningDone ? "Saving…" : "Listening…");
}

static void drawSaved() {
  char buf[48];
  snprintf(buf, sizeof(buf), "Saved, %d s", savedSecs);
  textCentre(W / 2, H / 2, F_B18, buf);
}

void uiRender(bool full) {
  canvas->fillScreen(PAPER);
  canvas->setTextColor(INK);
  switch (screen) {
    case SCREEN_LOCK: drawLock(); break;
    case SCREEN_NOTES: drawNotes(); break;
    case SCREEN_NOTE: drawNote(); break;
    case SCREEN_ARCHIVE: drawArchive(); break;
    case SCREEN_EXPORT: topBar("Export", true); exportDraw(); break;
    case SCREEN_SETTINGS: drawSettings(); break;
    case SCREEN_KEYBOARD: drawKeyboard(); break;
    case SCREEN_WIFI: drawWifi(); break;
    case SCREEN_WIFI_SCAN: drawWifiScan(); break;
    case SCREEN_ABOUT: drawAbout(); break;
    case SCREEN_LISTENING: drawListening(); break;
    case SCREEN_SAVED: drawSaved(); break;
  }
  show(full);
}

void uiGo(Screen s) {
  if (s != screen) { deleteArmed = deleteAllArmed = false; forgetArmed = -1; listeningDone = false; if (s == SCREEN_NOTES || s == SCREEN_ARCHIVE) page = 0; if (s != SCREEN_WIFI) wifiMsg = ""; }
  screen = s;
  touchQuick = s == SCREEN_KEYBOARD;
  screenGhostPause(s == SCREEN_EXPORT);
  uiRender(false);
}

void uiShowSaved(int secs) { savedSecs = secs; uiGo(SCREEN_SAVED); }

// ---------------------------------------------------------------- input

static void keyboardResult(KbResult r) {
  if (r == KB_CHANGED) redrawSoon();
  else if (r == KB_CANCEL) uiGo(kbBack);
  else if (r == KB_DONE) {
    if (kbPurpose == 1) { Note *n = notesFind(openId); if (n) { n->text = kb.text; n->modified = time(nullptr); notesSave(); } uiGo(SCREEN_NOTE); }
    else if (kbPurpose == 3) { String t = kb.text; t.trim(); if (t.length()) notesAdd(t, "", 0); uiGo(SCREEN_NOTES); }
    else if (kbPurpose == 2) { if (kb.text.length() < 8) { kb.note = "A password has at least 8 characters"; uiRender(false); } else joinNetwork(joinSsid, kb.text); }
  }
}

static bool inBar(int y) { return y < BAR; }
static bool leftIcon(int x, int y) { return inBar(y) && x < 80; }

void uiTap(int x, int y) {
  switch (screen) {
    case SCREEN_LOCK:
      for (auto &b : lockBoxes) if (y >= b.y && y < b.y + b.h) { openId = b.id; deleteArmed = false; uiGo(SCREEN_NOTE); return; }
      uiGo(SCREEN_NOTES);   // a tap opens the list; the OK button goes back to where the lock came from
      break;
    case SCREEN_SAVED: uiGo(SCREEN_NOTES); break;
    case SCREEN_NOTES: {
      if (inFooter(y)) {
        if (x < W / 2) { keyboardBegin(kb, "", "New note", "Type here", 1000); kbPurpose = 3; kbBack = SCREEN_NOTES; uiGo(SCREEN_KEYBOARD); }
        else uiGo(SCREEN_SETTINGS);
        break;
      }
      int i = rowAt(y);
      if (i >= 0) { openId = notesAll()[i].id; deleteArmed = false; uiGo(SCREEN_NOTE); }
      break;
    }
    case SCREEN_NOTE: {
      Note *n = notesFind(openId);
      Screen home = n && n->archived ? SCREEN_ARCHIVE : SCREEN_NOTES;   // back and Delete return to the list the note came from
      if (leftIcon(x, y) || !n) { uiGo(home); break; }
      int bw = UI_HALF, by = n->archived ? FOOT_Y : FOOT_Y - UI_BTN - 12;
      bool right = x >= 24 + bw + 16, row1 = !n->archived && y >= by && y < by + 52, row2 = n->archived ? inFooter(y) : y >= by + 64 && y < by + 116;
      bool waiting = n->audio.length() > 0;
      if (row1 && !right) { notesPin(*n); deleteArmed = false; uiRender(false); }
      else if (row1 && right && !waiting) { keyboardBegin(kb, n->text, "Edit note", "", 1000); kbPurpose = 1; kbBack = SCREEN_NOTE; uiGo(SCREEN_KEYBOARD); }
      else if (row2 && !right && !waiting) { n->archived = !n->archived; if (n->archived) n->pinned = false; else n->exported = false; n->modified = time(nullptr); notesSave(); uiGo(n->archived ? SCREEN_NOTES : SCREEN_ARCHIVE); }
      else if (row2 && right) {
        if (!deleteArmed) { deleteArmed = true; uiRender(false); }
        else { notesRemove(openId); deleteArmed = false; uiGo(home); }
      } else if (deleteArmed) { deleteArmed = false; uiRender(false); }
      break;
    }
    case SCREEN_ARCHIVE: {
      if (leftIcon(x, y)) { uiGo(SCREEN_SETTINGS); break; }
      if (inFooter(y) && notesCount(true)) {
        if (!deleteAllArmed) { deleteAllArmed = true; uiRender(false); }
        else {
          auto &all = notesAll();
          for (size_t i = all.size(); i-- > 0;) if (all[i].archived) notesRemove(all[i].id);
          deleteAllArmed = false;
          uiRender(false);
        }
        break;
      }
      int i = rowAt(y);
      if (i >= 0) { openId = notesAll()[i].id; deleteArmed = false; uiGo(SCREEN_NOTE); }
      else if (deleteAllArmed) { deleteAllArmed = false; uiRender(false); }
      break;
    }
    case SCREEN_EXPORT:
      if (leftIcon(x, y)) { exportDone(true); screenGhostReset(); uiGo(SCREEN_SETTINGS); }
      else if (inFooter(y) && exportParts()) { exportDone(false); screenGhostReset(); screen = SCREEN_NOTES; uiRender(true); }
      break;
    case SCREEN_SETTINGS: {
      if (leftIcon(x, y)) { uiGo(SCREEN_NOTES); break; }
      int i = settingsRowAt(y);
      if (i == 0) uiGo(SCREEN_WIFI);
      else if (i == 1) { exportStart(); uiGo(SCREEN_EXPORT); }
      else if (i == 2) uiGo(SCREEN_ARCHIVE);
      else if (i == 3) uiGo(SCREEN_ABOUT);
      break;
    }
    case SCREEN_WIFI: {
      if (leftIcon(x, y)) { uiGo(SCREEN_SETTINGS); break; }
      std::vector<WifiCred> nets = cfg::wifiNetworks();
      if (inFooter(y)) { startWifiScan(); break; }
      int i = settingsRowAt(y);
      if (i >= 0 && i < (int)nets.size()) {
        if (forgetArmed == i) { nets.erase(nets.begin() + i); String e; cfg::setWifi(nets, e); forgetArmed = -1; wifiMsg = "Forgotten"; }
        else forgetArmed = i;
        uiRender(false);
      } else if (forgetArmed >= 0) { forgetArmed = -1; uiRender(false); }
      break;
    }
    case SCREEN_WIFI_SCAN: {
      if (leftIcon(x, y)) { uiGo(SCREEN_WIFI); break; }
      if (inFooter(y)) { startWifiScan(); break; }
      int i = settingsRowAt(y);
      if (i >= 0 && i < (int)seen.size() && i < 8) {
        joinSsid = seen[i].ssid;
        if (!seen[i].secure) joinNetwork(joinSsid, "");
        else { keyboardBegin(kb, "", ("Password for " + joinSsid).c_str(), "", 64); keyboardSecret(kb); kbPurpose = 2; kbBack = SCREEN_WIFI_SCAN; uiGo(SCREEN_KEYBOARD); }
      }
      break;
    }
    case SCREEN_KEYBOARD: keyboardResult(keyboardTap(kb, x, y)); break;
    case SCREEN_ABOUT:
      if (leftIcon(x, y)) uiGo(SCREEN_SETTINGS);
      else if (inFooter(y) && !updateInstalling()) {
        extern void updateRequest(bool install);
        updateRequest(updInfo().available && uiFooterHit(x, y) == 1);
      }
      break;
    default: break;
  }
}

void uiSwipe(int x, int y, int dx, int dy) {
  if ((screen == SCREEN_NOTES || screen == SCREEN_ARCHIVE) && abs(dy) > abs(dx)) uiMove(dy < 0 ? 1 : -1);
}

void uiOk() {
  switch (screen) {
    case SCREEN_LOCK: uiGo(lockFrom); break;
    case SCREEN_EXPORT: exportDone(false); screenGhostReset(); screen = SCREEN_NOTES; uiRender(true); break;
    case SCREEN_KEYBOARD: keyboardResult(KB_DONE); break;   // OK is Done
    case SCREEN_LISTENING: break;
    default: uiLock(); break;   // one click locks
  }
}

void uiMove(int by) {
  if (screen == SCREEN_NOTE) {
    Note *n = notesFind(openId);
    auto idx = listed(n && n->archived);
    for (size_t k = 0; k < idx.size(); k++)
      if (notesAll()[idx[k]].id == openId) {
        int j = (int)k + by;
        if (j >= 0 && j < (int)idx.size()) { openId = notesAll()[idx[j]].id; deleteArmed = false; uiRender(false); }
        return;
      }
    return;
  }
  if (screen == SCREEN_NOTES || screen == SCREEN_ARCHIVE) {
    int pages = listPages(screen == SCREEN_ARCHIVE);
    int p = constrain(page + by, 0, pages - 1);
    if (p != page) { page = p; uiRender(false); }
  }
}

void uiLock() {
  if (screen == SCREEN_LOCK) return;
  if (screen == SCREEN_EXPORT) { exportDone(true); screenGhostReset(); }
  lockFrom = screen == SCREEN_LISTENING || screen == SCREEN_SAVED || screen == SCREEN_KEYBOARD ? SCREEN_NOTES
           : screen == SCREEN_EXPORT ? SCREEN_SETTINGS : screen == SCREEN_WIFI_SCAN ? SCREEN_WIFI : screen;
  uiGo(SCREEN_LOCK);
}

// Deep sleep keeps the picture; these keep the place.
void uiSaveForSleep() {
  rtcScreen = screen == SCREEN_LISTENING || screen == SCREEN_SAVED || screen == SCREEN_KEYBOARD ? SCREEN_NOTES : screen;
  strlcpy(rtcOpenId, openId.c_str(), sizeof rtcOpenId);
  rtcPage = page; rtcLockFrom = lockFrom;
}
bool uiRestoreAfterSleep() {
  if (rtcScreen < 0) return false;
  screen = (Screen)rtcScreen; openId = rtcOpenId; page = rtcPage; lockFrom = (Screen)rtcLockFrom;
  if (screen == SCREEN_EXPORT) screen = SCREEN_SETTINGS;
  if (screen == SCREEN_WIFI_SCAN) screen = SCREEN_WIFI;
  if (lockFrom == SCREEN_EXPORT) lockFrom = SCREEN_SETTINGS;
  if (lockFrom == SCREEN_WIFI_SCAN) lockFrom = SCREEN_WIFI;
  if (screen == SCREEN_NOTE && !notesFind(openId)) screen = SCREEN_NOTES;
  touchQuick = false; screenGhostPause(false);
  return true;
}

// Something changed the notes outside the UI (a transcription, the web page): redraw if they're on screen.
void uiNotesChanged() {
  if (screen == SCREEN_NOTES || screen == SCREEN_LOCK || screen == SCREEN_ARCHIVE || screen == SCREEN_NOTE) uiRender(false);
}
