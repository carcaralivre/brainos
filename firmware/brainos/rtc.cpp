#include "rtc.h"
#include <Arduino.h>
#include <sys/time.h>
#include "board.h"
#include "pins.h"

// The chip keeps UTC; the display converts with the TZ the sketch sets.
static i2c_master_dev_handle_t rtc;
static bool valid;

static uint8_t bcd(int v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

bool rtcBegin() {
  if (!sensorBus) return false;
  rtc = i2cDevice(sensorBus, ADDR_PCF8563);
  struct tm t;
  bool ok = rtcRead(t);
  Serial.printf("rtc: %s%s\n", ok ? "ok" : "MISSING", ok && !valid ? " (time lost: set it)" : "");
  return ok;
}

bool rtcValid() { return valid; }

bool rtcRead(struct tm &t) {
  uint8_t reg = 0x02, r[7];
  if (!rtc || i2c_master_transmit_receive(rtc, &reg, 1, r, 7, 100) != ESP_OK) return false;
  valid = !(r[0] & 0x80);  // VL: voltage low since the last set
  t = {};
  t.tm_sec = unbcd(r[0] & 0x7F);
  t.tm_min = unbcd(r[1] & 0x7F);
  t.tm_hour = unbcd(r[2] & 0x3F);
  t.tm_mday = unbcd(r[3] & 0x3F);
  t.tm_wday = r[4] & 7;
  t.tm_mon = unbcd(r[5] & 0x1F) - 1;
  t.tm_year = unbcd(r[6]) + 100;  // 20xx
  return true;
}

bool rtcWrite(const struct tm &t) {
  uint8_t w[8] = {0x02, bcd(t.tm_sec), bcd(t.tm_min), bcd(t.tm_hour), bcd(t.tm_mday), (uint8_t)t.tm_wday, bcd(t.tm_mon + 1), bcd(t.tm_year % 100)};
  if (!rtc || i2c_master_transmit(rtc, w, 8, 100) != ESP_OK) return false;
  valid = true;
  return true;
}

// mktime/localtime in UTC whatever TZ is: swap it for the call.
static time_t utcFrom(struct tm &t) {
  const char *tz = getenv("TZ");
  String keep = tz ? tz : "";
  setenv("TZ", "UTC0", 1); tzset();
  time_t secs = mktime(&t);
  if (tz) setenv("TZ", keep.c_str(), 1); else unsetenv("TZ");
  tzset();
  return secs;
}
static void utcTo(time_t secs, struct tm &t) { gmtime_r(&secs, &t); }

void rtcToSystem() {
  struct tm t;
  if (!rtcRead(t) || !valid) return;
  struct timeval tv = {utcFrom(t), 0};
  settimeofday(&tv, nullptr);
}

void rtcFromSystem() {
  struct tm t;
  utcTo(time(nullptr), t);
  rtcWrite(t);
}
