#include "screen.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "src/fonts/FreeSans9ptExt.h"
#include "src/fonts/FreeSans12ptExt.h"
#include "src/fonts/FreeSans18ptExt.h"
#include "src/fonts/FreeSansBold9ptExt.h"
#include "src/fonts/FreeSansBold12ptExt.h"
#include "src/fonts/FreeSansBold18ptExt.h"
#include "src/fonts/FreeSansBold24ptExt.h"
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include "pins.h"
#include "settings.h"
#include "src/sticky_drivers/epaper_panel.h"

const GFXfont *const F_S9 = &FreeSans9pt7b, *const F_S12 = &FreeSans12pt7b, *const F_S18 = &FreeSans18pt7b;
const GFXfont *const F_B9 = &FreeSansBold9pt7b, *const F_B12 = &FreeSansBold12pt7b, *const F_B18 = &FreeSansBold18pt7b,
              *const F_B24 = &FreeSansBold24pt7b;

int W = NW, H = NH;
int rotation;
TextCanvas *canvas;
const char *panelName = "none";
uint32_t lastActivity;

// The canvas prints UTF-8: ASCII from the font itself, the other letters
// from its extra table (glyphs.h), anything else as an ASCII stand-in.
static const struct { const GFXfont *base; const ExtFont *ext; } FONT_EXT[] = {
    {&FreeSans9pt7b, &FreeSans9ptExt},           {&FreeSans12pt7b, &FreeSans12ptExt},
    {&FreeSans18pt7b, &FreeSans18ptExt},         {&FreeSansBold9pt7b, &FreeSansBold9ptExt},
    {&FreeSansBold12pt7b, &FreeSansBold12ptExt}, {&FreeSansBold18pt7b, &FreeSansBold18ptExt},
    {&FreeSansBold24pt7b, &FreeSansBold24ptExt},
};

const ExtFont *TextCanvas::extFor(const GFXfont *f) {
  for (auto &e : FONT_EXT)
    if (e.base == f) return e.ext;
  return nullptr;
}

size_t TextCanvas::write(uint8_t c) {
  if (pending == 0 && c < 0x80) return GFXcanvas1::write(c);
  if (c >= 0xC0) {  // a new character: 2, 3 or 4 bytes
    pending = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    cp = c & (c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F);
    return 1;
  }
  if (c < 0x80 || !pending) {  // a broken sequence: drop it, keep the byte if it's ASCII
    pending = 0;
    return c < 0x80 ? GFXcanvas1::write(c) : 1;
  }
  cp = cp << 6 | (c & 0x3F);
  if (--pending == 0) drawCodePoint(cp);
  return 1;
}

void TextCanvas::drawCodePoint(uint32_t code) {
  const ExtFont *ext = gfxFont ? extFor(gfxFont) : nullptr;
  int at = ext ? extIndex(code) : -1;
  if (at < 0) {  // not in the font: a look-alike, or nothing
    char two[2];
    const char *f = foldCodePoint(code, two);
    for (; f && *f; f++) GFXcanvas1::write(*f);
    return;
  }
  const ExtGlyph &g = ext->glyph[at];
  int xo = g.xOffset;
  if (wrap && g.width && cursor_x + textsize_x * (xo + g.width) > _width) {
    cursor_x = 0;
    cursor_y += (int16_t)textsize_y * gfxFont->yAdvance;
  }
  const uint8_t *bits = ext->bitmap + g.bitmapOffset;
  uint8_t byte = 0, bit = 0;
  startWrite();
  for (int yy = 0; yy < g.height; yy++)
    for (int xx = 0; xx < g.width; xx++) {
      if (!(bit++ & 7)) byte = *bits++;
      if (byte & 0x80) {
        if (textsize_x == 1 && textsize_y == 1) writePixel(cursor_x + xo + xx, cursor_y + g.yOffset + yy, textcolor);
        else writeFillRect(cursor_x + (xo + xx) * textsize_x, cursor_y + (g.yOffset + yy) * textsize_y, textsize_x, textsize_y, textcolor);
      }
      byte <<= 1;
    }
  endWrite();
  cursor_x += g.xAdvance * (int16_t)textsize_x;
}

// ---------------------------------------------------------------- panel

static uint8_t *rotated;  // what goes to the panel: the canvas turned 180 degrees
static int partials;      // ghosting since the last full refresh, in fifths of a partial (see show())
static int fullRefreshes, partialRefreshes;
static bool ghostPaused;
void screenGhostPause(bool on) { ghostPaused = on; }
void screenGhostReset() { ghostPaused = false; partials = 101; }
static seeed_epaper_panel_handle_t panel;
static const seeed_epaper_area_t FULL = {0, 0, NW, NH};

static uint8_t reverseBits(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}

static void rotateCanvas() {
  const uint8_t *src = canvas->getBuffer();
  const size_t n = NW / 8 * NH;
  for (size_t i = 0; i < n; i++) rotated[n - 1 - i] = reverseBits(src[i]);
}

int refreshCount(bool full) { return full ? fullRefreshes : partialRefreshes; }

