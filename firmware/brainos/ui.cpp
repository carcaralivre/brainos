#include "ui.h"
#include <time.h>
#include <algorithm>
#include <vector>
#include "board.h"
#include "chat.h"
#include "config.h"
#include "cost.h"
#include "dates.h"
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
#include "web.h"
extern const char *const VERSION;

Screen screen = SCREEN_LOCK;
static const int BAR = UI_BAR;
static int page;                    // the list page shown
static String openId;               // the note on SCREEN_NOTE
static int notePage, noteFocus = -1, notePages = 1;   // the open note's page; focus: a byte to show, set by uiOpenNote and uiTextGrew
static int chatPage, chatFocus = -1, chatPages = 1;   // the same for the passing chat (focus: a turn)
static Screen chatBack = SCREEN_NOTES;   // where the passing chat was opened from
static String sayMsg;               // uiSay's line
static bool asleep;                 // the padlock on the lock screen: set just before deep sleep (a wake is a restart, which clears it)
// Destructive buttons: a first tap arms ("Yes, …"), a second runs, any other tap disarms.
static bool deleteArmed;            // Delete on a note
static bool deleteAllArmed;         // Delete all in the Archive
static bool exportArmed;            // Archive all on the export screen
static int forgetArmed = -1;        // the saved network whose X was tapped
static bool resetArmed;             // Reset page password in About
static Keyboard kb;
static int kbPurpose;              // 1 edit the open note, 2 a Wi-Fi password, 3 a new note
static Screen kbBack;
static std::vector<NetSeen> seen;  // the last scan
static String joinSsid, wifiMsg;
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
static void iconBubble(int x, int y) {      // 16 x 16: a speech bubble, for a question to the assistant
  canvas->drawRoundRect(x, y + 1, 16, 11, 4, INK);
  canvas->drawRoundRect(x + 1, y + 2, 14, 9, 3, INK);
  canvas->fillTriangle(x + 4, y + 11, x + 9, y + 11, x + 4, y + 16, INK);
  canvas->fillRect(x + 5, y + 10, 3, 2, PAPER);
}
static void iconMic(int cx, int cy) {       // big, for the listening screen
  canvas->fillRoundRect(cx - 18, cy - 44, 36, 60, 18, INK);
  canvas->drawRoundRect(cx - 30, cy - 20, 60, 50, 24, INK);
  canvas->drawRoundRect(cx - 31, cy - 21, 62, 52, 25, INK);
  canvas->fillRect(cx - 24, cy - 24, 48, 8, PAPER);
  canvas->fillRect(cx - 2, cy + 30, 4, 14, INK);
  canvas->fillRect(cx - 14, cy + 44, 28, 4, INK);
}

static void iconPadlock(int cx, int y) {   // 12 x 16, thin: above "N notes" while the board sleeps
  canvas->drawRoundRect(cx - 4, y, 8, 12, 4, INK);       // the shackle
  canvas->drawRoundRect(cx - 3, y + 1, 6, 10, 3, INK);
  canvas->fillRect(cx - 6, y + 6, 12, 10, PAPER);
  canvas->drawRoundRect(cx - 6, y + 6, 12, 10, 2, INK);  // the body
  canvas->drawRoundRect(cx - 5, y + 7, 10, 8, 2, INK);
}
static void dim(int x, int y, int w, int h) {   // grey on a 1-bit screen: every other pixel back to paper
  for (int j = y; j < y + h; j++) for (int i = x + ((j ^ x) & 1); i < x + w; i += 2) canvas->drawPixel(i, j, PAPER);
}

// ---------------------------------------------------------------- text

// At most maxLines lines of s; the last one ends in "…" when there is more.
static std::vector<String> wrapLines(const GFXfont *font, const String &s, int width, int maxLines) {
  std::vector<String> out = wrapText(font, s, width);
  if ((int)out.size() > maxLines) {
    out.resize(maxLines);
    String &last = out.back();
    while (last.length() && textW(font, (last + "…").c_str()) > width) last.remove(utf8Back(last.c_str(), last.length()));
    last += "…";
  }
  return out;
}

static String timeOf(uint32_t t, bool withDate) { return dateText(t, withDate ? "%b %d, %H:%M" : "%H:%M"); }

