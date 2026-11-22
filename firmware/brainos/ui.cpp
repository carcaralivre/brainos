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
static const int BAR = 56;          // the top bar with the icons and title
static int page;                    // the list page shown
static String openId;               // the note on SCREEN_NOTE
static bool deleteArmed;            // Delete tapped once
static bool deleteAllArmed;
static int savedSecs;
static Keyboard kb;
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

static void topBar(const char *title, bool back) {
  if (back) iconBack(20, 16);
  textCentre(W / 2, 38, F_B12, title);
}
static const int FOOT_Y = 800 - 76;   // the standard button row: 52 px tall, 24 px from the bottom and the sides
static bool inFooter(int y) { return y >= FOOT_Y && y < FOOT_Y + 52; }
static void button(int x, int y, int w, int h, const char *label, bool filled) {
  if (filled) { canvas->fillRoundRect(x, y, w, h, 8, INK); canvas->setTextColor(PAPER); }
  else canvas->drawRoundRect(x, y, w, h, 8, INK);
  textCentre(x + w / 2, y + h / 2 + 6, F_B12, label);
  canvas->setTextColor(INK);
}

// ---------------------------------------------------------------- the list (Notes and Archive)

struct Row { int noteIndex; int y, h; String day; };  // noteIndex -1: a day header
static std::vector<Row> rows;
static const int ROW_H = 68, HEAD_H = 36, LIST_TOP = BAR + 8, LIST_BOTTOM_PAD = 110;

static std::vector<int> listed(bool archived) {
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) if (all[i].archived == archived) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) {
    if (all[a].pinned != all[b].pinned) return all[a].pinned;   // the pinned ones first
    return all[a].created > all[b].created;
  });
  return idx;
}

// Pins a note; with three already pinned, the oldest pin is let go.
static void pinNote(Note &n) {
  if (n.pinned) { n.pinned = false; n.modified = time(nullptr); notesSave(); return; }
  auto &all = notesAll();
  for (;;) {
    int count = 0, oldest = -1;
    for (size_t i = 0; i < all.size(); i++) if (all[i].pinned) { count++; if (oldest < 0 || all[i].modified < all[oldest].modified) oldest = i; }
    if (count < 3) break;
    all[oldest].pinned = false;
  }
  n.pinned = true;
  n.modified = time(nullptr);
  notesSave();
}

