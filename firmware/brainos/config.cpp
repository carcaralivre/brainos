#include "config.h"
#include <Preferences.h>

void logf(const char *fmt, ...);  // the sketch: a line on the serial port

enum CfgType : uint8_t { CFG_STR, CFG_INT, CFG_BOOL, CFG_WIFI };
enum CfgCheck : uint8_t { CHK_TEXT, CHK_TOKEN, CHK_URL, CHK_TZ };   // how a string is checked before it is accepted

struct CfgKey {
  const char *name;   // "stt.model"
  const char *nvs;    // the NVS key, at most 15 characters
  CfgType type;
  bool secret;        // never logged
  CfgCheck check;     // CFG_STR only
  int32_t min, max;   // CFG_INT: range; CFG_STR: length in bytes
  const char *def;    // default when not set
};

// Defaults are what a new board does without being told.
static const CfgKey KEYS[] = {
  // name               nvs          type       secret check      min  max    default
  {"wifi.networks",     "wifi",      CFG_WIFI,  true,  CHK_TEXT,  0,   0,     ""},
  {"device.name",       "dev_name",  CFG_STR,   false, CHK_TEXT,  0,   40,    "BrainOS"},
  {"device.timezone",   "dev_tz",    CFG_STR,   false, CHK_TZ,    3,   63,    "<-03>3"},
  {"stt.api_key",       "stt_key",   CFG_STR,   true,  CHK_TOKEN, 20,  256,   ""},
  {"stt.model",         "stt_model", CFG_STR,   false, CHK_TOKEN, 1,   64,    "openai/whisper-large-v3"},
  {"stt.language",      "stt_lang",  CFG_STR,   false, CHK_TOKEN, 0,   5,     ""},
  {"wifi.idle_min",     "wifi_idle", CFG_INT,   false, CHK_TEXT,  0,   1440,  "0"},     // minutes of Wi-Fi after the last interaction; 0: always on
  {"sleep.min",         "sleep_min", CFG_INT,   false, CHK_TEXT,  0,   1440,  "30"},    // minutes on battery without a touch before deep sleep; 0: never
  {"export.archive",    "exp_arch",  CFG_BOOL,  false, CHK_TEXT,  0,   0,     "true"},
  {"sound",             "sound",     CFG_BOOL,  false, CHK_TEXT,  0,   0,     "true"},
  {"update.url",        "upd_url",   CFG_STR,   false, CHK_URL,   0,   200,   ""},      // release feed; empty: GitHub
};
static const size_t N = sizeof(KEYS) / sizeof(KEYS[0]);
static const char *NS = "pcfg0";   // the namespace the earlier versions wrote: nothing to migrate