static String dayLabel(uint32_t t) {
  time_t secs = t, nowS = time(nullptr);
  struct tm a, b; localtime_r(&secs, &a); localtime_r(&nowS, &b);
  int da = a.tm_year * 366 + a.tm_yday, db = b.tm_year * 366 + b.tm_yday;
  if (da == db) return "Today";
  if (da == db - 1) return "Yesterday";
  return String(monthLong(a.tm_mon)) + " " + a.tm_mday + (a.tm_year != b.tm_year ? String(", ") + (a.tm_year + 1900) : "");
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

// A line of laid-out text: where it starts in the source (a byte offset, or a chat turn) and its font.
struct Line { String s; const GFXfont *font; int start; };
// Every line of s into out; `tag` >= 0 puts that number in start instead of the offset.
static void wrapAll(std::vector<Line> &out, const GFXfont *font, const String &s, int width, int tag = -1) {
  std::vector<int> starts;
  std::vector<String> lines = wrapText(font, s, width, &starts);
  for (size_t i = 0; i < lines.size(); i++) out.push_back({lines[i], font, tag >= 0 ? tag : starts[i]});
}
// Pages of `per` lines; `focus` (a start to show, or -1) picks the page, otherwise `pg` is kept in range.
static int pageLines(const std::vector<Line> &lines, int per, int &pg, int &focus, int &pages) {
  pages = max<int>(1, (lines.size() + per - 1) / per);
  if (focus >= 0) {
    int at = lines.size() - 1;   // the first line at or after the focus
    for (size_t i = 0; i < lines.size(); i++) if (lines[i].start >= focus) { at = i; break; }
    pg = at / per;
    focus = -1;
  }
  pg = constrain(pg, 0, pages - 1);
  return pg * per;
}

static String waitingOr(const Note &n) {   // the text, and what it waits for: a recording, an answer, Tidy
  String wait = notesWaitText(n);
  return wait.length() ? n.text + (n.text.length() ? "\n\n" : "") + wait : n.text;
}
// In a list and on the lock screen: one run of text, its line breaks as spaces (an addition
// after a blank line reads on, no "…" where the paragraph ends); a chat shows its first question.
static String previewOf(const Note &n) {
  String s = n.text.length() ? n.text : waitingOr(n);
  int cut = n.asked ? s.indexOf("\n\n") : -1;
  if (cut > 0) s = s.substring(0, cut);
  s.replace("\n", " ");
  while (s.indexOf("  ") >= 0) s.replace("  ", " ");
  s.trim();
  return s;
}

// ---------------------------------------------------------------- the list (Notes and Archive)

static void pageMark(int pg, int pages) {   // "k / n" at the top right of every list and long note
  if (pages > 1) { String p = String(pg + 1) + " / " + pages; textRight(W - 24, 38, F_S9, p.c_str()); }
}

struct Row { int noteIndex; int y, h; String day; };  // noteIndex -1: a day header
static std::vector<Row> rows;
static const int NOTES_A_PAGE = 9, HEAD_H = 32, LIST_TOP = BAR + 8, LIST_BOTTOM_PAD = UI_MARGIN + UI_BTN + 12;   // nine notes a page; the rows stretch to fill it

static std::vector<int> listed(bool archived) { return notesSorted(archived ? 1 : 0, true); }   // newest first; a pinned note keeps its place and shows the pin


// Pages of up to nine notes. A page's rows share the height left after its day headers, so the
// list always ends at the same line just above the buttons; a page whose notes span many days
// takes fewer notes, so a row never gets too short for its two lines.
static const int MIN_ROW = 58;
static int listPages(bool archived) {
  rows.clear();
  auto idx = listed(archived);
  auto &all = notesAll();
  int pageH = H - LIST_TOP - LIST_BOTTOM_PAD, pages = 0;
  for (size_t at = 0; at < idx.size(); pages++) {
    size_t k = min(idx.size() - at, (size_t)NOTES_A_PAGE);
    int rowH = 0;
    for (;; k--) {
      int heads = 0;
      String last;
      for (size_t j = at; j < at + k; j++) { String d = dayLabel(all[idx[j]].created); if (d != last) { heads++; last = d; } }
      int slots = at + k == idx.size() ? NOTES_A_PAGE : k;   // the last page keeps the full-page row height
      rowH = (pageH - heads * HEAD_H) / max<int>(slots, k);
      if (rowH >= MIN_ROW || k == 1) break;
    }
    int y = LIST_TOP + pages * pageH;
    String last;
    for (size_t j = at; j < at + k; j++) {
      String d = dayLabel(all[idx[j]].created);
      if (d != last) { rows.push_back({-1, y, HEAD_H, d}); y += HEAD_H; last = d; }
      rows.push_back({idx[j], y, rowH, ""});
      y += rowH;
    }
    at += k;
  }
  return max(1, pages);
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
      text(24, y + 22, F_B9, r.day.c_str());
      canvas->drawFastHLine(24, y + HEAD_H - 1, W - 48, INK);
      continue;
    }
    const Note &n = all[r.noteIndex];
    String body = previewOf(n);
    auto lines = wrapLines(F_S12, body, W - 48 - 64 - (n.pinned ? 22 : 0) - (n.asked ? 22 : 0), 2);
    int first = y + r.h / 2 + 8 - (lines.size() > 1 ? 12 : 0);   // the text block centred in the row
    for (size_t i = 0; i < lines.size(); i++) text(24, first + i * 24, F_S12, lines[i].c_str());
    String at = timeOf(n.created, false);
    textRight(W - 24, first - 2, F_S9, at.c_str());
    int ix = W - 24 - textW(F_S9, at.c_str()) - 10 - 16;   // the marks, right to left: pin, then bubble
    if (n.pinned) { iconPin(ix, first - 15); ix -= 22; }
    if (n.asked) iconBubble(ix, first - 15);
    canvas->drawFastHLine(24, y + r.h - 1, W - 48, INK);
  }
  if (!any) textCentre(W / 2, H / 2, F_S12, archived ? "The archive is empty" : "No notes yet");
  pageMark(page, pages);
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

static String dateLine() {   // "Sunday\nNov 23": the day, then the date
  if (!clockKnown()) return "Time not set\n";
  time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  return String(weekday(tm.tm_wday)) + "\n" + monthShort(tm.tm_mon) + " " + tm.tm_mday;
}
static String batteryLine() { return now.battery ? String(now.percent) + "%" : String("--%"); }
static String lockDrawn;   // the battery and the date the lock screen shows, for uiMinute

