#include "cost.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include <vector>
#include "dates.h"

static const char *LOG = "/costs.log";   // "YYYYMMDD micro" a line, oldest first
static const size_t KEEP_DAYS = 400;
static std::vector<std::pair<uint32_t, uint32_t>> days;   // (YYYYMMDD, micro)

static struct tm localDay(int daysAgo) {   // noon of that day, so a DST change can't move it to another date
  time_t t = time(nullptr);
  struct tm tm; localtime_r(&t, &tm);
  tm.tm_hour = 12; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_mday -= daysAgo; tm.tm_isdst = -1;
  mktime(&tm);
  return tm;
}
static uint32_t ymd(const struct tm &tm) { return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday; }

void costBegin() {
  { Preferences p; if (p.begin("cost", false)) p.end(); }   // made once, so the read-only opens below find it (and log nothing)
  days.clear();
  File f = LittleFS.open(LOG, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    unsigned long d, m;
    if (sscanf(line.c_str(), "%lu %lu", &d, &m) == 2) days.push_back({(uint32_t)d, (uint32_t)m});
  }
  f.close();
}

static void saveLog() {
  File f = LittleFS.open("/costs.tmp", "w");
  if (!f) return;
  bool ok = true;
  for (auto &d : days) {
    char line[32]; int n = snprintf(line, sizeof line, "%lu %lu\n", (unsigned long)d.first, (unsigned long)d.second);
    if (f.write((const uint8_t *)line, n) != (size_t)n) { ok = false; break; }
  }
  f.close();
  if (ok) LittleFS.rename("/costs.tmp", LOG);   // the flash full: the old log stays whole
  else LittleFS.remove("/costs.tmp");
}

static String monthKey(const struct tm &tm) { char k[24]; snprintf(k, sizeof k, "m%04d%02d", tm.tm_year + 1900, tm.tm_mon + 1); return k; }

void costAdd(uint32_t micro) {
  if (!micro) return;
  Preferences p;
  if (p.begin("cost", false)) {
    p.putULong64("total", p.getULong64("total", 0) + micro);
    if (clockKnown()) { String k = monthKey(localDay(0)); p.putULong64(k.c_str(), p.getULong64(k.c_str(), 0) + micro); }
    p.end();
  }
  if (!clockKnown()) return;   // no date to put it under: only the total has it
  uint32_t today = ymd(localDay(0));
  if (!days.empty() && days.back().first == today) days.back().second += micro;
  else days.push_back({today, micro});
  while (days.size() > KEEP_DAYS) days.erase(days.begin());
  saveLog();
}

uint32_t costTranscription(const String &model, int secs) {
  // US dollars a minute, in millionths (OpenAI's list prices; the Whisper ones as hosted on OpenRouter, about).
  uint32_t perMin = model.indexOf("gpt-4o-mini-transcribe") >= 0 ? 3000
                  : model.indexOf("gpt-4o-transcribe") >= 0 ? 6000
                  : model.indexOf("whisper-large-v3-turbo") >= 0 ? 700
                  : model.indexOf("whisper-large-v3") >= 0 ? 1850
                  : 6000;   // whisper-1, and anything unknown
  return (uint32_t)((uint64_t)perMin * secs / 60);
}

String costText(uint64_t micro) {
  if (!micro) return "US$ 0";
  char b[24];
  if (micro < 1000) snprintf(b, sizeof b, "US$ %.4f", micro / 1e6);   // under a tenth of a cent: one more digit
  else snprintf(b, sizeof b, "US$ %.3f", micro / 1e6);
  if (micro >= 1000000) snprintf(b, sizeof b, "US$ %.2f", micro / 1e6);
  return b;
}

static uint64_t between(uint32_t from, uint32_t to) {
  uint64_t s = 0;
  for (auto &d : days) if (d.first >= from && d.first <= to) s += d.second;
  return s;
}

uint64_t costDaysAgo(int n) { if (!clockKnown()) return 0; uint32_t d = ymd(localDay(n)); return between(d, d); }

static int sinceMonday() { struct tm t = localDay(0); return (t.tm_wday + 6) % 7; }
uint64_t costWeeksAgo(int n) {
  if (!clockKnown()) return 0;
  int start = sinceMonday() + 7 * n;
  return between(ymd(localDay(start)), ymd(localDay(start - 6 < 0 ? 0 : start - 6)));
}

static struct tm monthStart(int n) { struct tm t = localDay(0); t.tm_mday = 1; t.tm_mon -= n; t.tm_isdst = -1; mktime(&t); return t; }
uint64_t costMonthsAgo(int n) {
  if (!clockKnown()) return 0;
  Preferences p;
  if (!p.begin("cost", true)) return 0;
  uint64_t v = p.getULong64(monthKey(monthStart(n)).c_str(), 0);
  p.end();
  return v;
}

uint64_t costTotal() {
  Preferences p;
  if (!p.begin("cost", true)) return 0;
  uint64_t v = p.getULong64("total", 0);
  p.end();
  return v;
}

String costDayLabel(int n) { struct tm t = localDay(n); return String(monthShort(t.tm_mon)) + " " + t.tm_mday; }
String costWeekLabel(int n) { struct tm t = localDay(sinceMonday() + 7 * n); return String(monthShort(t.tm_mon)) + " " + t.tm_mday; }
String costMonthLabel(int n) { struct tm t = monthStart(n); return String(monthShort(t.tm_mon)) + " " + (t.tm_year + 1900); }
