#include "web.h"
#include <WebServer.h>
#include <LittleFS.h>
#include "board.h"
#include "config.h"
#include "net.h"
#include "notes.h"
#include "ota.h"
#include "provjson.h"
#include "stt.h"

static WebServer server(80);
static bool started;
extern void uiNotesChanged();
extern void updateRequest(bool install);
extern const char *const VERSION;

static String html(const String &s) {
  String o; o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

static String when(uint32_t t) {
  time_t secs = t; struct tm tm; localtime_r(&secs, &tm);
  char b[32]; strftime(b, sizeof(b), "%d %b %Y, %H:%M", &tm); return b;
}

static void setKey(const char *key, const String &value, String &err) {
  String j = pj::esc(value);
  pj::Val v;
  pj::parse(j.c_str(), j.length(), v);
  cfg::setInPlace(key, v, err);
}
static void setNum(const char *key, long value, String &err) {
  String j = String(value); pj::Val v; pj::parse(j.c_str(), j.length(), v); cfg::setInPlace(key, v, err);
}
static void setFlag(const char *key, bool value, String &err) {
  String j = value ? "true" : "false"; pj::Val v; pj::parse(j.c_str(), j.length(), v); cfg::setInPlace(key, v, err);
}

static const char *CSS =
  "body{font-family:-apple-system,Helvetica,Arial,sans-serif;max-width:640px;margin:0 auto;padding:16px;color:#111;background:#fafafa}"
  "h1{font-size:22px;margin:8px 0 4px}h2{font-size:17px;margin:28px 0 8px}.s{color:#666;font-size:14px}"
  ".n{background:#fff;border:1px solid #ddd;border-radius:10px;padding:12px 14px;margin:10px 0}"
  ".n p{margin:0 0 8px;white-space:pre-wrap;font-size:17px}.n .m{color:#777;font-size:13px}"
  ".n form{display:inline}button,input[type=submit]{font:inherit;font-size:14px;padding:6px 12px;border-radius:8px;border:1px solid #bbb;background:#fff;margin:2px 4px 0 0}"
  "button.d{color:#b00}input[type=text],input[type=password],input[type=number]{font:inherit;width:100%;box-sizing:border-box;padding:8px;border:1px solid #bbb;border-radius:8px;margin:4px 0 10px}"
  "label{font-size:14px;color:#444}.pend{color:#a60}.pin{font-size:13px;color:#06c}a{color:#06c}textarea{font:inherit;width:100%;box-sizing:border-box;padding:8px;border:1px solid #bbb;border-radius:8px;margin:4px 0 6px;min-height:70px}details{margin-top:6px}summary{cursor:pointer;font-size:14px;color:#06c}";

static void sendPage(const String &msg) {
  String p = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>" + html(cfg::str("device.name")) + "</title><style>" + CSS + "</style></head><body>";
  p += "<h1>" + html(cfg::str("device.name")) + "</h1>";
  int pend = notesPending();
  p += "<div class='s'>Battery " + String(now.percent) + "%" + (now.usb ? ", charging" : "") + " · " + String(notesCount(false)) + " notes" + (pend ? ", " + String(pend) + " waiting for transcription" : "") + " · " + String(VERSION) + "</div>";
  if (msg.length()) p += "<p><b>" + html(msg) + "</b></p>";
  if (!sttConfigured()) p += "<p class='pend'>No OpenRouter key yet: recordings wait until one is set below.</p>";
  else if (strlen(sttLastError())) p += "<p class='pend'>Last transcription problem: " + html(sttLastError()) + "</p>";

  p += "<h2>New note</h2><form method='post' action='/new'><textarea name='text' placeholder='Type a note'></textarea><input type='submit' value='Save note'></form>";
  p += "<h2>Notes</h2>";
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) if (!all[i].archived) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return all[a].created > all[b].created; });
  if (idx.empty()) p += "<p class='s'>None yet. Hold OK on the board and speak.</p>";
  for (int i : idx) {
    const Note &n = all[i];
    p += "<div class='n'>";
    if (n.audio.length()) p += "<p class='pend'>Recording of " + String(n.secs) + " s, waiting for transcription</p>";
    else p += "<p>" + html(n.text) + "</p>";
    p += "<div class='m'>" + when(n.created) + (n.pinned ? " · <span class='pin'>pinned</span>" : "") + "</div>";
    p += "<form method='post' action='/note'><input type='hidden' name='id' value='" + n.id + "'>";
    p += "<button name='do' value='pin'>" + String(n.pinned ? "Unpin" : "Pin") + "</button>" + (n.audio.length() ? String() : String("<button name='do' value='archive'>Archive</button>")) + "<button class='d' name='do' value='delete' onclick='return confirm(\"Delete this note?\")'>Delete</button></form>";
    if (!n.audio.length()) p += "<details><summary>Edit</summary><form method='post' action='/note'><input type='hidden' name='id' value='" + n.id + "'><input type='hidden' name='do' value='edit'><textarea name='text'>" + html(n.text) + "</textarea><input type='submit' value='Save'></form></details>";
    p += "</div>";
  }
  int arch = notesCount(true);
  p += "<p class='s'><a href='/notes.md'>Download all notes (.md)</a> · <a href='/notes.json'>JSON</a>" + (arch ? " · " + String(arch) + " archived (<a href='/archive'>see</a>)" : String()) + "</p>";

  int fresh = 0;
  for (auto &n : all) if (!n.archived && !n.audio.length() && n.text.length()) fresh++;
  p += "<h2>Export</h2><p class='s'>" + String(fresh) + " note" + (fresh == 1 ? "" : "s") + " to export. Exporting here downloads them as Markdown and " + (cfg::flag("export.archive") ? "moves them to the Archive" : "marks them exported") + ", like the QR export on the board.</p>";
  if (fresh) p += "<form method='post' action='/export'><input type='submit' value='Export these notes (.md)'></form>";
  UpdInfo u = updInfo();
  p += "<h2>Update</h2><p class='s'>This board: " + String(updVersion()) + (u.checkedMs ? ". " + html(u.status) : "") + "</p><form method='post' action='/update'><button name='do' value='check'>Check for update</button>" + (u.available ? "<button name='do' value='install' onclick='return confirm(\"Install " + html(u.latest) + "? The board restarts.\")'>Install " + html(u.latest) + "</button>" : String()) + "</form>";
  p += "<h2>Settings</h2><form method='post' action='/settings'>";
  p += "<label>OpenRouter API key " + String(sttConfigured() ? "(set; leave empty to keep)" : "(not set)") + "</label><input type='password' name='key' placeholder='sk-or-...' autocomplete='off'>";
  p += "<label>Speech-to-text model</label><input type='text' name='model' value='" + html(cfg::str("stt.model")) + "'>";
  p += "<label>Language hint (pt, en; empty detects)</label><input type='text' name='lang' value='" + html(cfg::str("stt.language")) + "'>";
  p += "<label>Device name</label><input type='text' name='name' value='" + html(cfg::str("device.name")) + "'>";
  p += "<label>Time zone (POSIX, e.g. &lt;-03&gt;3)</label><input type='text' name='tz' value='" + html(cfg::str("device.timezone")) + "'>";
  p += "<label>Wi-Fi stays on this many minutes after the last touch (0: always)</label><input type='number' name='idle' value='" + String(cfg::num("wifi.idle_min")) + "'>";
  p += "<label>On battery, deep sleep after this many minutes without a touch (0: never)</label><input type='number' name='sleep' value='" + String(cfg::num("sleep.min")) + "'>";
  p += "<label><input type='checkbox' name='archive' " + String(cfg::flag("export.archive") ? "checked" : "") + "> Archive notes after exporting</label><br>";
  p += "<label><input type='checkbox' name='sound' " + String(cfg::flag("sound") ? "checked" : "") + "> Beeps</label><br><br>";
  p += "<input type='submit' value='Save'></form>";
  p += "<h2>Wi-Fi</h2><p class='s'>Connected to " + html(netName()) + " (" + netAddress() + ", " + String(netSignal()) + " dBm). Networks are added on the board, in Settings.</p>";
  p += "</body></html>";
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleRoot() { netTouch(); sendPage(""); }