struct LockBox { int y, h; String id; };
static std::vector<LockBox> lockBoxes;   // the pinned notes on the lock screen, for taps
static void drawLock() {
  textCentre(W / 2, 32 + 17, F_S12, batteryLine().c_str());   // 32 px from the top edge of the text
  String date = dateLine();
  int cut = date.indexOf('\n');
  textCentre(W / 2, 160, F_B24, date.substring(0, cut).c_str());   // the day, bold and big
  textCentre(W / 2, 212, F_S18, date.substring(cut + 1).c_str());   // the date under it
  lockDrawn = batteryLine() + date;
  lockBoxes.clear();
  {  // the pinned notes (three at most), newest first, four lines each; a tap on one opens it
    auto idx = listed(false);
    int top = 250, bottom = H - 90;
    for (int i : idx) {
      const Note &p = notesAll()[i];
      if (!p.pinned) continue;
      int maxLines = (bottom - top - 30) / 26;
      if (maxLines < 1) break;
      auto lines = wrapLines(F_S12, previewOf(p), W - 48 - 56 - (p.asked ? 24 : 0), min(maxLines, 4));
      int height = 30 + 26 * max<int>(1, lines.size());
      canvas->drawRoundRect(24, top, W - 48, height, 8, INK);
      iconPin(W - 48, top + height / 2 - 8);   // centred in the box
      if (p.asked) iconBubble(W - 48 - 24, top + height / 2 - 8);
      for (size_t k = 0; k < lines.size(); k++) text(40, top + 34 + k * 26, F_S12, lines[k].c_str());
      lockBoxes.push_back({top, height, p.id});
      top += height + 12;
    }
  }
  int count = notesCount(false);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d note%s", count, count == 1 ? "" : "s");
  textCentre(W / 2, H - 32, F_S12, buf);   // 32 px from the bottom
  if (asleep) iconPadlock(W / 2, H - 76);    // small, just above "N notes", only while the board sleeps
}

static void drawNotes() {
  topBar("Notes", true);   // the arrow goes to the lock screen
  drawList(false);
  uiFooter("New note", false, "Settings", false);
}

// The note screen: the date and its cost, the text in pages, and the buttons. Not archived:
// Pin | Edit | Tidy over Archive | Delete; Edit, Tidy and Archive are greyed while the cloud
// works on the note. The third button is Cancel while an answer or Tidy is awaited, and Original
// (the text before Tidy) on a tidied note; a chat note isn't tidied. Archived: Restore | Delete.
static const int THIRD = (480 - 2 * UI_MARGIN - 2 * UI_GAP) / 3;
static int noteButtonsY(const Note &n) { return n.archived ? FOOT_Y : FOOT_Y - UI_BTN - 12; }
static void drawNote() {
  Note *n = notesFind(openId);
  topBar("Note", true);
  if (!n) { textCentre(W / 2, H / 2, F_S12, "Gone"); return; }
  String date = timeOf(n->created, true);
  text(24, BAR + 28, F_S9, date.c_str());
  if (n->asked) iconBubble(W / 2 - 8, BAR + 15);   // a chat note: the bubble centred on the date line
  textRight(W - 24, BAR + 28, F_S9, costText(n->cost).c_str());
  int by = noteButtonsY(*n), top = BAR + 60;
  int per = max(1, (by - 16 - top) / 26 - (sayMsg.length() ? 1 : 0));
  std::vector<Line> lines;
  wrapAll(lines, F_S12, waitingOr(*n), W - 48);
  int first = pageLines(lines, per, notePage, noteFocus, notePages);
  for (int i = 0; i < per && first + i < (int)lines.size(); i++) text(24, top + i * 26, F_S12, lines[first + i].s.c_str());
  if (notePages > 1) pageMark(notePage, notePages);
  else {  // where this note sits in its list, for Up/Down
    auto idx = listed(n->archived);
    for (size_t k = 0; k < idx.size(); k++) if (notesAll()[idx[k]].id == openId) { pageMark(k, idx.size()); break; }
  }
  if (sayMsg.length()) textCentre(W / 2, by - 20, F_S9, sayMsg.c_str());
  const char *del = deleteArmed ? "Yes, delete" : "Delete";
  if (n->archived) { uiFooter("Restore", false, del, deleteArmed); return; }
  bool busy = n->busy();
  uiButton(24, by, THIRD, UI_BTN, n->pinned ? "Unpin" : "Pin", n->pinned);
  uiButton(24 + THIRD + UI_GAP, by, THIRD, UI_BTN, "Edit", false);
  if (busy) dim(24 + THIRD + UI_GAP, by, THIRD, UI_BTN);
  bool awaited = n->asking || n->tidying;
  uiButton(24 + 2 * (THIRD + UI_GAP), by, THIRD, UI_BTN, awaited ? "Cancel" : n->orig.length() ? "Original" : "Tidy", false);
  if (!awaited && (busy || !n->text.length() || (n->asked && !n->orig.length()))) dim(24 + 2 * (THIRD + UI_GAP), by, THIRD, UI_BTN);
  uiFooter("Archive", false, del, deleteArmed);
  if (busy) dim(UI_MARGIN, FOOT_Y, UI_HALF, UI_BTN);
}

