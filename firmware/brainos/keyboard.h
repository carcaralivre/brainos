// The on-screen keyboard, from Sticky OS: English letters, symbols, and a page of Portuguese
// accents. No dictation key (BrainOS records with the OK button).
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <vector>

enum KbResult { KB_NONE, KB_CHANGED, KB_DONE, KB_CANCEL };
struct Keyboard {
  String text, label, hint, note;
  int layer = 0;          // 0 letters, 1 digits and symbols, 2 more symbols, 3 accented letters
  int shift = 0;          // 0 off, 1 the next letter, 2 caps lock
  uint32_t shiftAt = 0;
  int maxLen = 120;
  bool masked = false, reveal = false;   // a password: dots, a Show key
  int keysTop = 0, buttonsY = 0;   // where the keys and the Cancel | Done row were drawn
};
void keyboardBegin(Keyboard &kb, const String &text, const char *label, const char *hint, int maxLen);
void keyboardSecret(Keyboard &kb);
void keyboardDraw(Keyboard &kb);                   // the title, the field, Cancel | Done, the keys (bottom of the screen)
KbResult keyboardTap(Keyboard &kb, int x, int y);
String fitted(const GFXfont *font, String s, int width);
std::vector<String> wrapped(const GFXfont *font, const String &s, int width);
// Typing on e-paper: a key marks the screen and loop() redraws once the taps pause.
void redrawSoon();
bool redrawDue(uint32_t afterMs = 350);   // true once, when it's time
