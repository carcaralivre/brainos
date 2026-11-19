// The e-paper screen: a 1-bit canvas that prints UTF-8, shown on the panel with partial or
// full refreshes. Lifted from Sticky OS (sticky-os.ino) with the photo/grey path removed.
#pragma once
#include <Adafruit_GFX.h>
#include "glyphs.h"

static const int NW = 800, NH = 480;        // the panel as it is wired (landscape)
extern int W, H;                            // the picture as it is drawn: 480 x 800 in portrait
static const uint16_t INK = 0, PAPER = 1;   // the panel's 1 bpp format: 1 is white

// The UI fonts, defined once in screen.cpp (Adafruit's headers hold the bitmaps).
extern const GFXfont *const F_S9, *const F_S12, *const F_S18;
extern const GFXfont *const F_B9, *const F_B12, *const F_B18, *const F_B24;

class TextCanvas : public GFXcanvas1 {
 public:
  TextCanvas(uint16_t w, uint16_t h) : GFXcanvas1(w, h) {}
  static const ExtFont *extFor(const GFXfont *f);
  const GFXfont *font() const { return gfxFont; }
  size_t write(uint8_t c) override;

 private:
  uint32_t cp = 0;
  uint8_t pending = 0;
  void drawCodePoint(uint32_t code);
};
extern TextCanvas *canvas;
extern const char *panelName;
extern uint32_t lastActivity;   // millis() of the last touch or button; show() uses it for the ghosting budget
extern int rotation;            // quarter turns the canvas is drawn with (touch.cpp maps back)

bool screenBegin();
void screenRotation(int quarterTurns);
void show(bool full);           // send the canvas to the panel
void screenSleep();             // panel into its low-power state (before deep sleep)
int refreshCount(bool full);
void screenGhostPause(bool on);   // partial refreshes without the budget (the QR loop); a full refresh after
void screenGhostReset();

int textW(const GFXfont *font, const char *s);
void text(int x, int y, const GFXfont *font, const char *s);
void textCentre(int cx, int y, const GFXfont *font, const char *s);
void textRight(int right, int y, const GFXfont *font, const char *s);