static int listPages(bool archived) {
  rows.clear();
  auto idx = listed(archived);
  auto &all = notesAll();
  String lastDay;
  int y = LIST_TOP, pageH = H - LIST_TOP - LIST_BOTTOM_PAD, pages = 1;
  for (int i : idx) {
    String day = all[i].pinned ? "Pinned" : dayLabel(all[i].created);
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
  for (auto &r : rows) {
    int y = r.y - shift;
    if (y < LIST_TOP || y + r.h > LIST_TOP + pageH) continue;
    any = true;
    if (r.noteIndex < 0) {
      text(24, y + 24, F_B9, r.day.c_str());
      canvas->drawFastHLine(24, y + 31, W - 48, INK);
      continue;
    }
    const Note &n = all[r.noteIndex];
    String body = n.audio.length() ? String("Recording, ") + n.secs + " s. Waiting for Wi-Fi to transcribe." : n.text;
    auto lines = wrapLines(F_S12, body, W - 48 - 64 - (n.pinned ? 22 : 0), 2);
    for (size_t i = 0; i < lines.size(); i++) text(24, y + 26 + i * 24, F_S12, lines[i].c_str());
    textRight(W - 24, y + 24, F_S9, timeOf(n.created, false).c_str());
    if (n.pinned) iconPin(W - 44, y + 32);
    canvas->drawFastHLine(24, y + r.h - 1, W - 48, INK);
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

static void drawLock() {
  char buf[64];
  if (now.battery) snprintf(buf, sizeof(buf), "%d%%", now.percent); else strcpy(buf, "--%");
  textCentre(W / 2, 40, F_S12, buf);
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
  {  // the pinned notes, newest pin first, stacked while they fit
    auto idx = listed(false);
    int top = 300, bottom = H - 110;
    for (int i : idx) {
      const Note &p = notesAll()[i];
      if (!p.pinned) break;
      int maxLines = (bottom - top - 30) / 26;
      if (maxLines < 1) break;
      auto lines = wrapLines(F_S12, p.audio.length() ? "(recording, waiting)" : p.text, W - 48 - 56, min(maxLines, 4));
      int height = 30 + 26 * max<int>(1, lines.size());
      canvas->drawRect(24, top, W - 48, height, INK);
      iconPin(W - 48, top + 10);
      for (size_t k = 0; k < lines.size(); k++) text(40, top + 34 + k * 26, F_S12, lines[k].c_str());
      top += height + 12;
    }
  }
  int count = notesCount(false);
  snprintf(buf, sizeof(buf), "%d note%s", count, count == 1 ? "" : "s");
  textCentre(W / 2, H - 70, F_S12, buf);
}

static void drawNotes() {
  topBar("Notes", false);
  drawList(false);
  int bw = (W - 48 - 16) / 2;
  button(24, FOOT_Y, bw, 52, "New note", false);
  button(24 + bw + 16, FOOT_Y, bw, 52, "Settings", false);
}

static void drawNote() {
  Note *n = notesFind(openId);
  topBar("Note", true);
  if (!n) { textCentre(W / 2, H / 2, F_S12, "Gone"); return; }
  text(24, BAR + 28, F_S9, timeOf(n->created, true).c_str());
  String body = n->audio.length() ? String("Recording of ") + n->secs + " s, waiting for Wi-Fi to be transcribed." : n->text;
  auto lines = wrapLines(F_S12, body, W - 48, (H - BAR - 140) / 26);
  for (size_t i = 0; i < lines.size(); i++) text(24, BAR + 60 + i * 26, F_S12, lines[i].c_str());
  int bw = (W - 48 - 16) / 2, by = H - 76 - 64;
  button(24, by, bw, 52, n->pinned ? "Unpin" : "Pin", n->pinned);
  button(24 + bw + 16, by, bw, 52, "Edit", false);
  button(24, by + 64, bw, 52, n->archived ? "Restore" : "Archive", false);
  button(24 + bw + 16, by + 64, bw, 52, deleteArmed ? "Confirm" : "Delete", deleteArmed);
}

// ---------------------------------------------------------------- settings

static const int SROW = 72;
static void settingsRow(int i, const char *label, const String &value) {
  int y = BAR + 12 + i * SROW;
  text(24, y + 44, F_S12, label);
  if (value.length()) textRight(W - 24, y + 44, F_S12, value.c_str());
  canvas->drawFastHLine(24, y + SROW - 1, W - 48, INK);
}
static int settingsRowAt(int y) { int i = (y - BAR - 12) / SROW; return y < BAR + 12 ? -1 : i; }

static void drawSettings() {
  topBar("Settings", true);
  settingsRow(0, "Wi-Fi", netUp() ? netName() : String(cfg::wifiNetworks().size()) + " saved");
  settingsRow(1, "Export", "");
  settingsRow(2, "Archive", notesCount(true) ? String(notesCount(true)) : String(""));
  settingsRow(3, "Sound", soundOn ? "On" : "Off");
  settingsRow(4, "About", "");
  textCentre(W / 2, H - 60, F_S9, "More settings at http://brain.local");
}

static void drawWifi() {
  topBar("Wi-Fi", true);
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  for (size_t i = 0; i < nets.size(); i++) {
    bool on = netUp() && netName() == nets[i].ssid;
    settingsRow(i, fitted(F_S12, nets[i].ssid, W - 48 - 130).c_str(), (int)i == forgetArmed ? "Forget?" : on ? "connected" : "");
  }
  if (nets.empty()) text(24, BAR + 56, F_S12, "No saved networks");
  int by = BAR + 12 + nets.size() * SROW + 16;
  button(24, by, W - 48, 52, "Add a network", false);
  if (wifiMsg.length()) textCentre(W / 2, by + 52 + 36, F_S9, fitted(F_S9, wifiMsg, W - 48).c_str());
}

static void drawWifiScan() {
  topBar("Networks", true);
  if (seen.empty()) textCentre(W / 2, H / 2, F_S12, "None found");
  for (size_t i = 0; i < seen.size() && i < 9; i++) {
    String bars; int b = seen[i].rssi > -55 ? 4 : seen[i].rssi > -67 ? 3 : seen[i].rssi > -78 ? 2 : 1;
    for (int k = 0; k < 4; k++) bars += k < b ? "|" : ".";
    settingsRow(i, fitted(F_S12, seen[i].ssid, W - 48 - 90).c_str(), (seen[i].secure ? "* " : "") + bars);
  }
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
  button(24, FOOT_Y, W - 48, 52, "Check for update", false);
}

static void drawKeyboard() { keyboardDraw(kb, 8); }

static void drawUpdate() {
  topBar("Update", true);
  UpdInfo u = updInfo();
  text(24, BAR + 40, F_S9, (String("This board: ") + updVersion()).c_str());
  auto lines = wrapLines(F_S12, u.status, W - 48, 4);
  for (size_t i = 0; i < lines.size(); i++) text(24, BAR + 84 + i * 26, F_S12, lines[i].c_str());
  if (u.notes.length()) { auto n = wrapLines(F_S9, u.notes, W - 48, 8); for (size_t i = 0; i < n.size(); i++) text(24, BAR + 200 + i * 22, F_S9, n[i].c_str()); }
  if (u.state == UPD_INSTALLING) textCentre(W / 2, H - 60, F_B12, "Installing, don't switch off");
  else {
    button(24, H - 76, (W - 48 - 16) / 2, 52, "Check now", false);
    if (u.available) button(24 + (W - 48 - 16) / 2 + 16, H - 76, (W - 48 - 16) / 2, 52, "Install", true);
  }
}

static void startWifiScan() {
  canvas->fillScreen(PAPER); topBar("Networks", true); textCentre(W / 2, H / 2, F_S12, "Scanning…"); show(false);
  String why;
  netScan(seen, why);
  uiGo(SCREEN_WIFI_SCAN);
}

static void joinNetwork(const String &ssid, const String &pass) {
  canvas->fillScreen(PAPER); topBar("Wi-Fi", true);
  textCentre(W / 2, H / 2, F_S12, fitted(F_S12, "Connecting to " + ssid + "…", W - 48).c_str()); show(false);
  String why;
  bool ok = netJoin(ssid, pass, why);
  if (ok) {
    std::vector<WifiCred> nets = cfg::wifiNetworks();
    for (size_t i = 0; i < nets.size(); i++) if (nets[i].ssid == ssid) nets.erase(nets.begin() + i--);
    nets.insert(nets.begin(), {ssid, pass});
    String err; cfg::setWifiInPlace(nets, err);
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
  if (notesCount(true)) button(24, FOOT_Y, W - 48, 52, deleteAllArmed ? "Tap again to delete everything" : "Delete all", deleteAllArmed);
}

static void drawSoon(const char *title) {
  topBar(title, true);
  textCentre(W / 2, H / 2, F_S12, "Coming soon");
}

static void drawListening() {
  iconMic(W / 2, H / 2 - 60);
  textCentre(W / 2, H / 2 + 60, F_B18, "Listening…");
  textCentre(W / 2, H / 2 + 100, F_S12, "Release OK to stop");
}

static void drawSaved() {
  char buf[48];
  snprintf(buf, sizeof(buf), "Saved, %d s", savedSecs);
  textCentre(W / 2, H / 2, F_B18, buf);
  textCentre(W / 2, H / 2 + 40, F_S12, notesPending() > 1 ? "Will be transcribed with Wi-Fi" : "Transcribing when Wi-Fi is on");
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
    case SCREEN_UPDATE: drawUpdate(); break;
    case SCREEN_LISTENING: drawListening(); break;
    case SCREEN_SAVED: drawSaved(); break;
  }
  show(full);
}

void uiGo(Screen s) {
  if (s != screen) { deleteArmed = deleteAllArmed = false; forgetArmed = -1; if (s == SCREEN_NOTES || s == SCREEN_ARCHIVE) page = 0; if (s != SCREEN_WIFI) wifiMsg = ""; }
  screen = s;
  touchQuick = s == SCREEN_KEYBOARD;
  screenGhostPause(s == SCREEN_EXPORT);
  uiRender(false);
}

void uiShowSaved(int secs) { savedSecs = secs; uiGo(SCREEN_SAVED); }

// ---------------------------------------------------------------- input

static bool inBar(int y) { return y < BAR; }
static bool leftIcon(int x, int y) { return inBar(y) && x < 80; }

void uiTap(int x, int y) {
  switch (screen) {
    case SCREEN_LOCK: uiGo(lockFrom); break;
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
      if (leftIcon(x, y)) { uiGo(SCREEN_NOTES); break; }
      Note *n = notesFind(openId);
      if (!n) { uiGo(SCREEN_NOTES); break; }
      int bw = (W - 48 - 16) / 2, by = H - 76 - 64;
      bool right = x >= 24 + bw + 16, row1 = y >= by && y < by + 52, row2 = y >= by + 64 && y < by + 116;
      if (row1 && !right) { pinNote(*n); deleteArmed = false; uiRender(false); }
      else if (row1 && right) { keyboardBegin(kb, n->text, "Edit the note", "", 1000); kbPurpose = 1; kbBack = SCREEN_NOTE; uiGo(SCREEN_KEYBOARD); }
      else if (row2 && !right) { n->archived = !n->archived; if (n->archived) n->pinned = false; n->modified = time(nullptr); notesSave(); uiGo(n->archived ? SCREEN_NOTES : SCREEN_ARCHIVE); }
      else if (row2 && right) {
        if (!deleteArmed) { deleteArmed = true; uiRender(false); }
        else { notesRemove(openId); deleteArmed = false; uiGo(SCREEN_NOTES); }
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
      else if (i == 3) { soundOn = !soundOn; String e; String j = soundOn ? "true" : "false"; pj::Val v; pj::parse(j.c_str(), j.length(), v); cfg::setInPlace("sound", v, e); uiRender(false); }
      else if (i == 4) uiGo(SCREEN_ABOUT);
      break;
    }
    case SCREEN_WIFI: {
      if (leftIcon(x, y)) { uiGo(SCREEN_SETTINGS); break; }
      std::vector<WifiCred> nets = cfg::wifiNetworks();
      int by = BAR + 12 + nets.size() * SROW + 16;
      if (y >= by && y < by + 52) { startWifiScan(); break; }
      int i = settingsRowAt(y);
      if (i >= 0 && i < (int)nets.size()) {
        if (forgetArmed == i) { nets.erase(nets.begin() + i); String e; cfg::setWifiInPlace(nets, e); forgetArmed = -1; wifiMsg = "Forgotten"; }
        else forgetArmed = i;
        uiRender(false);
      } else if (forgetArmed >= 0) { forgetArmed = -1; uiRender(false); }
      break;
    }
    case SCREEN_WIFI_SCAN: {
      if (leftIcon(x, y)) { uiGo(SCREEN_WIFI); break; }
      int i = settingsRowAt(y);
      if (i >= 0 && i < (int)seen.size() && i < 9) {
        joinSsid = seen[i].ssid;
        if (!seen[i].secure) joinNetwork(joinSsid, "");
        else { keyboardBegin(kb, "", ("Password for " + joinSsid).c_str(), "", 64); keyboardSecret(kb); kbPurpose = 2; kbBack = SCREEN_WIFI_SCAN; uiGo(SCREEN_KEYBOARD); }
      }
      break;
    }
    case SCREEN_KEYBOARD: {
      KbResult r = keyboardTap(kb, x, y);
      if (r == KB_CHANGED) redrawSoon();
      else if (r == KB_CANCEL) uiGo(kbBack);
      else if (r == KB_DONE) {
        if (kbPurpose == 1) { Note *n = notesFind(openId); if (n) { n->text = kb.text; n->modified = time(nullptr); notesSave(); } uiGo(SCREEN_NOTE); }
        else if (kbPurpose == 3) { String t = kb.text; t.trim(); if (t.length()) notesAdd(t, "", 0); uiGo(SCREEN_NOTES); }
        else if (kbPurpose == 2) { if (kb.text.length() < 8) { kb.note = "A password has at least 8 characters"; uiRender(false); } else joinNetwork(joinSsid, kb.text); }
      }
      break;
    }
    case SCREEN_ABOUT:
      if (leftIcon(x, y)) uiGo(SCREEN_SETTINGS);
      else if (inFooter(y)) { extern void updateRequest(bool install); uiGo(SCREEN_UPDATE); updateRequest(false); }
      break;
    case SCREEN_UPDATE: {
      if (leftIcon(x, y)) { uiGo(SCREEN_ABOUT); break; }
      if (y >= H - 76 && y < H - 24 && updState() != UPD_INSTALLING) {
        extern void updateRequest(bool install);
        updateRequest(x >= 24 + (W - 48 - 16) / 2 + 16 && updInfo().available);
      }
      break;
    }
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
    case SCREEN_KEYBOARD: uiTap(W - 40, H - 40); break;   // the Done key, bottom right
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
  lockFrom = (screen == SCREEN_LISTENING || screen == SCREEN_SAVED || screen == SCREEN_KEYBOARD) ? SCREEN_NOTES : screen;
  uiGo(SCREEN_LOCK);
}
void uiHome() { uiLock(); }

// Deep sleep keeps the picture; these keep the place.
void uiSaveForSleep() {
  rtcScreen = screen == SCREEN_LISTENING || screen == SCREEN_SAVED || screen == SCREEN_KEYBOARD ? SCREEN_NOTES : screen;
  strlcpy(rtcOpenId, openId.c_str(), sizeof rtcOpenId);
  rtcPage = page; rtcLockFrom = lockFrom;
}
bool uiRestoreAfterSleep() {
  if (rtcScreen < 0) return false;
  screen = (Screen)rtcScreen; openId = rtcOpenId; page = rtcPage; lockFrom = (Screen)rtcLockFrom;
  if (screen == SCREEN_EXPORT) screen = SCREEN_NOTES;
  if (screen == SCREEN_NOTE && !notesFind(openId)) screen = SCREEN_NOTES;
  touchQuick = false; screenGhostPause(false);
  return true;
}

// Something changed the notes outside the UI (a transcription, the web page): redraw if they're on screen.
void uiNotesChanged() {
  if (screen == SCREEN_NOTES || screen == SCREEN_LOCK || screen == SCREEN_ARCHIVE || screen == SCREEN_NOTE) uiRender(false);
}