// A full refresh flashes and leaves no ghosts; a partial one is quick.
// Ghosting budget, in fifths of a partial refresh: one the user caused
// (touch, buttons, in the last minute) costs 5, a timed one with nobody
// around (the clock's minute) costs 1. A full refresh comes at 60.
void show(bool full) {
  if (!panel) return;
  bool idle = (int32_t)(millis() - lastActivity) > 60000;
  if (!ghostPaused && partials + (idle ? 1 : 5) > 100) full = true;
  partials = full ? 0 : ghostPaused ? partials : partials + (idle ? 1 : 5);
  (full ? fullRefreshes : partialRefreshes)++;
  uint32_t t = millis();
  rotateCanvas();
  esp_err_t err;
  if (full) {
    err = seeed_epaper_panel_refresh_area(panel, &FULL, rotated, NW / 8, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_FULL);
  } else {
    err = seeed_epaper_panel_write_bitmap(panel, &FULL, rotated, NW / 8, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL);
    if (err == ESP_OK) err = seeed_epaper_panel_commit(panel, &FULL, SEEED_EPAPER_REFRESH_PARTIAL);
  }
  Serial.printf("screen: %s refresh in %lu ms: %s\n", full ? "full" : "partial", (unsigned long)(millis() - t), esp_err_to_name(err));
}

void screenSleep() { if (panel) seeed_epaper_panel_sleep(panel); }

void screenRotation(int quarterTurns) {
  rotation = quarterTurns & 3;
  canvas->setRotation(rotation);
  W = canvas->width();
  H = canvas->height();
}

bool screenBegin() {
  canvas = new TextCanvas(NW, NH);
  rotated = (uint8_t *)heap_caps_malloc(NW / 8 * NH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!canvas || !canvas->getBuffer() || !rotated) return false;

  // The SD card shares the bus: keep it deselected.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, HIGH);
  gpio_install_isr_service(0);  // the panel driver hooks BUSY; "already installed" is fine

  pinMode(PIN_EPD_EN, OUTPUT);
  digitalWrite(PIN_EPD_EN, HIGH);
  delay(100);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_EPD_MOSI;
  bus.miso_io_num = PIN_EPD_MISO;
  bus.sclk_io_num = PIN_EPD_CLK;
  // Unused data pins must be -1: zero would hand GPIO0 (sensor SCL) to SPI.
  bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
  bus.max_transfer_sz = NW * NH / 8;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 2 * 1000 * 1000;  // the SSD2677 reads at 2.5 MHz at most
  dev.mode = 0;
  dev.spics_io_num = PIN_EPD_CS;
  dev.queue_size = 1;
  dev.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_NO_DUMMY;
  spi_device_handle_t spi;
  if (spi_bus_add_device(SPI2_HOST, &dev, &spi) != ESP_OK) return false;

  seeed_epaper_panel_config_t cfg = {};
  cfg.spi_handle = spi;
  cfg.pin_dc = (gpio_num_t)PIN_EPD_DC;
  cfg.pin_rst = (gpio_num_t)PIN_EPD_RST;
  cfg.pin_busy = (gpio_num_t)PIN_EPD_BUSY;
  cfg.pin_enable = GPIO_NUM_NC;
  cfg.busy_timeout_ms = 10000;
  cfg.reset_low_ms = 10;
  cfg.reset_high_ms = 10;
  cfg.busy_level = 1;
  cfg.enable_level = 1;
  cfg.mirror_x = true;
  // Sticky boards ship with one of two panels; the driver asks which.
  if (seeed_epaper_new_panel(SEEED_EPAPER_PANEL_STICKY_AUTO, &cfg, &panel) != ESP_OK) {
    panel = nullptr;
    return false;
  }
  seeed_epaper_panel_model_t model;
  seeed_epaper_panel_get_model(panel, &model);
  panelName = model == SEEED_EPAPER_PANEL_SSD2677 ? "SSD2677" : "SSD1677";
  if (model == SEEED_EPAPER_PANEL_SSD1677 && PANEL_WRITE_HZ > dev.clock_speed_hz) {
    // Known now, and never read again: open it again on a faster clock.
    seeed_epaper_panel_del(panel);
    spi_bus_remove_device(spi);
    dev.clock_speed_hz = PANEL_WRITE_HZ;
    if (spi_bus_add_device(SPI2_HOST, &dev, &spi) != ESP_OK) return panel = nullptr, false;
    cfg.spi_handle = spi;
    if (seeed_epaper_new_panel(SEEED_EPAPER_PANEL_SSD1677, &cfg, &panel) != ESP_OK) return panel = nullptr, false;
  }
  return true;
}

// ---------------------------------------------------------------- text

int textW(const GFXfont *font, const char *s) {
  const ExtFont *ext = TextCanvas::extFor(font);
  int x = 0, minX = INT16_MAX, maxX = INT16_MIN;
  auto glyph = [&](int w, int h, int xo, int adv) {
    if (w > 0 && h > 0) minX = min(minX, x + xo), maxX = max(maxX, x + xo + w - 1);
    x += adv;
  };
  char two[2];
  for (int i = 0; s[i];) {
    uint32_t cp = utf8Next(s, i);
    int at = ext && cp >= 0x80 ? extIndex(cp) : -1;
    if (at >= 0) {
      const ExtGlyph &g = ext->glyph[at];
      glyph(g.width, g.height, g.xOffset, g.xAdvance);
      continue;
    }
    const char *f = cp < 0x80 ? nullptr : foldCodePoint(cp, two);
    char one[2] = {(char)cp, 0};
    for (const char *p = cp < 0x80 ? one : f; p && *p; p++) {
      uint8_t c = *p;
      if (c < font->first || c > font->last) continue;
      const GFXglyph &g = font->glyph[c - font->first];
      glyph(g.width, g.height, g.xOffset, g.xAdvance);
    }
  }
  return maxX < minX ? 0 : maxX + 1;
}

void text(int x, int y, const GFXfont *font, const char *s) {
  canvas->setFont(font);
  canvas->setCursor(x, y);
  canvas->print(s);
}
void textCentre(int cx, int y, const GFXfont *font, const char *s) { text(cx - textW(font, s) / 2, y, font, s); }
void textRight(int right, int y, const GFXfont *font, const char *s) { text(right - textW(font, s), y, font, s); }