static void handleSettings() {
  netTouch();
  String err, msg = "Saved";
  if (server.arg("key").length()) { setKey("stt.api_key", server.arg("key"), err); if (err.length()) msg = "Key: " + err; }
  if (!err.length()) setKey("stt.model", server.arg("model"), err);
  if (!err.length()) setKey("stt.language", server.arg("lang"), err);
  if (!err.length()) setKey("device.name", server.arg("name"), err);
  if (!err.length()) { setKey("device.timezone", server.arg("tz"), err); if (!err.length()) { setenv("TZ", server.arg("tz").c_str(), 1); tzset(); } }
  if (!err.length() && server.arg("idle").length()) setNum("wifi.idle_min", server.arg("idle").toInt(), err);   // an emptied field keeps its value
  if (!err.length() && server.arg("sleep").length()) setNum("sleep.min", server.arg("sleep").toInt(), err);
  if (!err.length()) setFlag("export.archive", server.hasArg("archive"), err);
  if (!err.length()) { setFlag("sound", server.hasArg("sound"), err); soundOn = cfg::flag("sound"); }
  if (err.length()) msg = "Not saved: " + err;
  uiNotesChanged();
  sendPage(msg);
}

static void handleNote() {
  netTouch();
  Note *n = notesFind(server.arg("id"));
  String d = server.arg("do");
  if (n) {
    if (d == "delete") notesRemove(n->id);
    else if (d == "pin" && !n->archived) {
      if (n->pinned) n->pinned = false;
      else { for (;;) { int c = 0, oldest = -1; auto &all = notesAll(); for (size_t i = 0; i < all.size(); i++) if (all[i].pinned) { c++; if (oldest < 0 || all[i].modified < all[oldest].modified) oldest = i; } if (c < 3) break; all[oldest].pinned = false; } n->pinned = true; }
      n->modified = time(nullptr); notesSave();
    }
    else if (d == "archive" && !n->audio.length()) { n->archived = true; n->pinned = false; notesSave(); }
    else if (d == "restore") { n->archived = false; n->exported = false; notesSave(); }
    else if (d == "edit" && !n->archived) { String t = server.arg("text"); t.trim(); if (t.length()) { n->text = t; n->modified = time(nullptr); notesSave(); } }
  }
  uiNotesChanged();
  server.sendHeader("Location", server.arg("back") == "/archive" ? "/archive" : "/");   // only our own two pages
  server.send(303, "text/plain", "");
}

