// Text beyond ASCII. The screen's fonts are Adafruit GFX fonts, which hold one run of
// characters each (printable ASCII, 0x20..0x7E). Every font the screens use has a second
// table (src/fonts/*ptExt.h, from Sticky OS) with Latin-1, Latin Extended-A and some
// punctuation. The same sorted list of code points (EXT_CODES) indexes every font's
// table, so a lookup is one binary search.
//
// Text is UTF-8 everywhere. The canvas (screen.cpp) decodes it as it prints, and measures
// it the same way; a character no font has gets an ASCII stand-in (foldCodePoint) or nothing.
#pragma once
#include <Arduino.h>

struct ExtGlyph {
  uint32_t bitmapOffset;
  uint8_t width, height, xAdvance;
  int8_t xOffset, yOffset;
};
struct ExtFont {
  const uint8_t *bitmap;
  const ExtGlyph *glyph;  // EXT_COUNT of them, in EXT_CODES' order
};

int extIndex(uint32_t cp);          // where cp is in every ExtFont, -1: not there
// An ASCII look-alike for a character the fonts can't draw ("..." for an
// ellipsis, "1/2", 'e' for an e with a mark the tables don't have); nullptr: none, drop it.
const char *foldCodePoint(uint32_t cp, char *two);

// UTF-8, one code point at a time. Returns 0 at the end; a broken byte comes back as 0xFFFD.
uint32_t utf8Next(const char *s, int &i);
int utf8Back(const char *s, int end);     // where the character before byte `end` starts
int utf8Count(const char *s);             // characters, not bytes
void utf8Append(String &out, uint32_t cp);
// The longest start of `s` that is whole characters and at most `bytes` long.
int utf8Cut(const char *s, int bytes);

// Upper case for the letters the fonts have (Latin-1, Latin Extended-A,
// comma-below, Cyrillic); `turkish`: i -> İ, ı -> I.
uint32_t cpUpper(uint32_t cp);

// Text from the internet or the AI as the screen can show it: what the
// fonts have stays, the rest is folded (curly quotes, dashes, fractions...)
// or dropped; controls become spaces.
String textFold(const String &s);
