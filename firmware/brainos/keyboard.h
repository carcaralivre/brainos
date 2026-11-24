// The on-screen keyboard, from Sticky OS: English letters and two pages of symbols, with a
// cursor and arrow keys. No dictation key (BrainOS records with the OK button).
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <vector>

enum KbResult { KB_NONE, KB_CHANGED, KB_DONE, KB_CANCEL };
struct Keyboard {
  String text, label, note;
  int cursor = 0;         // byte offset in text where typing goes
  int layer = 0;          // 0 letters, 1 digits and symbols, 2 more symbols
  int shift = 0;          // 0 off, 1 the next letter, 2 caps lock
  uint32_t shiftAt = 0;
  int maxLen = 120;
  int keysTop = 0, buttonsY = 0;   // where the keys and the Cancel | Done row were drawn
};
void keyboardBegin(Keyboard &kb, const String &text, const char *label, int maxLen);
void keyboardDraw(Keyboard &kb);                   // the title, the field, Cancel | Done, the keys (bottom of the screen)
KbResult keyboardTap(Keyboard &kb, int x, int y);
void keyboardMove(Keyboard &kb, int by);          // the cursor one character left (-1) or right (+1)
String fitted(const GFXfont *font, String s, int width);
std::vector<String> wrapText(const GFXfont *font, const String &s, int width, std::vector<int> *starts = nullptr);   // every screen's line breaking
// Typing on e-paper: a key marks the screen and loop() redraws once the taps pause.
void redrawSoon();
bool redrawDue(uint32_t afterMs = 350);   // true once, when it's time
