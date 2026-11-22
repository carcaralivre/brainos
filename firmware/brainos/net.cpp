#include "net.h"
extern const char *const VERSION;
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_sntp.h>
#include "config.h"
#include "notes.h"
#include "rtc.h"

// ESP-IDF's bundle of public root CAs, in the core's mbedtls library.
extern const uint8_t caBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t caBundleEnd[] asm("_binary_x509_crt_bundle_end");

static uint32_t lastTouch;       // millis() of the last interaction
static bool busy;                // a request in flight
static bool radioOn;             // what we asked for
static int tryIndex = -1;        // the saved network being tried, -1: none
static uint32_t tryAt, backoffUntil;
static bool wasUp, clockStarted, rtcSynced, mdnsOn;
static volatile uint8_t lastReason;

bool netUp() { return WiFi.status() == WL_CONNECTED; }
bool timeKnown() { return time(nullptr) > 1700000000; }
void netTouch() { lastTouch = millis(); }
void netSetBusy(bool b) { busy = b; }
String netName() { return netUp() ? WiFi.SSID() : String(); }
String netAddress() { return netUp() ? WiFi.localIP().toString() : String(); }
int netSignal() { return netUp() ? WiFi.RSSI() : 0; }

bool netWanted() {
  long idleMin = cfg::num("wifi.idle_min");
  if (idleMin <= 0) return true;                         // 0: always on
  if (notesPending() && cfg::isSet("stt.api_key")) return true;
  return millis() - lastTouch < (uint32_t)idleMin * 60000UL;
}

static void onWifiEvent(WiFiEvent_t e, WiFiEventInfo_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) lastReason = info.wifi_sta_disconnected.reason;
}

void netBegin() {
  WiFi.persistent(false);  // the networks live in the settings, not in the driver's flash
  WiFi.onEvent(onWifiEvent);
  WiFi.setHostname("brain");
  lastTouch = millis();
}

// The time servers as addresses, so SNTP never does DNS from lwIP's core (upstream's finding).
static void startClock() {
  static char servers[2][46];
  static const char *const NAMES[2] = {"pool.ntp.org", "time.cloudflare.com"};
  esp_sntp_stop();
  for (int i = 0; i < 2; i++) {
    IPAddress ip;
    if (WiFi.hostByName(NAMES[i], ip) == 1) strlcpy(servers[i], ip.toString().c_str(), sizeof servers[i]);
    else strlcpy(servers[i], NAMES[i], sizeof servers[i]);
  }
  static String tz;
  tz = getenv("TZ") ? getenv("TZ") : "UTC0";
  configTzTime(tz.c_str(), servers[0], servers[1]);
  clockStarted = true;
  rtcSynced = false;
}

static void joined() {
  Serial.printf("wifi: connected to %s, %s, %d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  startClock();
  if (!mdnsOn) {
    mdnsOn = MDNS.begin("brain");
    if (mdnsOn) MDNS.addService("http", "tcp", 80);
    Serial.printf("mdns: %s\n", mdnsOn ? "brain.local" : "FAILED");
  }
}

static void radio(bool on) {
  if (on == radioOn) return;
  radioOn = on;
  if (on) { WiFi.mode(WIFI_STA); tryIndex = -1; backoffUntil = 0; }
  else {
    Serial.println("wifi: off");
    if (mdnsOn) { MDNS.end(); mdnsOn = false; }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    tryIndex = -1;
  }
}

void netTick() {
  bool want = netWanted();
  if (!want && !busy) { radio(false); wasUp = false; return; }
  if (want) radio(true);
  bool up = netUp();
  if (up && !wasUp) joined();
  if (!up && wasUp) Serial.printf("wifi: dropped (reason %d)\n", lastReason);
  wasUp = up;
  if (up) {
    if (clockStarted && !rtcSynced && timeKnown()) { rtcFromSystem(); rtcSynced = true; Serial.println("rtc: set from the network"); }
    tryIndex = -1;
    return;
  }
  // Not connected: try the saved networks in turn, 10 s each, then wait a minute.
  if (millis() < backoffUntil) return;
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  if (nets.empty()) { backoffUntil = millis() + 30000; return; }
  if (tryIndex >= 0 && millis() - tryAt < 12000) return;   // still trying one
  tryIndex++;
  if (tryIndex >= (int)nets.size()) {
    tryIndex = -1;
    backoffUntil = millis() + 60000;
    Serial.println("wifi: no saved network answered, trying again in a minute");
    WiFi.disconnect();
    return;
  }
  Serial.printf("wifi: trying %s\n", nets[tryIndex].ssid.c_str());
  WiFi.disconnect(false, true);   // the driver refuses a new config while still trying the last network
  delay(100);
  lastReason = 0;
  WiFi.begin(nets[tryIndex].ssid.c_str(), nets[tryIndex].pass.c_str());
  tryAt = millis();
}

// ---------------------------------------------------------------- scan and join (settings)

bool netScan(std::vector<NetSeen> &out, String &why) {
  out.clear();
  radio(true);
  int n = -2;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt) delay(700);
    WiFi.scanDelete();
    n = WiFi.scanNetworks(false, false);
  }
  Serial.printf("wifi: scan, %d networks\n", n);
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool dup = false;
    for (NetSeen &o : out) if (o.ssid == ssid) { o.rssi = max(o.rssi, (int)WiFi.RSSI(i)); dup = true; }
    if (!dup) out.push_back({ssid, (int)WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(out.begin(), out.end(), [](const NetSeen &a, const NetSeen &b) { return a.rssi > b.rssi; });
  if (out.size() > 20) out.resize(20);
  if (n < 0) return why = "The scan failed", false;
  return true;
}

static String reasonText(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND: return "Network not found";
    case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Wrong password";
    case WIFI_REASON_ASSOC_TOOMANY: return "The network is full";
    case 0: return "No answer";
    default: return String("Turned away (") + (int)r + ")";
  }
}

