// The board's settings: Wi-Fi networks, the OpenRouter key, the name, the time zone, the
// timings. Kept in NVS (one namespace), never in the source. Changed from the board's own
// screens (Wi-Fi) and from the brain.local page (everything else).
//
// Secrets (the key, the Wi-Fi passwords) leave this module only through str() and
// wifiNetworks(), for the code that uses them: never in a listing, never in a log line.
//
// Threads: the getters and setters take a lock, so the transcription worker can read the
// key while the loop changes a setting.
#pragma once
#include <Arduino.h>
#include <vector>

struct WifiCred {
  String ssid, pass;
};

namespace cfg {

static const int MAX_WIFI = 8;

void begin();   // once, early in setup(), before anything reads a value

// A value falls back to the key's default when it is not set; an unknown name logs once and
// reads as empty / 0 / false.
bool isSet(const char *name);
String str(const char *name);
long num(const char *name);
bool flag(const char *name);
std::vector<WifiCred> wifiNetworks();

// Checked, written at once, live at once. false: `err` says what was wrong.
bool setStr(const char *name, const String &value, String &err);
bool setNum(const char *name, long value, String &err);
bool setFlag(const char *name, bool value, String &err);
bool setWifi(const std::vector<WifiCred> &list, String &err);   // the whole list, in order

}  // namespace cfg
