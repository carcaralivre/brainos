#include "capture.h"
#include <ESP_I2S.h>
#include <LittleFS.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include "notes.h"
#include "pins.h"

static const int RATE = 16000, MAX_SECONDS = 30;
static const char *why = "";
const char *captureError() { return why; }

// From Sticky OS: GPIO19/20 are the chip's USB pins, handed back to the pin matrix first.
static size_t micRecord(bool (*keepGoing)(), void (*atLimit)(), int16_t **pcm) {
  *pcm = nullptr;
  why = "";
  gpio_reset_pin((gpio_num_t)PIN_MIC_CLK);
  gpio_reset_pin((gpio_num_t)PIN_MIC_DATA);
  pinMode(PIN_MIC_EN, OUTPUT);
  digitalWrite(PIN_MIC_EN, HIGH);
  I2SClass i2s;
  i2s.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
  if (!i2s.begin(I2S_MODE_PDM_RX, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    digitalWrite(PIN_MIC_EN, LOW);
    why = "Microphone didn't start";
    return 0;
  }
  const size_t maxSamples = (size_t)RATE * MAX_SECONDS;
  int16_t *buf = (int16_t *)heap_caps_malloc(maxSamples * 2, MALLOC_CAP_SPIRAM);
  size_t n = 0;
  if (buf) {
    i2s.readBytes((char *)buf, 1600 * 2);  // the mic's first 100 ms is its power-up thump
    while (n < maxSamples && keepGoing()) {
      size_t want = min<size_t>(1600, maxSamples - n);
      n += i2s.readBytes((char *)(buf + n), want * 2) / 2;
    }
  }
  i2s.end();
  digitalWrite(PIN_MIC_EN, LOW);
  if (n >= maxSamples && atLimit) atLimit();
  if (!buf || !n) { why = buf ? "Too short" : "No memory to record"; free(buf); return 0; }
  int64_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += buf[i];
  int mean = sum / (int64_t)n, peak = 1;
  for (size_t i = 0; i < n; i++) peak = max(peak, abs(buf[i] - mean));
  int gain = min(64 * 256, 23000 * 256 / peak);
  for (size_t i = 0; i < n; i++) buf[i] = constrain((buf[i] - mean) * gain / 256, -32767, 32767);
  Serial.printf("mic: %u ms recorded, peak %d, gain %.1fx\n", (unsigned)(n * 1000 / RATE), peak, gain / 256.0);
  *pcm = buf;
  return n;
}

static bool writeWav(const String &path, const int16_t *pcm, size_t n) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  uint32_t data = n * 2, rate = RATE, bytesPerSec = RATE * 2;
  uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a',0,0,0,0};
  uint32_t riff = 36 + data;
  memcpy(h + 4, &riff, 4); memcpy(h + 24, &rate, 4); memcpy(h + 28, &bytesPerSec, 4); memcpy(h + 40, &data, 4);
  bool ok = f.write(h, 44) == 44;
  const uint8_t *p = (const uint8_t *)pcm;
  for (size_t off = 0; ok && off < data; off += 4096) { size_t n = min<size_t>(4096, data - off); ok = f.write(p + off, n) == n; }   // a short write: the flash is full
  f.close();
  if (!ok) LittleFS.remove(path);
  return ok;
}

int captureRecord(bool (*keepGoing)(), void (*atLimit)(), String &path, String &id) {
  int16_t *pcm;
  size_t n = micRecord(keepGoing, atLimit, &pcm);
  if (n < (size_t)RATE / 2) { free(pcm); if (n || !*why) why = "Too short"; return 0; }   // under half a second
  id = notesNewId();
  path = "/queue/" + id + ".wav";
  uint32_t t = millis();
  if (!writeWav(path, pcm, n)) { free(pcm); why = "Couldn't save the recording"; return 0; }
  free(pcm);
  Serial.printf("capture: %s, %u s, written in %lu ms\n", path.c_str(), (unsigned)(n / RATE), (unsigned long)(millis() - t));
  return (n + RATE / 2) / RATE;
}
