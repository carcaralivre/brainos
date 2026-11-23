#include "keyboard.h"
#include "screen.h"
#include "ui.h"

static bool due;
static uint32_t askedAt;
void redrawSoon() { due = true; askedAt = millis(); }
bool redrawDue(uint32_t afterMs) {
  if (due && millis() - askedAt > afterMs) { due = false; return true; }
  return false;
}

String fitted(const GFXfont *font, String s, int width) {
  if (textW(font, s.c_str()) <= width) return s;
  while (s.length() > 1 && textW(font, (s + "...").c_str()) > width) s.remove(utf8Back(s.c_str(), s.length()));
  return s + "...";
}

// Lines of at most `width`, broken at spaces (a word too long for a line is cut). `starts`, when
// given, gets each line's first byte in `s`, so a cursor can be placed.
static std::vector<String> wrapInto(const GFXfont *font, const String &s, int width, std::vector<int> *starts) {
  std::vector<String> lines;
  String line;
  int from = 0, lineStart = 0;
  auto push = [&](const String &l, int at) { lines.push_back(l); if (starts) starts->push_back(at); };
  while (from < (int)s.length()) {
    int space = s.indexOf(' ', from), wordStart = from;
    String word = s.substring(from, space < 0 ? s.length() : space);
    from = space < 0 ? s.length() : space + 1;
    String tryLine = line.length() ? line + " " + word : word;
    if (textW(font, tryLine.c_str()) <= width) { if (!line.length()) lineStart = wordStart; line = tryLine; continue; }
    if (line.length()) push(line, lineStart);
    lineStart = wordStart;
    while (textW(font, word.c_str()) > width) {
      int n = word.length();
      while (textW(font, word.substring(0, n).c_str()) > width) { int back = utf8Back(word.c_str(), n); if (back <= 0) break; n = back; }
      push(word.substring(0, n), lineStart);
      word = word.substring(n);
      lineStart += n;
    }
    line = word;
  }
  if (line.length()) push(line, lineStart);
  return lines;
}
std::vector<String> wrapped(const GFXfont *font, const String &s, int width) { return wrapInto(font, s, width, nullptr); }

enum KeyCode { K_SHIFT = 1, K_BACK, K_LAYER, K_MORE, K_LEFT, K_RIGHT };
struct KeyBox { int16_t x, y, w, h; uint16_t code; };
static const int PITCH = 80, KB_MAX_KEYS = 48;
static int keyboardHeight() { return 4 * PITCH + 2; }
static const char *const LETTERS[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};

static int kbRow(const char *row, uint16_t *out, int max) {
  int n = 0;
  for (int i = 0; row[i] && n < max;) out[n++] = utf8Next(row, i);
  return n;
}

static int keyboardKeys(const Keyboard &kb, int keysY, KeyBox *out) {
  static const char *const SYMBOLS[] = {"1234567890", "@#&-+()/:", "?!'\";%*"};
  static const char *const MORE[] = {"[]{}<>^~|\\", "*$_=`@#&+", "-()/:;%"};
  const int M = 2, GAP = 5, kh = PITCH - 8;
  uint16_t rows[3][16];
  int len[3] = {0, 0, 0};
  const char *const *src = kb.layer == 2 ? MORE : kb.layer == 1 ? SYMBOLS : LETTERS;
  for (int r = 0; r < 3; r++) len[r] = kbRow(src[r], rows[r], 16);
  int cols = max(10, max(len[0], max(len[1], len[2] + 2)));
  int n = 0;
  auto px = [&](int half, int halves) { return M + half * (W - 2 * M) / halves; };
  auto add = [&](int row, int from, int to, int halves, int code) {
    if (n >= KB_MAX_KEYS) return;
    KeyBox &k = out[n++];
    k.x = px(from, halves) + GAP / 2; k.w = px(to, halves) - px(from, halves) - GAP;
    k.y = keysY + row * PITCH; k.h = kh; k.code = code;
  };
  const int H2 = 2 * cols;
  for (int r = 0; r < 2; r++)
    for (int i = 0; i < len[r]; i++) add(r, cols - len[r] + 2 * i, cols - len[r] + 2 * i + 2, H2, rows[r][i]);
  int side = min(3, cols - len[2]);
  add(2, 0, side, H2, kb.layer == 1 || kb.layer == 2 ? K_MORE : K_SHIFT);
  for (int i = 0; i < len[2]; i++) add(2, cols - len[2] + 2 * i, cols - len[2] + 2 * i + 2, H2, rows[2][i]);
  add(2, H2 - side, H2, H2, K_BACK);
  add(3, 0, 8, 48, K_LAYER);    // in 48ths of the width: ?123, comma, space, full stop, the two arrows (equal)
  add(3, 8, 12, 48, ',');
  add(3, 12, 30, 48, ' ');
  add(3, 30, 34, 48, '.');
  add(3, 34, 41, 48, K_LEFT);
  add(3, 41, 48, 48, K_RIGHT);
  return n;
}