// The passing chat: the questions in bold, the answers plain, and Save as note.
static void drawChat() {
  topBar("Chat", true);
  int top = BAR + 40, per = max(1, (FOOT_Y - 16 - top) / 26 - (sayMsg.length() ? 1 : 0));
  std::vector<Line> lines;
  int n = chat.turns.size();
  for (int i = 0; i < n; i++) {
    if (i) lines.push_back({"", F_S12, i - 1});
    wrapAll(lines, i % 2 ? F_S12 : F_B12, chat.turns[i], W - 48, i);
  }
  String why = sttProblem();
  String wait = chat.audio.length() ? String("Question, ") + chat.secs + " s, " + (why.length() ? why : String("sending"))
              : chat.asking ? (why.length() ? "(asking: " + why + ")" : String("(asking…)")) : chat.note;
  if (wait.length()) { if (n) lines.push_back({"", F_S12, n - 1}); wrapAll(lines, F_S12, wait, W - 48, n); }
  int first = pageLines(lines, per, chatPage, chatFocus, chatPages);
  for (int i = 0; i < per && first + i < (int)lines.size(); i++) text(24, top + i * 26, lines[first + i].font, lines[first + i].s.c_str());
  pageMark(chatPage, chatPages);
  if (sayMsg.length()) textCentre(W / 2, FOOT_Y - 20, F_S9, sayMsg.c_str());
  uiButton(24, FOOT_Y, W - 48, UI_BTN, "Save as note", false);
  if (chat.waiting() || chat.turns.empty()) dim(24, FOOT_Y, W - 48, UI_BTN);
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

// The network lists (saved and found) share the notes' rule: rows that fill the page, a line
// under each, "k / n" at the top, Up/Down and a swipe to page.
static const int NETS_A_PAGE = 8;
static int netRowH() { return (H - LIST_TOP - LIST_BOTTOM_PAD) / NETS_A_PAGE; }
static int netRowAt(int y, int pg) { int k = y < LIST_TOP ? -1 : (y - LIST_TOP) / netRowH(); return k < 0 || k >= NETS_A_PAGE ? -1 : pg * NETS_A_PAGE + k; }
static int netPages(size_t n) { return max<int>(1, (n + NETS_A_PAGE - 1) / NETS_A_PAGE); }

// The saved networks as listed: the connected one first, the others A to Z. (The order they are
// saved in, which the board tries them in, stays as it is.)
static std::vector<WifiCred> shownNets() {
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  String on = netUp() ? netName() : String();
  std::sort(nets.begin(), nets.end(), [&](const WifiCred &a, const WifiCred &b) {
    if ((a.ssid == on) != (b.ssid == on)) return a.ssid == on;
    String x = a.ssid, y = b.ssid; x.toLowerCase(); y.toLowerCase();
    return x < y;
  });
  return nets;
}

static int wifiPage;
static const int FORGET_W = 132;   // the armed "Yes, forget" button at the end of a row
static void drawWifi() {
  topBar("Wi-Fi", true);
  std::vector<WifiCred> nets = shownNets();
  wifiPage = constrain(wifiPage, 0, netPages(nets.size()) - 1);
  int rowH = netRowH();
  for (int k = 0; k < NETS_A_PAGE; k++) {
    size_t i = wifiPage * NETS_A_PAGE + k;
    if (i >= nets.size()) break;
    int y = LIST_TOP + k * rowH, mid = y + rowH / 2;
    bool on = netUp() && netName() == nets[i].ssid;
    text(24, mid + 8, F_S12, fitted(F_S12, nets[i].ssid, W - 48 - 170).c_str());
    if ((int)i == forgetArmed) {
      canvas->fillRoundRect(W - 24 - FORGET_W, mid - 20, FORGET_W, 40, 8, INK);
      canvas->setTextColor(PAPER); textCentre(W - 24 - FORGET_W / 2, mid + 6, F_B9, "Yes, forget"); canvas->setTextColor(INK);
    } else {
      if (on) textRight(W - 24 - 16 - 14, mid + 6, F_S9, "connected");
      iconX(W - 24 - 16, mid - 8);
    }
    canvas->drawFastHLine(24, y + rowH - 1, W - 48, INK);
  }
  if (nets.empty()) textCentre(W / 2, H / 2, F_S12, "No saved networks");
  pageMark(wifiPage, netPages(nets.size()));
  uiButton(24, FOOT_Y, W - 48, 52, "Add a network", false);
  if (wifiMsg.length()) textCentre(W / 2, FOOT_Y - 20, F_S9, fitted(F_S9, wifiMsg, W - 48).c_str());
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
static int scanPage;   // the page of found networks shown
static void drawWifiScan() {
  topBar("Networks", true);
  if (seen.empty()) textCentre(W / 2, H / 2, F_S12, "None found");
  int rowH = netRowH();
  scanPage = constrain(scanPage, 0, netPages(seen.size()) - 1);
  for (int k = 0; k < NETS_A_PAGE; k++) {
    size_t i = scanPage * NETS_A_PAGE + k;
    if (i >= seen.size()) break;
    int bars = seen[i].rssi > -55 ? 4 : seen[i].rssi > -67 ? 3 : seen[i].rssi > -78 ? 2 : 1;
    int y = LIST_TOP + k * rowH;
    text(24, y + rowH / 2 + 8, F_S12, fitted(F_S12, seen[i].ssid, W - 48 - 70).c_str());
    iconSignal(W - 24 - 22, y + rowH / 2 - 8, bars);
    if (seen[i].secure) iconLock(W - 24 - 22 - 24, y + rowH / 2 - 8);
    canvas->drawFastHLine(24, y + rowH - 1, W - 48, INK);
  }
  pageMark(scanPage, netPages(seen.size()));
  uiButton(24, FOOT_Y, W - 48, 52, "Scan again", false);
}

static void drawAbout() {
  topBar("About", true);
  String lines[] = {
    cfg::str("device.name"), String("BrainOS ") + VERSION,
    netUp() ? "Wi-Fi: " + netName() + ", " + netAddress() : "No Wi-Fi",
    "Battery: " + batteryLine() + (now.usb ? ", charging" : ""),
    "Notes: " + String(notesCount(false)) + ", archived " + notesCount(true),
    "Today " + costText(costDaysAgo(0)) + " · This week " + costText(costWeeksAgo(0)) + " · This month " + costText(costMonthsAgo(0)),
  };
  int y = BAR + 40;
  for (auto &l : lines) { if (!l.length()) continue; auto w = wrapLines(F_S9, l, W - 48, 2); for (auto &t : w) { text(24, y, F_S9, t.c_str()); y += 24; } y += 6; }
  UpdInfo u = updInfo();
  if (u.status.length() && u.checkedMs) { auto w = wrapLines(F_S9, u.status, W - 48, 3); y += 10; for (auto &t : w) { text(24, y, F_S9, t.c_str()); y += 24; } }
  if (webHasPassword()) uiButton(24, FOOT_Y - UI_BTN - 12, W - 48, UI_BTN, resetArmed ? "Yes, reset" : "Reset page password", resetArmed);   // brain.local asks for a new one at the next visit
  bool checking = u.state == UPD_CHECKING || updateQueued();
  if (u.state == UPD_INSTALLING || updateInstalling()) uiButton(24, FOOT_Y, W - 48, 52, "Installing, don't switch off", true);
  else if (u.available) uiFooter(checking ? "Checking…" : "Check again", false, ("Install " + u.latest).c_str(), true);
  else uiButton(24, FOOT_Y, W - 48, 52, checking ? "Checking…" : "Check for update", false);
}

static void drawKeyboard() { keyboardDraw(kb); }

static void startWifiScan() {
  canvas->fillScreen(PAPER); topBar("Networks", false); textCentre(W / 2, H / 2, F_S12, "Scanning…"); show(false);
  String why;
  netScan(seen, why);
  scanPage = 0;
  uiGo(SCREEN_WIFI_SCAN);
}

static bool joinCancelled() {   // a tap on the Cancel button while the board tries to connect
  TouchEvent te = touchPoll();
  return te.kind == TOUCH_TAP && inFooter(te.y);
}

// onFail: where a failed or cancelled attempt lands (the saved list, or the password keyboard).
static void joinNetwork(const String &ssid, const String &pass, Screen onFail) {
  canvas->fillScreen(PAPER); topBar("Wi-Fi", false);
  textCentre(W / 2, H / 2, F_S12, fitted(F_S12, "Connecting to " + ssid + "…", W - 48).c_str());
  uiButton(24, FOOT_Y, W - 48, 52, "Cancel", false);
  show(false);
  String why;
  bool ok = netJoin(ssid, pass, why, joinCancelled);
  if (ok) {
    std::vector<WifiCred> nets = cfg::wifiWithout(ssid);
    nets.insert(nets.begin(), {ssid, pass});
    String err; cfg::setWifi(nets, err);
    wifiMsg = err.length() ? "Connected, but not saved: " + err : "Connected and saved";
    uiGo(SCREEN_WIFI);
  } else if (onFail == SCREEN_KEYBOARD) {
    kb.note = why;
    uiGo(SCREEN_KEYBOARD);
  } else {
    wifiMsg = why;
    uiGo(SCREEN_WIFI);
  }
}

static void drawArchive() {
  topBar("Archive", true);
  drawList(true);
  if (notesCount(true)) uiButton(24, FOOT_Y, W - 48, 52, deleteAllArmed ? "Yes, delete all" : "Delete all", deleteAllArmed);
}

static bool listeningDone, listeningFull;
void uiListeningFull() { listeningFull = true; uiRender(false); }
bool uiListeningDone() { if (listeningDone) return false; listeningDone = true; return true; }
static String notice;   // a recording that failed: why, on the listening screen for a moment
void uiNotice(const char *msg) { notice = msg; Screen keep = screen; screen = SCREEN_LISTENING; uiRender(false); screen = keep; notice = ""; delay(1800); }
static void drawListening() {
  if (notice.length()) { iconMic(W / 2, H / 2 - 60); textCentre(W / 2, H / 2 + 60, F_B18, notice.c_str()); return; }
  iconMic(W / 2, H / 2 - 60);
  textCentre(W / 2, H / 2 + 60, F_B18, listeningFull ? "30 s is the limit" : listeningDone ? "Saving…" : "Listening…");
}

void uiRender(bool full) {
  canvas->fillScreen(PAPER);
  canvas->setTextColor(INK);
  switch (screen) {
    case SCREEN_LOCK: drawLock(); break;
    case SCREEN_NOTES: drawNotes(); break;
    case SCREEN_NOTE: drawNote(); break;
    case SCREEN_CHAT: drawChat(); break;
    case SCREEN_ARCHIVE: drawArchive(); break;
    case SCREEN_EXPORT: topBar("Export", true); exportDraw(exportArmed); break;
    case SCREEN_SETTINGS: drawSettings(); break;
    case SCREEN_KEYBOARD: drawKeyboard(); break;
    case SCREEN_WIFI: drawWifi(); break;
    case SCREEN_WIFI_SCAN: drawWifiScan(); break;
    case SCREEN_ABOUT: drawAbout(); break;
    case SCREEN_LISTENING: drawListening(); break;
  }
  show(full);
}

void uiGo(Screen s) {
  if (s != screen) {
    deleteArmed = deleteAllArmed = exportArmed = resetArmed = false; forgetArmed = -1; listeningDone = listeningFull = false; sayMsg = "";
    if (s == SCREEN_NOTES || s == SCREEN_ARCHIVE) page = 0;
    if (s != SCREEN_WIFI) wifiMsg = "";
    if (screen == SCREEN_CHAT && s != SCREEN_LISTENING) chatClear();   // leaving the passing chat forgets it
  }
  screen = s;
  touchQuick = s == SCREEN_KEYBOARD;
  screenGhostPause(s == SCREEN_EXPORT);
  uiRender(false);
}

void uiOpenNote(const String &id, int focus) {
  openId = id; notePage = 0; noteFocus = focus; deleteArmed = false; sayMsg = "";
  if (screen == SCREEN_NOTE) uiRender(false); else uiGo(SCREEN_NOTE);
}
String uiOpenNoteId() { return openId; }

static Screen backFrom(Screen s) {   // a screen that can be gone back to (the export loop and a scan can't)
  return s == SCREEN_EXPORT ? SCREEN_SETTINGS : s == SCREEN_WIFI_SCAN ? SCREEN_WIFI : s == SCREEN_LISTENING || s == SCREEN_KEYBOARD || s == SCREEN_CHAT ? SCREEN_NOTES : s;
}
void uiOpenChat(Screen back) {
  if (back != SCREEN_CHAT) chatBack = backFrom(back);   // a new chat remembers where it came from
  if (chatBack == SCREEN_LOCK) chatBack = SCREEN_NOTES;
  chatPage = 0; chatFocus = chat.turns.size(); sayMsg = "";   // the page with the new question
  if (screen == SCREEN_CHAT) uiRender(false); else uiGo(SCREEN_CHAT);   // from Listening, uiGo keeps the chat
}

void uiSay(const char *msg) { sayMsg = msg; uiRender(false); }
void uiSleeping(bool on) { asleep = on; }
void uiMinute() { if (screen == SCREEN_LOCK && batteryLine() + dateLine() != lockDrawn) uiRender(false); }

// The open note or the chat grew (a transcription, an answer, Tidy): show the page where it did.
void uiTextGrew(const String &noteId, int offset) {
  if (!noteId.length()) { if (screen == SCREEN_CHAT) chatFocus = offset; }
  else if (screen == SCREEN_NOTE && openId == noteId) noteFocus = offset;
}

// ---------------------------------------------------------------- input

// The edit of the open note, kept: an empty text leaves the note as it was (Delete is the way to remove it).
static void keepEdit() {
  String t = kb.text; t.trim();
  Note *n = notesFind(openId);
  if (!n || !t.length() || kb.text == n->text) return;
  n->text = kb.text; n->orig = ""; n->modified = time(nullptr);   // edited: the text before Tidy no longer undoes it
  notesSave();
}

static void keyboardResult(KbResult r) {
  if (r == KB_CHANGED) redrawSoon();
  else if (r == KB_CANCEL) uiGo(kbBack);
  else if (r == KB_DONE) {
    if (kbPurpose == 1) { keepEdit(); uiGo(SCREEN_NOTE); }
    else if (kbPurpose == 3) { String t = kb.text; t.trim(); if (t.length()) { Note &n = notesAdd(t, "", 0); uiOpenNote(n.id, 0); } else uiGo(SCREEN_NOTES); }
    else if (kbPurpose == 2) { if (kb.text.length() < 8) { kb.note = "A password has at least 8 characters"; uiRender(false); } else joinNetwork(joinSsid, kb.text, SCREEN_KEYBOARD); }
  }
}

static bool inBar(int y) { return y < BAR; }
static bool leftIcon(int x, int y) { return inBar(y) && x < 80; }

// A tap while a destructive button is armed only disarms it, unless it is on that button.
static bool disarm() {
  if (!deleteArmed && !deleteAllArmed && !exportArmed && !resetArmed && forgetArmed < 0) return false;
  deleteArmed = deleteAllArmed = exportArmed = resetArmed = false; forgetArmed = -1;
  uiRender(false);
  return true;
}

static void switchNote(int by) {
  Note *n = notesFind(openId);
  auto idx = listed(n && n->archived);
  for (size_t k = 0; k < idx.size(); k++)
    if (notesAll()[idx[k]].id == openId) {
      int j = (int)k + by;
      if (j >= 0 && j < (int)idx.size()) uiOpenNote(notesAll()[idx[j]].id, 0);
      return;
    }
}

void uiTap(int x, int y) {
  bool hadMsg = sayMsg.length();
  sayMsg = "";
  switch (screen) {
    case SCREEN_LOCK:
      for (auto &b : lockBoxes) if (y >= b.y && y < b.y + b.h) { uiOpenNote(b.id, 0); return; }
      uiGo(SCREEN_NOTES);   // a tap opens the list; the OK button goes back to where the lock came from
      break;
    case SCREEN_NOTES: {
      if (leftIcon(x, y)) { uiLock(); break; }
      if (inFooter(y)) {
        if (x < W / 2) { keyboardBegin(kb, "", "New note", KB_NOTE_MAX); kbPurpose = 3; kbBack = SCREEN_NOTES; uiGo(SCREEN_KEYBOARD); }
        else uiGo(SCREEN_SETTINGS);
        break;
      }
      int i = rowAt(y);
      if (i >= 0) uiOpenNote(notesAll()[i].id, 0);
      break;
    }
    case SCREEN_NOTE: {
      Note *n = notesFind(openId);
      Screen home = n && n->archived ? SCREEN_ARCHIVE : SCREEN_NOTES;   // back and Delete return to the list the note came from
      if (leftIcon(x, y) || !n) { uiGo(home); break; }
      int by = noteButtonsY(*n);
      bool row1 = !n->archived && y >= by && y < by + UI_BTN, foot = inFooter(y), right = x >= W / 2;
      if (foot && right) {   // Delete: arms, then deletes
        if (!deleteArmed) { deleteArmed = true; uiRender(false); }
        else { notesRemove(openId); deleteArmed = false; uiGo(home); }
        break;
      }
      if (disarm()) break;
      int third = (x - 24) / (THIRD + UI_GAP);
      if (row1 && third == 0) { notesPin(*n); uiRender(false); }
      else if (row1 && third == 1 && !n->busy()) { keyboardBegin(kb, n->text, "Edit note", max<int>(KB_NOTE_MAX, utf8Count(n->text.c_str()) + 200)); kbPurpose = 1; kbBack = SCREEN_NOTE; uiGo(SCREEN_KEYBOARD); }
      else if (row1 && third == 2 && (n->asking || n->tidying)) {   // Cancel: the note is free again; an answer on its way is dropped
        if (n->asking) n->text += "\n\n(no answer: cancelled)";   // in the answer's place, so the turns stay in order
        n->asking = n->tidying = false; n->modified = time(nullptr); notesSave(); uiRender(false);
      }
      else if (row1 && third == 2 && !n->busy() && n->orig.length()) { n->text = n->orig; n->orig = ""; n->modified = time(nullptr); notesSave(); notePage = 0; uiRender(false); }
      else if (row1 && third == 2 && !n->busy() && n->text.length() && !n->asked) { n->tidying = true; notesSave(); uiRender(false); }
      else if (foot && n->archived) { n->archived = false; n->modified = time(nullptr); notesSave(); uiGo(SCREEN_ARCHIVE); }
      else if (foot && !n->busy()) { n->archived = true; n->pinned = false; n->modified = time(nullptr); notesSave(); uiGo(SCREEN_NOTES); }
      else if (hadMsg) uiRender(false);
      break;
    }
    case SCREEN_CHAT:
      if (leftIcon(x, y)) { uiGo(chatBack); break; }
      if (inFooter(y)) { Note *n = chatSave(); if (n) { String id = n->id; uiOpenNote(id, 0); } break; }
      if (hadMsg) uiRender(false);
      break;
    case SCREEN_ARCHIVE: {
      if (inFooter(y) && notesCount(true)) {
        if (!deleteAllArmed) { deleteAllArmed = true; uiRender(false); }
        else {
          notesRemoveArchived();
          deleteAllArmed = false;
          uiRender(false);
        }
        break;
      }
      if (disarm()) break;
      if (leftIcon(x, y)) { uiGo(SCREEN_SETTINGS); break; }
      int i = rowAt(y);
      if (i >= 0) uiOpenNote(notesAll()[i].id, 0);
      break;
    }
    case SCREEN_EXPORT:
      if (inFooter(y) && exportParts()) {
        if (cfg::flag("export.archive") && !exportArmed) { exportArmed = true; uiRender(false); }
        else { exportDone(false); screenGhostReset(); uiGo(SCREEN_NOTES); }   // the reset makes that one refresh a full one
        break;
      }
      if (disarm()) break;
      if (leftIcon(x, y)) { exportDone(true); screenGhostReset(); uiGo(SCREEN_SETTINGS); }
      break;
    case SCREEN_SETTINGS: {
      if (leftIcon(x, y)) { uiGo(SCREEN_NOTES); break; }
      int i = settingsRowAt(y);
      if (i == 0) { wifiPage = 0; uiGo(SCREEN_WIFI); }
      else if (i == 1) { exportStart(); uiGo(SCREEN_EXPORT); }
      else if (i == 2) uiGo(SCREEN_ARCHIVE);
      else if (i == 3) uiGo(SCREEN_ABOUT);
      break;
    }
    case SCREEN_WIFI: {
      std::vector<WifiCred> nets = shownNets();
      int i = netRowAt(y, wifiPage);
      bool real = i >= 0 && i < (int)nets.size();
      if (real && forgetArmed == i && x >= W - 24 - FORGET_W) {   // the second tap forgets
        String e; cfg::setWifi(cfg::wifiWithout(nets[i].ssid), e); forgetArmed = -1; wifiMsg = "Forgotten"; uiRender(false);
        break;
      }
      if (disarm()) break;
      if (leftIcon(x, y)) { uiGo(SCREEN_SETTINGS); break; }
      if (inFooter(y)) { startWifiScan(); break; }
      if (real && x >= W - 24 - 44) { forgetArmed = i; uiRender(false); }   // the X arms
      else if (real) {   // the name: connect to it
        if (netUp() && netName() == nets[i].ssid) { wifiMsg = "Already connected"; uiRender(false); }
        else joinNetwork(nets[i].ssid, nets[i].pass, SCREEN_WIFI);
      }
      break;
    }
    case SCREEN_WIFI_SCAN: {
      if (leftIcon(x, y)) { uiGo(SCREEN_WIFI); break; }
      if (inFooter(y)) { startWifiScan(); break; }
      int i = netRowAt(y, scanPage);
      if (i >= 0 && i < (int)seen.size()) {
        joinSsid = seen[i].ssid;
        if (!seen[i].secure) joinNetwork(joinSsid, "", SCREEN_WIFI_SCAN);
        else { keyboardBegin(kb, "", ("Password for " + joinSsid).c_str(), 64); kbPurpose = 2; kbBack = SCREEN_WIFI_SCAN; uiGo(SCREEN_KEYBOARD); }
      }
      break;
    }
    case SCREEN_KEYBOARD: keyboardResult(keyboardTap(kb, x, y)); break;
    case SCREEN_ABOUT:
      if (webHasPassword() && y >= FOOT_Y - UI_BTN - 12 && y < FOOT_Y - 12) {   // Reset page password: arms, then resets
        if (!resetArmed) resetArmed = true;
        else { webForgetPassword(); resetArmed = false; }
        uiRender(false);
        break;
      }
      if (disarm()) break;
      if (leftIcon(x, y)) uiGo(SCREEN_SETTINGS);
      else if (inFooter(y) && !updateInstalling()) {
        updateRequest(updInfo().available && uiFooterHit(x, y) == 1);
      }
      break;
    default: break;
  }
}

void uiSwipe(int x, int y, int dx, int dy) {
  bool across = abs(dx) > abs(dy);
  if (screen == SCREEN_NOTE && across) { sayMsg = ""; switchNote(dx < 0 ? 1 : -1); return; }   // to the left: the next note
  if (across) return;
  int by = dy < 0 ? 1 : -1;   // up: forward
  if (screen == SCREEN_NOTE) { if (notePages > 1) uiMove(by); return; }
  if (screen == SCREEN_NOTES || screen == SCREEN_ARCHIVE || screen == SCREEN_WIFI || screen == SCREEN_WIFI_SCAN || screen == SCREEN_CHAT) uiMove(by);
}

void uiOk() {
  switch (screen) {
    case SCREEN_LOCK: uiGo(lockFrom); break;
    case SCREEN_KEYBOARD: keyboardResult(KB_DONE); break;   // OK is Done
    case SCREEN_LISTENING: break;
    default: uiLock(); break;   // one click locks
  }
}

void uiMove(int by) {
  if (screen == SCREEN_KEYBOARD) { keyboardMove(kb, by); uiRender(false); return; }   // Down: right, Up: left
  bool hadMsg = sayMsg.length();
  sayMsg = "";
  int *pg = nullptr, pages = 1;
  switch (screen) {
    case SCREEN_NOTE:
      if (notePages <= 1) { switchNote(by); if (hadMsg) uiRender(false); return; }   // a short note: Up/Down go through the notes
      pg = &notePage; pages = notePages; break;
    case SCREEN_CHAT: pg = &chatPage; pages = chatPages; break;
    case SCREEN_WIFI_SCAN: pg = &scanPage; pages = netPages(seen.size()); break;
    case SCREEN_WIFI: pg = &wifiPage; pages = netPages(cfg::wifiNetworks().size()); break;
    case SCREEN_NOTES: case SCREEN_ARCHIVE: pg = &page; pages = listPages(screen == SCREEN_ARCHIVE); break;
    default: return;
  }
  int p = constrain(*pg + by, 0, pages - 1);
  if (p != *pg || hadMsg) { *pg = p; uiRender(false); }
}

void uiLock() {
  if (screen == SCREEN_LOCK) return;
  if (screen == SCREEN_EXPORT) { exportDone(true); screenGhostReset(); }
  if (screen == SCREEN_KEYBOARD) {   // locked or asleep under the keyboard: what was typed is kept (a Wi-Fi password is not)
    String t = kb.text; t.trim();
    if (kbPurpose == 1) keepEdit();
    else if (kbPurpose == 3 && t.length()) notesAdd(t, "", 0);
  }
  lockFrom = screen == SCREEN_CHAT ? chatBack : backFrom(screen);
  if (lockFrom == SCREEN_LOCK) lockFrom = SCREEN_NOTES;
  uiGo(SCREEN_LOCK);
}

// Deep sleep keeps the picture; these keep the place.
void uiSaveForSleep() {
  rtcScreen = backFrom(screen);
  strlcpy(rtcOpenId, openId.c_str(), sizeof rtcOpenId);
  rtcPage = page; rtcLockFrom = lockFrom;
}
bool uiRestoreAfterSleep() {
  if (rtcScreen < 0 || rtcScreen > SCREEN_ABOUT || rtcLockFrom < 0 || rtcLockFrom > SCREEN_ABOUT) return false;   // nothing kept, or not a screen
  rtcOpenId[sizeof rtcOpenId - 1] = 0;
  screen = backFrom((Screen)rtcScreen); openId = rtcOpenId; page = rtcPage; lockFrom = backFrom((Screen)rtcLockFrom);
  if (lockFrom == SCREEN_LOCK) lockFrom = SCREEN_NOTES;
  if (screen == SCREEN_NOTE && !notesFind(openId)) screen = SCREEN_NOTES;
  if (lockFrom == SCREEN_NOTE && !notesFind(openId)) lockFrom = SCREEN_NOTES;
  touchQuick = false; screenGhostPause(false);
  return true;
}

// Something changed the notes outside the UI (a transcription, an answer, the web page): redraw if they're on screen.
void uiNotesChanged() {
  if (screen == SCREEN_NOTES || screen == SCREEN_LOCK || screen == SCREEN_ARCHIVE || screen == SCREEN_NOTE || screen == SCREEN_CHAT) uiRender(false);
}
