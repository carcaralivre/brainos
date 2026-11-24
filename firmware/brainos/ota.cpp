#include "ota.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "glyphs.h"
#include "mbedtls/sha256.h"
#include "net.h"
#include "semver.h"

static String fmt(const char *f, ...) {
  char b[200];
  va_list ap; va_start(ap, f); vsnprintf(b, sizeof(b), f, ap); va_end(ap);
  return String(b);
}

#ifndef STICKY_VERSION  // build.sh passes it: the tag in CI, dev-<commit> otherwise
#define STICKY_VERSION dev-unknown
#endif
#define UPD_STR2(x) #x
#define UPD_STR(x) UPD_STR2(x)

static const char *const GITHUB_FEED = "https://api.github.com/repos/carcaralivre/brainos/releases/latest";
static const char *const ASSET = "brainos.bin";
static const size_t FEED_MAX = 96 * 1024;  // the release JSON, in PSRAM; the assets come before the long parts

// The core checks a new firmware at start-up and keeps it at once unless
// this says the sketch will (updMarkGood).
extern "C" bool verifyRollbackLater() { return true; }

static UpdInfo info;      // read and written on the loop task only
static bool pending;      // this firmware boots on trial
// What updBegin() found at start-up, put in words when it's read (updInfo).
static uint8_t bootNote;  // 1: updated to bootTag, 2: bootTag didn't start
static String bootTag;
// What the last check found to install.
static String binUrl, shaUrl, shaHex, binTag;
static size_t binSize;
// NVS "brainos_ota": "try" is the version an install restarted into, so the next start can
// say whether it kept running.
static const char *const NVS_NS = "brainos_ota";

const char *updVersion() { return UPD_STR(STICKY_VERSION); }

static bool updIsDev() {
  long v[3];
  return !strncmp(updVersion(), "dev", 3) || !parseVersion(updVersion(), v);
}

void updBegin() {
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  pending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  Serial.printf("update: %s running from %s at 0x%lx%s\n", updVersion(), run ? run->label : "?",
                run ? (unsigned long)run->address : 0UL, pending ? ", new: on trial until it runs well" : "");
  // Did the last install keep running? If this isn't the version it
  // restarted into, the new one went back: remember not to try it again.
  Preferences p;
  String tried;
  bool failed = false;
  if (p.begin(NVS_NS, false)) {
    tried = p.getString("try", "");
    if (tried.length()) {
      p.remove("try");
      if (tried == updVersion()) {
        Serial.printf("update: now running %s, as installed\n", tried.c_str());
        bootNote = 1, bootTag = tried;
      } else {
        Serial.printf("update: %s didn't keep running: back on %s\n", tried.c_str(), updVersion());
        bootNote = 2, bootTag = tried;
        failed = true;
      }
    }
    p.end();
  }
  info.state = UPD_IDLE;
  info.status = "";  // until a check or an install says more: the boot's note (updInfo)
  info.failed = failed;
}

bool updPendingVerify() { return pending; }

void updMarkGood(const char *why) {
  if (!pending) return;
  pending = false;
  esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  Serial.printf("update: %s kept (%s): %s\n", updVersion(), why, esp_err_to_name(e));
}

static void setStatus(uint8_t state, const String &status, bool failed);
void updNoWifi() { setStatus(UPD_IDLE, "No Wi-Fi", true); info.checkedMs = millis(); }

UpdInfo updInfo() {
  UpdInfo r = info;
  if (!r.status.length() && r.state == UPD_IDLE)
    r.status = bootNote == 1 ? fmt("Updated to %s", bootTag.c_str())
               : bootNote == 2 ? fmt("%s didn't start: back on %s", bootTag.c_str(), updVersion())
                               : String("Not checked yet");
  return r;
}


static void setStatus(uint8_t state, const String &status, bool failed) {
  info.state = state;
  info.status = status;
  info.failed = failed;
}

// ---------------------------------------------------------------- HTTP