void keyboardBegin(Keyboard &kb, const String &text, const char *label, const char *hint, int maxLen) {
  kb.text = text; kb.label = label; kb.hint = hint; kb.note = ""; kb.cursor = text.length();
  kb.layer = 0; kb.shift = text.length() ? 0 : 1; kb.shiftAt = 0; kb.maxLen = maxLen;

}

static void keyboardAutoShift(Keyboard &kb) {
  if (kb.shift == 2) return;
  String t = kb.text.substring(0, kb.cursor);
  kb.shift = !t.length() || t.endsWith(". ") || t.endsWith("? ") || t.endsWith("! ") ? 1 : 0;
  kb.shiftAt = 0;
}

static void keyboardField(Keyboard &kb, int x, int y, int w, int h) {
  canvas->drawRoundRect(x, y, w, h, 10, INK);
  canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, INK);
  const GFXfont *f = F_S12;
  const int LINE = 30, tx = x + 14, tw = w - 32;
  int maxLines = max(1, (h - 10) / LINE), top = maxLines == 1 ? y + (h - LINE) / 2 : y + 6;
  std::vector<int> starts;
  std::vector<String> lines = wrapInto(f, kb.text, tw, &starts);
  if (lines.empty()) { lines.push_back(""); starts.push_back(0); }
  int cl = 0;   // the cursor's line: the last one that starts at or before it
  for (size_t i = 0; i < starts.size(); i++) if (starts[i] <= kb.cursor) cl = i;
  int col = min(kb.cursor - starts[cl], (int)lines[cl].length());
  String before = kb.text.substring(starts[cl], starts[cl] + col);   // what is left of the cursor on its line (a trailing space included)
  int first = max(0, (int)lines.size() - maxLines);
  if (cl < first) first = cl;
  for (int i = first; i < (int)lines.size() && i < first + maxLines; i++) text(tx, top + (i - first) * LINE + 21, f, lines[i].c_str());
  int baseline = top + (cl - first) * LINE + 21, cx = tx + textW(f, before.c_str()) + (before.length() ? 1 : 0);
  if (!kb.text.length()) text(tx + 8, baseline, f, fitted(f, kb.hint, tw - 8).c_str());
  canvas->fillRect(cx, baseline - 19, 3, 25, INK);
}

static void keyLabel(int cx, int cy, const GFXfont *f, const char *s, int lift) { textCentre(cx, cy + lift, f, s); }

static void keyboardDrawKeys(Keyboard &kb, int top) {
  kb.keysTop = top;
  KeyBox keys[KB_MAX_KEYS];
  int n = keyboardKeys(kb, kb.keysTop, keys);
  for (int i = 0; i < n; i++) {
    const KeyBox &k = keys[i];
    bool filled = k.code == K_SHIFT && kb.shift;
    if (filled) canvas->fillRoundRect(k.x, k.y, k.w, k.h, 8, INK);
    else {
      canvas->drawRoundRect(k.x, k.y, k.w, k.h, 8, INK);
      canvas->drawFastHLine(k.x + 6, k.y + k.h, k.w - 12, INK);
      canvas->drawFastHLine(k.x + 8, k.y + k.h + 1, k.w - 16, INK);
    }
    uint16_t fg = filled ? PAPER : INK;
    canvas->setTextColor(fg);
    int cx = k.x + k.w / 2, cy = k.y + k.h / 2;
    switch (k.code) {
      case K_SHIFT:
        canvas->fillTriangle(cx - 12, cy + 1, cx, cy - 13, cx + 12, cy + 1, fg);
        canvas->fillRect(cx - 5, cy, 11, 10, fg);
        if (kb.shift == 2) canvas->fillRect(cx - 9, cy + 14, 19, 3, fg);
        break;
      case K_BACK:
        for (int t = 0; t < 2; t++) { canvas->drawLine(cx - 17 + t, cy, cx - 8 + t, cy - 11, fg); canvas->drawLine(cx - 17 + t, cy, cx - 8 + t, cy + 11, fg); }
        canvas->drawFastHLine(cx - 8, cy - 11, 24, fg); canvas->drawFastHLine(cx - 8, cy - 10, 24, fg);
        canvas->drawFastHLine(cx - 8, cy + 10, 24, fg); canvas->drawFastHLine(cx - 8, cy + 11, 24, fg);
        canvas->drawFastVLine(cx + 15, cy - 11, 23, fg); canvas->drawFastVLine(cx + 14, cy - 11, 23, fg);
        for (int t = 0; t < 2; t++) { canvas->drawLine(cx - 2 + t, cy - 5, cx + 8 + t, cy + 5, fg); canvas->drawLine(cx + 8 + t, cy - 5, cx - 2 + t, cy + 5, fg); }
        break;
      case K_LAYER: keyLabel(cx, cy, F_B12, kb.layer == 1 || kb.layer == 2 ? "ABC" : "?123", 8); break;
      case K_MORE: keyLabel(cx, cy, F_B12, kb.layer == 2 ? "?123" : "=\\<", 8); break;
      case K_LEFT: canvas->fillTriangle(cx + 7, cy - 11, cx + 7, cy + 11, cx - 8, cy, fg); break;
      case K_RIGHT: canvas->fillTriangle(cx - 7, cy - 11, cx - 7, cy + 11, cx + 8, cy, fg); break;
      case ' ': keyLabel(cx, cy, F_S9, "space", 5); break;
      default: {
        uint32_t c = kb.shift ? cpUpper(k.code) : k.code;
        String s; utf8Append(s, c);
        bool lower = c >= 'a' && c <= 'z';
        keyLabel(cx, cy, F_S18, s.c_str(), lower ? 7 : 12);
      }
    }
    canvas->setTextColor(INK);
  }
}