static String val[N];
static bool isset[N];
static std::vector<WifiCred> wifi;
static SemaphoreHandle_t mtx;
struct Locked {
  Locked() { if (mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Locked() { if (mtx) xSemaphoreGive(mtx); }
};

static int indexOf(const char *name) {
  for (size_t i = 0; i < N; i++) if (!strcmp(KEYS[i].name, name)) return i;
  logf("config: no key %s\n", name);
  return -1;
}

// ---- the Wi-Fi list <-> NVS blob: per entry, u8 length + SSID, u8 length + password

static std::vector<uint8_t> packWifi(const std::vector<WifiCred> &list) {
  std::vector<uint8_t> b;
  for (const WifiCred &w : list) {
    b.push_back(w.ssid.length());
    b.insert(b.end(), w.ssid.c_str(), w.ssid.c_str() + w.ssid.length());
    b.push_back(w.pass.length());
    b.insert(b.end(), w.pass.c_str(), w.pass.c_str() + w.pass.length());
  }
  return b;
}

static std::vector<WifiCred> unpackWifi(const uint8_t *b, size_t n) {
  std::vector<WifiCred> out;
  size_t i = 0;
  while (i < n && out.size() < (size_t)cfg::MAX_WIFI) {
    WifiCred w;
    for (String *s : {&w.ssid, &w.pass}) {
      if (i >= n) return out;
      size_t len = b[i++];
      if (len > 64 || i + len > n) return out;  // a damaged blob: an SSID is 32 bytes at most, a password 64
      char tmp[65];
      memcpy(tmp, b + i, len);
      tmp[len] = 0;
      *s = tmp;
      i += len;
    }
    out.push_back(w);
  }
  return out;
}

// ---- checks

static bool validUtf8(const String &s) {
  const uint8_t *p = (const uint8_t *)s.c_str();
  size_t n = s.length();
  for (size_t i = 0; i < n;) {
    uint8_t c = p[i];
    if (c < 0x20 || c == 0x7F) return false;
    int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0 || i + extra >= n) return false;
    for (int j = 1; j <= extra; j++) if ((p[i + j] & 0xC0) != 0x80) return false;
    i += extra + 1;
  }
  return true;
}

static bool printableAscii(const String &s) {
  for (size_t i = 0; i < s.length(); i++) if (s[i] <= 0x20 || s[i] >= 0x7F) return false;
  return true;
}

static bool checkString(const CfgKey &k, const String &v, String &err) {
  if ((int32_t)v.length() < k.min || (int32_t)v.length() > k.max) { err = String(k.name) + ": " + k.min + ".." + k.max + " bytes"; return false; }
  if (!v.length()) return true;   // empty is "not configured" for an optional key
  bool ok = true;
  switch (k.check) {
    case CHK_TEXT: ok = validUtf8(v); break;
    case CHK_TOKEN: ok = printableAscii(v); break;
    case CHK_URL: ok = printableAscii(v) && (v.startsWith("http://") || v.startsWith("https://")) && v.length() > 8; break;
    case CHK_TZ: ok = printableAscii(v) && (isalpha((unsigned char)v[0]) || v[0] == '<'); break;
  }
  if (!ok) err = String(k.name) + ": not a valid value";
  return ok;
}

static bool validPassword(const String &p) {
  if (!p.length()) return true;   // an open network
  if (p.length() >= 8 && p.length() <= 63) return true;
  if (p.length() == 64) { for (size_t i = 0; i < 64; i++) if (!isxdigit((unsigned char)p[i])) return false; return true; }   // a raw PSK
  return false;
}

// ---- storage

static bool write(size_t i, String &err) {   // val[i] / wifi -> NVS
  Preferences p;
  if (!p.begin(NS, false)) return err = "storage unavailable", false;
  const CfgKey &k = KEYS[i];
  bool ok = true;
  switch (k.type) {
    case CFG_STR: ok = p.putString(k.nvs, val[i]) == val[i].length(); break;
    case CFG_INT: ok = p.putInt(k.nvs, val[i].toInt()) == 4; break;
    case CFG_BOOL: ok = p.putBool(k.nvs, val[i] == "true") == 1; break;
    case CFG_WIFI: {
      std::vector<uint8_t> b = packWifi(wifi);
      if (b.empty()) p.remove(k.nvs); else ok = p.putBytes(k.nvs, b.data(), b.size()) == b.size();
      break;
    }
  }
  p.end();
  if (!ok) err = String("could not write ") + k.name;
  else logf("config: %s %s\n", k.name, k.secret ? "set" : val[i].c_str());
  return ok;
}

namespace cfg {

void begin() {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  Preferences p;
  if (!p.begin(NS, true)) { logf("config: nothing stored yet\n"); return; }
  for (size_t i = 0; i < N; i++) {
    const CfgKey &k = KEYS[i];
    if (!p.isKey(k.nvs)) continue;
    isset[i] = true;
    switch (k.type) {
      case CFG_STR: val[i] = p.getString(k.nvs, ""); break;
      case CFG_INT: val[i] = String(p.getInt(k.nvs, 0)); break;
      case CFG_BOOL: val[i] = p.getBool(k.nvs, false) ? "true" : "false"; break;
      case CFG_WIFI: {
        size_t n = p.getBytesLength(k.nvs);
        std::vector<uint8_t> b(n);
        if (n) p.getBytes(k.nvs, b.data(), n);
        wifi = unpackWifi(b.data(), n);
        break;
      }
    }
  }
  p.end();
  logf("config: %u networks, key %s\n", (unsigned)wifi.size(), isSet("stt.api_key") ? "set" : "not set");
}

bool isSet(const char *name) {
  int i = indexOf(name);
  Locked l;
  return i >= 0 && isset[i];
}

String str(const char *name) {
  int i = indexOf(name);
  if (i < 0) return "";
  Locked l;
  return isset[i] ? val[i] : String(KEYS[i].def);
}

long num(const char *name) { return str(name).toInt(); }
bool flag(const char *name) { return str(name) == "true"; }
std::vector<WifiCred> wifiNetworks() { Locked l; return wifi; }

static bool put(const char *name, CfgType type, const String &text, String &err) {
  int i = indexOf(name);
  if (i < 0) return err = String("no key ") + name, false;
  const CfgKey &k = KEYS[i];
  if (k.type != type) return err = String(k.name) + ": wrong type", false;
  if (k.type == CFG_STR && !checkString(k, text, err)) return false;
  if (k.type == CFG_INT) { long v = text.toInt(); if (v < k.min || v > k.max) return err = String(k.name) + ": " + k.min + ".." + k.max, false; }
  Locked l;
  val[i] = text; isset[i] = true;
  return write(i, err);
}

bool setStr(const char *name, const String &value, String &err) { return put(name, CFG_STR, value, err); }
bool setNum(const char *name, long value, String &err) { return put(name, CFG_INT, String(value), err); }
bool setFlag(const char *name, bool value, String &err) { return put(name, CFG_BOOL, value ? "true" : "false", err); }

bool setWifi(const std::vector<WifiCred> &list, String &err) {
  if (list.size() > (size_t)MAX_WIFI) return err = String("at most ") + MAX_WIFI + " networks", false;
  for (const WifiCred &w : list) {
    if (!w.ssid.length() || w.ssid.length() > 32 || !validUtf8(w.ssid)) return err = "a network name is 1 to 32 characters", false;
    if (!validPassword(w.pass)) return err = "a password is 8 to 63 characters", false;
  }
  int i = indexOf("wifi.networks");
  Locked l;
  wifi = list; isset[i] = !list.empty();
  return write(i, err);
}

}  // namespace cfg