static void handleNew() {
  netTouch();
  String t = server.arg("text"); t.trim();
  if (t.length()) notesAdd(t, "", 0);
  uiNotesChanged();
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

static String indented(String t) { t.replace("\n", "\n  "); return t; }   // a note's own line breaks stay inside its list item

static void handleExport() {
  netTouch();
  String p = "# " + cfg::str("device.name") + ", exported " + when(time(nullptr)) + "\n\n";
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) if (!all[i].archived && !all[i].audio.length() && all[i].text.length()) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return all[a].created < all[b].created; });
  bool archive = cfg::flag("export.archive");
  for (int i : idx) { Note &n = all[i]; p += "- **" + when(n.created) + "**\n  " + indented(n.text) + "\n"; n.exported = true; if (archive) { n.archived = true; n.pinned = false; } }
  if (!idx.empty()) notesSave();
  uiNotesChanged();
  server.sendHeader("Content-Disposition", "attachment; filename=brainos-export.md");
  server.send(200, "text/markdown; charset=utf-8", p);
}

static void handleUpdate() {
  netTouch();
  bool install = server.arg("do") == "install" && updInfo().available;
  updateRequest(install);
  String p = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='" + String(install ? 90 : 12) + ";url=/'><style>" + String(CSS) + "</style></head><body><h1>" + String(install ? "Installing" : "Checking for an update") + "</h1><p class='s'>" + (install ? "The board downloads the release, checks it and restarts. This page comes back in a minute and a half." : "This page comes back in a few seconds.") + "</p></body></html>";
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleArchive() {
  netTouch();
  String p = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Archive</title><style>" + String(CSS) + "</style></head><body><h1>Archive</h1><p class='s'><a href='/'>Back</a></p>";
  for (auto &n : notesAll()) if (n.archived) {
    p += "<div class='n'><p>" + html(n.text) + "</p><div class='m'>" + when(n.created) + "</div><form method='post' action='/note'><input type='hidden' name='id' value='" + n.id + "'><input type='hidden' name='back' value='/archive'><button name='do' value='restore'>Restore</button><button class='d' name='do' value='delete' onclick='return confirm(\"Delete?\")'>Delete</button></form></div>";
  }
  p += "</body></html>";
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleMd() {
  netTouch();
  String p = "# " + cfg::str("device.name") + "\n\n";
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return all[a].created > all[b].created; });
  for (int i : idx) { const Note &n = all[i]; p += "- **" + when(n.created) + "**" + (n.archived ? " (archived)" : "") + (n.pinned ? " (pinned)" : "") + "\n  " + (n.audio.length() ? String("(recording waiting for transcription)") : indented(n.text)) + "\n"; }
  server.sendHeader("Content-Disposition", "attachment; filename=notes.md");
  server.send(200, "text/markdown; charset=utf-8", p);
}

static void handleJson() {
  netTouch();
  String p = "[";
  bool first = true;
  for (auto &n : notesAll()) {
    if (!first) p += ",";
    first = false;
    p += "{\"id\":\"" + n.id + "\",\"text\":" + pj::esc(n.text) + ",\"created\":" + n.created + ",\"pinned\":" + (n.pinned ? "true" : "false") + ",\"archived\":" + (n.archived ? "true" : "false") + ",\"pending\":" + (n.audio.length() ? "true" : "false") + "}";
  }
  p += "]";
  server.send(200, "application/json", p);
}

void webBegin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/note", HTTP_POST, handleNote);
  server.on("/new", HTTP_POST, handleNew);
  server.on("/export", HTTP_POST, handleExport);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/archive", HTTP_GET, handleArchive);
  server.on("/notes.md", HTTP_GET, handleMd);
  server.on("/notes.json", HTTP_GET, handleJson);
  server.onNotFound([] { server.send(404, "text/plain", "Not here"); });
}

void webTick() {
  bool up = netUp();
  if (up && !started) { server.begin(); started = true; Serial.println("web: http://brain.local"); }
  if (!up && started) { server.stop(); started = false; }
  if (started) server.handleClient();
}