// Where an HTTP body goes: start() hears the Content-Length (-1: unknown)
// and can refuse it; put() gets the bytes. Either sets `why` on refusing.
class Sink : public Stream {
 public:
  String why;
  virtual bool start(int length) { return true; }
  virtual bool put(const uint8_t *b, size_t n) = 0;
  size_t write(const uint8_t *b, size_t n) override { return put(b, n) ? n : 0; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// Collects up to `max` bytes in a PSRAM buffer, NUL-terminated. Past that
// it keeps what it has and refuses the rest (the caller may still use it).
class BufferSink : public Sink {
 public:
  char *buf = nullptr;
  size_t len = 0, max;
  bool full = false;
  explicit BufferSink(size_t m) : max(m) { buf = (char *)heap_caps_malloc(m + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); if (buf) buf[0] = 0; }
  ~BufferSink() { free(buf); }
  bool put(const uint8_t *b, size_t n) override {
    if (!buf) return why = "out of memory", false;
    size_t take = min(n, max - len);
    memcpy(buf + len, b, take);
    len += take;
    buf[len] = 0;
    if (take < n) return full = true, why = "too long", false;
    return true;
  }
};

static String origin(const String &url) {  // "https://host:port"
  int slash = url.indexOf('/', url.indexOf("//") + 2);
  return slash < 0 ? url : url.substring(0, slash);
}

static String hostOf(const String &url) {
  String o = origin(url);
  int at = o.indexOf("//");
  return at < 0 ? o : o.substring(at + 2);
}

// GET `url` into `sink`, following redirects by hand: HTTPClient's own
// following keeps the connection to the first host. Returns the HTTP
// status, or a negative HTTPClient error; `why` says what went wrong.
static int httpGet(String url, bool api, Sink &sink, String &why) {
  for (int hop = 0; hop < 6; hop++) {
    if (!url.startsWith("https://")) {   // the feed, the checksum and the binary: https only, redirects too
      why = "not an https address: " + hostOf(url);
      return -100;
    }
    WiFiClientSecure tls;
    netTrust(tls);  // the public CA bundle: GitHub and its download hosts; an 8 s handshake
    tls.setTimeout(15000);
    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    http.setUserAgent(String("brainos/") + updVersion());
    if (!http.begin(tls, url)) {
      why = "bad address";
      return -101;
    }
    if (api) {
      http.addHeader("Accept", "application/vnd.github+json");
      http.addHeader("X-GitHub-Api-Version", "2022-11-28");
    }
    const char *keys[] = {"Location"};
    http.collectHeaders(keys, 1);
    int code = http.GET();
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String to = http.header("Location");
      http.end();
      if (!to.length()) return why = "a redirect to nowhere", -102;
      url = to.startsWith("//") ? "https:" + to : to[0] == '/' ? origin(url) + to : to;
      Serial.printf("update: redirected to %s\n", hostOf(url).c_str());
      continue;
    }
    if (code != 200) {
      why = code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code);
      http.end();
      return code;
    }
    if (!sink.start(http.getSize())) {
      why = sink.why;
      http.end();
      return -103;
    }
    int n = http.writeToStream(&sink);
    http.end();
    if (n < 0) {
      why = sink.why.length() ? sink.why : HTTPClient::errorToString(n);
      return n;
    }
    return 200;
  }
  why = "too many redirects";
  return -104;
}

// ---------------------------------------------------------------- the release JSON
// Just enough for GitHub's release object: string and number values by
// key, and the objects of the "assets" array.

static const char *skipSpace(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  return p;
}

// The value after `"key":` in [p, end), or nullptr.
static const char *valueOf(const char *p, const char *end, const char *key) {
  char k[40];
  snprintf(k, sizeof k, "\"%s\"", key);
  size_t kl = strlen(k);
  for (const char *at = p; at + kl <= end; at++) {
    at = (const char *)memmem(at, end - at, k, kl);
    if (!at) return nullptr;
    if (at > p && at[-1] == '\\') continue;  // inside a string: \"key\"
    const char *v = skipSpace(at + kl, end);
    if (v < end && *v == ':') return skipSpace(v + 1, end);
  }
  return nullptr;
}

// A JSON string at p, decoded (UTF-8, \u escapes included), at most `cap`
// bytes, whole characters only.
static String stringAt(const char *p, const char *end, size_t cap) {
  String out;
  if (!p || p >= end || *p != '"') return out;
  auto hex4 = [&](const char *h) -> long {  // the 4 hex digits at h, -1: not there
    if (h + 4 > end) return -1;
    char d[5] = {h[0], h[1], h[2], h[3], 0};
    for (int i = 0; i < 4; i++)
      if (!isxdigit((unsigned char)d[i])) return -1;
    return strtol(d, nullptr, 16);
  };
  for (p++; p < end && *p != '"' && out.length() < cap; p++) {
    unsigned c = (unsigned char)*p;
    if (c == '\\' && p + 1 < end) {
      char e = *++p;
      if (e == 'n') out += '\n';
      else if (e == 't') out += ' ';
      else if (e == 'r') continue;
      else if (e == 'u') {
        long u = hex4(p + 1);
        if (u < 0) continue;
        p += 4;
        if (u >= 0xD800 && u < 0xDC00 && p + 2 < end && p[1] == '\\' && p[2] == 'u') {  // a surrogate pair
          long lo = hex4(p + 3);
          if (lo >= 0xDC00 && lo < 0xE000) u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00), p += 6;
        }
        if (u >= 0x20 && !(u >= 0xD800 && u < 0xE000) && out.length() + 4 <= cap) utf8Append(out, u);
      } else out += e;  // \" \\ \/
    } else if (c >= 0x80) {  // raw UTF-8: the whole character, or none of it
      int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
      if (p + len > end || out.length() + len > cap) break;
      out.concat(p, len);
      p += len - 1;
    } else if (c >= 0x20) out += (char)c;
  }
  return out;
}