bool netJoin(const String &ssid, const String &pass, String &why, uint32_t timeoutMs) {
  radio(true);
  WiFi.disconnect();
  delay(100);
  lastReason = 0;
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (!netUp() && millis() - start < timeoutMs) delay(50);
  bool ok = netUp();
  if (!ok) { why = reasonText(lastReason); WiFi.disconnect(); }
  tryIndex = -1;
  Serial.printf("wifi: join %s: %s\n", ssid.c_str(), ok ? "ok" : why.c_str());
  return ok;
}

// ---------------------------------------------------------------- TLS and replies

void netTrust(WiFiClientSecure &tls) {
  tls.setCACertBundle(caBundleStart, caBundleEnd - caBundleStart);
  tls.setHandshakeTimeout(NET_HANDSHAKE_S);
}

class CapSink : public Stream {
 public:
  CapSink(String &out, size_t max) : out(out), max(max) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    if (out.length() + n > max) return over = true, 0;
    out.concat((const char *)b, n);
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  String &out;
  size_t max;
  bool over = false;
};

bool netReadBody(HTTPClient &http, String &out, size_t max) {
  out = "";
  int size = http.getSize();
  if (size > (int)max) return false;
  if (size > 0) out.reserve(size);
  CapSink sink(out, max);
  int n = http.writeToStream(&sink);
  return n >= 0 && !sink.over;
}

String netFailText(const char *who, int code) {
  switch (code) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return String("Couldn't reach ") + who;
    case HTTPC_ERROR_READ_TIMEOUT: return String(who) + " was too slow";
    case HTTPC_ERROR_CONNECTION_LOST: case HTTPC_ERROR_NOT_CONNECTED: return String("The connection to ") + who + " broke";
    case 401: case 403: return String(who) + " rejected the key";
    case 402: return String(who) + ": no credit";
    case 404: return String(who) + ": not found";
    case 429: return String(who) + ": too many requests";
  }
  if (code < 0) return String("Couldn't reach ") + who;
  if (code >= 500) return String(who) + " is down (" + code + ")";
  return String(who) + " said " + code;
}

// ---------------------------------------------------------------- a POST by hand

static bool readLine(WiFiClientSecure &tls, String &line, uint32_t deadline) {
  line = "";
  while (millis() < deadline) {
    if (!tls.available()) { if (!tls.connected()) return line.length() > 0; delay(5); continue; }
    char c = tls.read();
    if (c == '\n') return true;
    if (c != '\r') line += c;
    if (line.length() > 4000) return false;
  }
  return false;
}

int netPost(const char *host, const char *path, const String &headers, const uint8_t *body, size_t len, String &reply, uint32_t timeoutMs) {
  reply = "";
  WiFiClientSecure tls;
  netTrust(tls);
  if (!tls.connect(host, 443, 15000)) { reply = "connect failed"; return HTTPC_ERROR_CONNECTION_REFUSED; }
  String h = String("POST ") + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: brainos/" + VERSION + "\r\nConnection: close\r\nAccept: application/json\r\n" + headers + "Content-Length: " + String(len) + "\r\n\r\n";
  if (tls.write((const uint8_t *)h.c_str(), h.length()) != h.length()) { tls.stop(); reply = "header send failed"; return HTTPC_ERROR_SEND_HEADER_FAILED; }
  size_t sent = 0;
  while (sent < len) {   // 1 KB at a time: larger writes get the connection reset on this core
    size_t w = tls.write(body + sent, min<size_t>(1024, len - sent));
    if (!w) { tls.stop(); reply = "body send failed"; return HTTPC_ERROR_SEND_PAYLOAD_FAILED; }
    sent += w;
    if ((sent & 0xFFFF) == 0) delay(1);
  }
  uint32_t deadline = millis() + timeoutMs;
  String line;
  if (!readLine(tls, line, deadline) || !line.startsWith("HTTP/1.")) { tls.stop(); reply = "no status line"; return HTTPC_ERROR_READ_TIMEOUT; }
  int code = line.substring(9, 12).toInt();
  bool chunked = false; long contentLength = -1;
  while (readLine(tls, line, deadline) && line.length()) {
    String l = line; l.toLowerCase();
    if (l.startsWith("transfer-encoding:") && l.indexOf("chunked") >= 0) chunked = true;
    if (l.startsWith("content-length:")) contentLength = l.substring(15).toInt();
  }
  const size_t MAX = 65536;
  if (chunked) {
    for (;;) {
      if (!readLine(tls, line, deadline)) break;
      long n = strtol(line.c_str(), nullptr, 16);
      if (n <= 0) break;
      while (n > 0 && millis() < deadline) {
        if (!tls.available()) { delay(5); continue; }
        int c = tls.read(); if (c < 0) break;
        if (reply.length() < MAX) reply += (char)c;
        n--;
      }
      readLine(tls, line, deadline);  // the CRLF after the chunk
    }
  } else {
    while (millis() < deadline && (contentLength < 0 || (long)reply.length() < contentLength)) {
      if (!tls.available()) { if (!tls.connected()) break; delay(5); continue; }
      int c = tls.read(); if (c < 0) break;
      if (reply.length() < MAX) reply += (char)c;
    }
  }
  tls.stop();
  return code;
}