// The screen: a centred title, the field down to the standard Cancel | Done row, then the keys.
void keyboardDraw(Keyboard &kb) {
  uiTitle(fitted(F_B12, kb.label, W - 160).c_str());
  int keysAt = H - keyboardHeight();
  kb.buttonsY = keysAt - UI_GAP - UI_BTN;   // the same gap above and below the buttons
  int fieldY = UI_BAR + 8, fieldBottom = kb.buttonsY - UI_GAP;
  if (kb.note.length()) { fieldBottom -= 28; textCentre(W / 2, fieldBottom + 20, F_B9, fitted(F_B9, kb.note, W - 48).c_str()); }   // a message between the field and the buttons
  keyboardField(kb, UI_MARGIN, fieldY, W - 2 * UI_MARGIN, fieldBottom - fieldY);
  uiButton(UI_MARGIN, kb.buttonsY, UI_HALF, UI_BTN, "Cancel", false);
  uiButton(UI_MARGIN + UI_HALF + UI_GAP, kb.buttonsY, UI_HALF, UI_BTN, "Done", false);
  keyboardDrawKeys(kb, keysAt);
}

KbResult keyboardTap(Keyboard &kb, int x, int y) {
  if (y >= kb.buttonsY - 8 && y < kb.buttonsY + UI_BTN + 8) return x < W / 2 ? KB_CANCEL : KB_DONE;
  if (y < kb.keysTop - 8) return KB_NONE;
  KeyBox keys[KB_MAX_KEYS];
  int n = keyboardKeys(kb, kb.keysTop, keys), best = -1, bestDist = 1 << 20;
  for (int i = 0; i < n; i++) {
    const KeyBox &k = keys[i];
    int dx = max(0, max(k.x - x, x - (k.x + k.w))), dy = max(0, max(k.y - y, y - (k.y + k.h)));
    if (dx + dy < bestDist) bestDist = dx + dy, best = i;
  }
  if (best < 0 || bestDist > 24) return KB_NONE;
  int code = keys[best].code;
  kb.note = "";
  switch (code) {
    case K_SHIFT:
      if (kb.shift == 2) kb.shift = 0;
      else if (kb.shift == 1) kb.shift = kb.shiftAt && millis() - kb.shiftAt < 450 ? 2 : 0;
      else kb.shift = 1;
      kb.shiftAt = millis();
      return KB_CHANGED;
    case K_BACK: {
      if (!kb.cursor) return KB_NONE;
      int i = utf8Back(kb.text.c_str(), kb.cursor);
      kb.text.remove(i, kb.cursor - i);
      kb.cursor = i;
      keyboardAutoShift(kb);
      return KB_CHANGED;
    }
    case K_LAYER: kb.layer = kb.layer == 1 || kb.layer == 2 ? 0 : 1; return KB_CHANGED;
    case K_MORE: kb.layer = kb.layer == 2 ? 1 : 2; return KB_CHANGED;
    case K_LEFT: keyboardMove(kb, -1); return KB_CHANGED;
    case K_RIGHT: keyboardMove(kb, 1); return KB_CHANGED;
  }
  if (utf8Count(kb.text.c_str()) >= kb.maxLen) return KB_NONE;
  uint32_t c = kb.shift ? cpUpper(code) : code;
  String ch; utf8Append(ch, c);
  kb.text = kb.text.substring(0, kb.cursor) + ch + kb.text.substring(kb.cursor);
  kb.cursor += ch.length();
  if (kb.shift == 1) kb.shift = 0;
  if (c == ' ') { if (kb.layer) kb.layer = 0; keyboardAutoShift(kb); }
  return KB_CHANGED;
}

void keyboardMove(Keyboard &kb, int by) {
  if (by < 0 && kb.cursor > 0) kb.cursor = utf8Back(kb.text.c_str(), kb.cursor);
  else if (by > 0 && kb.cursor < (int)kb.text.length()) { int i = kb.cursor; utf8Next(kb.text.c_str(), i); kb.cursor = i; }
}