static String stringOf(const char *p, const char *end, const char *key, size_t cap = 1024) {
  return stringAt(valueOf(p, end, key), end, cap);
}

// The end of the object or array that starts at p (one past its closing bracket).
static const char *closeOf(const char *p, const char *end) {
  int depth = 0;
  bool inStr = false;
  for (; p < end; p++) {
    if (inStr) {
      if (*p == '\\') p++;
      else if (*p == '"') inStr = false;
    } else if (*p == '"') inStr = true;
    else if (*p == '{' || *p == '[') depth++;
    else if (*p == '}' || *p == ']') {
      if (--depth == 0) return p + 1;
    }
  }
  return nullptr;
}


static bool isHex64(const String &s) {
  if (s.length() != 64) return false;
  for (char c : s)
    if (!isxdigit((unsigned char)c)) return false;
  return true;
}

// ---------------------------------------------------------------- check

bool updCheck() {
  setStatus(UPD_CHECKING, "Checking…", false);
  Serial.println("update: checking GitHub");

  BufferSink json(FEED_MAX);
  String why;
  int code = httpGet(GITHUB_FEED, true, json, why);
  if (code != 200 && !(json.full && json.len)) {
    String status;
    if (code == 404) status = "No release published yet";
    else if (code == 403 || code == 429) status = "GitHub is busy (rate limit): try later";
    else status = netFailText("GitHub", code);
    Serial.printf("update: check failed: %s (%s)\n", why.c_str(), status.c_str());
    info.state = UPD_IDLE;
    info.status = status;
    info.failed = true;
    info.checkedMs = millis();
    // 404: the feed answered, there is just nothing: forget an older answer.
    if (code == 404) info.latest = "", info.available = false;
    return code > 0;
  }
  const char *b = json.buf, *end = json.buf + json.len;
  String tag = stringOf(b, end, "tag_name", 40), url, sha, shaAsset;
  size_t size = 0;
  const char *assets = valueOf(b, end, "assets");
  if (assets && *assets == '[') {
    const char *stop = closeOf(assets, end);
    if (!stop) stop = end;
    for (const char *p = assets + 1; p < stop;) {
      p = skipSpace(p, stop);
      if (p >= stop || *p != '{') {
        p++;
        continue;
      }
      const char *close = closeOf(p, stop);
      if (!close) break;
      String name = stringOf(p, close, "name", 64);
      if (name == ASSET) {
        url = stringOf(p, close, "browser_download_url", 1024);
        const char *s = valueOf(p, close, "size");
        size = s ? strtoul(s, nullptr, 10) : 0;
        String digest = stringOf(p, close, "digest", 80);  // GitHub's own: "sha256:<hex>"
        if (digest.startsWith("sha256:")) sha = digest.substring(7);
      } else if (name == String(ASSET) + ".sha256") {
        shaAsset = stringOf(p, close, "browser_download_url", 1024);
      }
      p = close;
    }
  }

  long v[3];
  if (!tag.length() || !parseVersion(tag.c_str(), v)) {
    Serial.printf("update: the answer has no version (tag \"%s\", %u bytes)\n", tag.c_str(), (unsigned)json.len);
    setStatus(UPD_IDLE, "The release feed's answer wasn't understood", true);
    return false;
  }
  int cmp = updCompare(tag.c_str(), updVersion());
  bool newer = !updIsDev() && cmp > 0;
  bool installable = url.length() && size > 0 && (updIsDev() || cmp > 0);
  String status;
  if ((newer || updIsDev()) && !url.length()) status = fmt("%s has no %s to install", tag.c_str(), ASSET);
  else if (newer) status = fmt("%s is available", tag.c_str());
  else if (updIsDev()) status = fmt("%s is the latest release (this is a dev build)", tag.c_str());
  else status = "You're up to date";
  Serial.printf("update: latest %s (this is %s), %s %u bytes, checksum %s: %s\n", tag.c_str(), updVersion(),
                url.length() ? "brainos.bin" : "no brainos.bin,", (unsigned)size,
                shaAsset.length() ? "file" : sha.length() ? "digest" : "none", status.c_str());

  binUrl = installable ? url : String();
  binSize = installable ? size : 0;
  shaUrl = shaAsset;
  shaHex = sha;
  binTag = tag;
  info.state = UPD_IDLE;
  info.status = status;
  info.failed = (newer || updIsDev()) && !url.length();
  info.latest = tag;
  info.available = installable;
  info.checkedMs = millis();
  return true;
}

// ---------------------------------------------------------------- install

// Streams the download into the next app slot, hashing it on the way.
class FlashSink : public Sink {
 public:
  size_t expect, done = 0;
  mbedtls_sha256_context sha;
  explicit FlashSink(size_t e) : expect(e) {
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
  }
  ~FlashSink() { mbedtls_sha256_free(&sha); }
  bool start(int length) override {
    if (length >= 0 && (size_t)length != expect) {
      why = "the download is " + String(length) + " bytes, the release says " + String(expect);
      return false;
    }
    return true;
  }
  bool put(const uint8_t *b, size_t n) override {
    if (done + n > expect) return why = "the download is longer than the release says", false;
    if (Update.write((uint8_t *)b, n) != n) return why = String("flash write: ") + Update.errorString(), false;
    mbedtls_sha256_update(&sha, b, n);
    done += n;
    return true;
  }
};

static bool installFailed(const String &status, const String &why) {
  Serial.printf("update: install failed: %s (%s)\n", why.c_str(), status.c_str());
  setStatus(UPD_IDLE, status, true);
  return false;
}

bool updInstall() {
  if (!binUrl.length() || !binSize) return installFailed("Nothing to install: check first", "no release");
  String tag = binTag;
  {
    info.state = UPD_INSTALLING;
    info.status = fmt("Updating to %s", tag.c_str());
    info.failed = false;
  }
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  if (!next) return installFailed("This board has no second app slot", "no OTA partition");
  if (binSize > next->size)
    return installFailed(fmt("%s is too big for this board's app slot", tag.c_str()),
                         String(binSize) + " bytes, the slot holds " + String(next->size));
  Serial.printf("update: installing %s (%u bytes) into %s\n", tag.c_str(), (unsigned)binSize, next->label);

  String expect = shaHex, why;
  if (shaUrl.length()) {  // the release's own checksum file: "<hex>  brainos.bin"
    BufferSink file(256);
    int code = httpGet(shaUrl, false, file, why);
    String text = file.buf ? String(file.buf) : String();
    text.trim();
    String hex = text.substring(0, 64);
    if (code != 200 || !isHex64(hex)) return installFailed("Couldn't get the release's checksum", why.length() ? why : "not a SHA-256: " + text.substring(0, 20));
    expect = hex;
  }
  if (!isHex64(expect)) return installFailed("The release has no checksum", expect.substring(0, 20));

  if (!Update.begin(binSize, U_FLASH)) return installFailed("Couldn't start writing the update", Update.errorString());
  FlashSink sink(binSize);
  int code = httpGet(binUrl, false, sink, why);
  if (code != 200 || sink.done != binSize) {
    Update.abort();
    if (code == 200) why = "got " + String(sink.done) + " of " + String(binSize) + " bytes";
    String status = sink.why.length() ? "The download doesn't match the release" : "The download didn't finish";
    return installFailed(status, why);
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&sink.sha, digest);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
  if (!expect.equalsIgnoreCase(hex)) {
    Update.abort();
    return installFailed("The download's checksum doesn't match", String("got ") + hex + ", want " + expect);
  }
  if (!Update.end()) return installFailed("The new firmware didn't check out", Update.errorString());  // the image's own hash too

  Serial.printf("update: %s written and checked (sha256 %s); restarting into it\n", tag.c_str(), hex);
  Preferences p;
  if (p.begin(NVS_NS, false)) {  // the next start tells whether it kept running
    p.putString("try", tag);
    p.end();
  }
  {
    info.state = UPD_RESTARTING;
    info.status = "Restarting…";
  }
  Serial.flush();
  ESP.restart();
  return true;
}
