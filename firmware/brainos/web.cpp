// The page at http://brain.local: notes, Wi-Fi, settings, update. Plain HTML forms, one
// page, served by the board itself; the QR reader lives on GitHub Pages (the camera needs
// https) and the page links to it.
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

static const char *READER = "https://carcaralivre.github.io/brainos-reader/";

static String html(const String &s) {
  String o; o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}

static String when(uint32_t t) {
  time_t secs = t; struct tm tm; localtime_r(&secs, &tm);
  char b[32]; strftime(b, sizeof(b), "%d %b %Y, %H:%M", &tm); return b;
}

static const char CSS[] PROGMEM =
  ":root{--ink:#111;--mute:#6b6b70;--line:#e4e4e7;--bg:#f2f2f4;--card:#fff;--acc:#0a7cff;--bad:#d0342c}"
  "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;-webkit-text-size-adjust:100%}"
  ".wrap{max-width:640px;margin:0 auto;padding:20px 16px 56px}"
  "header{padding:4px 4px 18px}h1{font-size:28px;margin:0;letter-spacing:-.02em;font-weight:700}"
  ".chips{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0 14px}.chip{font-size:13px;padding:5px 11px;border-radius:999px;background:#e6e6ea;color:#333}.chip.ok{background:#dcf5e3;color:#126a2d}.chip.warn{background:#fff0d2;color:#8a5600}"
  ".card{background:var(--card);border-radius:18px;padding:18px;margin:0 0 14px;box-shadow:0 1px 2px rgba(0,0,0,.05)}"
  "h2{font-size:13px;text-transform:uppercase;letter-spacing:.07em;color:var(--mute);margin:0 0 12px;font-weight:600}"
  ".note{padding:14px 0;border-top:1px solid var(--line)}.note.first{border-top:0;padding-top:4px}"
  ".note p{margin:0 0 6px;white-space:pre-wrap;font-size:17px}.meta{color:var(--mute);font-size:13px;margin-bottom:10px}"
  ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}form{display:inline}form.b{display:block}"
  "button,input[type=submit],.btn{font:inherit;font-size:15px;font-weight:600;padding:11px 18px;border-radius:12px;border:1px solid var(--line);background:#fff;color:var(--ink);text-decoration:none;display:inline-block;cursor:pointer}"
  ".p{background:var(--acc);color:#fff;border-color:var(--acc)}.d{color:var(--bad)}.w{width:100%;text-align:center;font-size:17px;padding:14px}"
  ".note button{padding:7px 13px;font-size:14px;border-radius:10px;font-weight:500}"
  "label{display:block;font-size:13px;color:var(--mute);margin:14px 0 5px}label:first-child{margin-top:0}"
  "input[type=text],input[type=password],input[type=number],select,textarea{font:inherit;font-size:16px;width:100%;padding:12px;border:1px solid var(--line);border-radius:12px;background:#fafafa;color:var(--ink);-webkit-appearance:none;appearance:none}"
  "select{background:#fafafa url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='14' height='9'%3E%3Cpath d='M1 1l6 6 6-6' fill='none' stroke='%236b6b70' stroke-width='2'/%3E%3C/svg%3E\") no-repeat right 14px center;padding-right:36px}"
  "textarea{min-height:84px}.check{display:flex;align-items:center;gap:12px;font-size:16px;color:var(--ink);margin:14px 0 0}.check input{width:22px;height:22px;margin:0}"
  ".pend{color:#8a5600}.mute{color:var(--mute);font-size:14px;margin:0 0 10px}a{color:var(--acc)}details{margin-top:8px}summary{cursor:pointer;color:var(--acc);font-size:14px}"
  ".msg{background:#111;color:#fff;border-radius:12px;padding:12px 16px;margin:0 0 14px;font-weight:600}"
  ".net{display:flex;justify-content:space-between;align-items:center;padding:12px 0;border-top:1px solid var(--line)}.net.first{border-top:0;padding-top:0}.net b{font-weight:600}";

static String head(const String &title, const String &extra = "") {
  return "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>" + html(title) + "</title>" + extra + "<style>" + String(FPSTR(CSS)) + "</style></head><body><div class='wrap'>";
}

struct Zone { const char *label, *tz; };
static const Zone ZONES[] = {
  {"Brasília (UTC-3)", "<-03>3"}, {"Fernando de Noronha (UTC-2)", "<-02>2"}, {"Manaus (UTC-4)", "<-04>4"}, {"Rio Branco (UTC-5)", "<-05>5"},
  {"Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"}, {"London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"Paris, Berlin, Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"New York", "EST5EDT,M3.2.0,M11.1.0"}, {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"}, {"UTC", "UTC0"},
};
static const char *const MODELS[] = {"openai/gpt-4o-mini-transcribe", "openai/gpt-4o-transcribe", "openai/whisper-large-v3-turbo", "openai/whisper-large-v3", "openai/whisper-1"};

static String option(const String &value, const String &label, const String &current) {
  return "<option value='" + html(value) + "'" + (value == current ? " selected" : "") + ">" + html(label) + "</option>";
}

static void noteCard(String &p, const Note &n, bool first, const char *back) {
  p += "<div class='note" + String(first ? " first" : "") + "'>";
  if (n.audio.length()) p += "<p class='pend'>Recording of " + String(n.secs) + " s, waiting for transcription</p>";
  else p += "<p>" + html(n.text) + "</p>";
  p += "<div class='meta'>" + when(n.created) + (n.pinned ? " · pinned" : "") + "</div>";
  p += "<div class='row'><form method='post' action='/note'><input type='hidden' name='id' value='" + n.id + "'><input type='hidden' name='back' value='" + back + "'>";
  if (n.archived) p += "<button name='do' value='restore'>Restore</button>";
  else {
    p += "<button name='do' value='pin'>" + String(n.pinned ? "Unpin" : "Pin") + "</button>";
    if (!n.audio.length()) p += "<button name='do' value='archive'>Archive</button>";
  }
  p += "<button class='d' name='do' value='delete' onclick='return confirm(\"Delete this note?\")'>Delete</button></form></div>";
  if (!n.archived && !n.audio.length()) p += "<details><summary>Edit</summary><form class='b' method='post' action='/note'><input type='hidden' name='id' value='" + n.id + "'><input type='hidden' name='do' value='edit'><textarea name='text'>" + html(n.text) + "</textarea><p></p><input type='submit' value='Save'></form></details>";
  p += "</div>";
}

static void sendPage(const String &msg) {
  String name = cfg::str("device.name");
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) if (!all[i].archived) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return all[a].created > all[b].created; });
  String p;
  p.reserve(9000 + 700 * idx.size());
  p += head(name);
  int pend = notesPending(), arch = notesCount(true);
  p += "<header><h1>" + html(name) + "</h1><div class='chips'>";
  p += "<span class='chip'>Battery " + String(now.percent) + "%" + (now.usb ? ", charging" : "") + "</span>";
  p += "<span class='chip'>" + String(netUp() ? html(netName()) : "No Wi-Fi") + "</span>";
  p += sttConfigured() ? "<span class='chip ok'>OpenRouter key set</span>" : "<span class='chip warn'>No OpenRouter key</span>";
  p += "<span class='chip'>" + String(VERSION) + "</span></div>";
  p += "<a class='btn p w' href='" + String(READER) + "'>Open the QR reader</a></header>";
  if (msg.length()) p += "<div class='msg'>" + html(msg) + "</div>";
  if (strlen(sttLastError())) p += "<p class='pend'>Last transcription problem: " + html(sttLastError()) + "</p>";

  p += "<div class='card'><h2>New note</h2><form class='b' method='post' action='/new'><textarea name='text' placeholder='Type a note'></textarea><p></p><input class='p' type='submit' value='Save note'></form></div>";

  p += "<div class='card'><h2>Notes · " + String(idx.size()) + (pend ? ", " + String(pend) + " waiting" : String()) + "</h2>";
  if (idx.empty()) p += "<p class='mute'>None yet. Hold OK on the board and speak.</p>";
  bool first = true;
  for (int i : idx) { noteCard(p, all[i], first, "/"); first = false; }
  p += "</div>";

  p += "<div class='card'><h2>Export</h2><p class='mute'>On the board, Settings → Export shows the notes as QR codes. Open the QR reader (button at the top), tap Scan and point the camera at the board; when the reader has every part, tap Done on the board.</p>";
  int fresh = 0;
  for (int i : idx) if (!all[i].audio.length() && all[i].text.length()) fresh++;
  p += "<p class='mute'>Or export here: " + String(fresh) + " note" + (fresh == 1 ? "" : "s") + " as a Markdown file, which " + (cfg::flag("export.archive") ? "moves them to the Archive" : "marks them exported") + ".</p><div class='row'>";
  if (fresh) p += "<form method='post' action='/export'><input type='submit' value='Export these notes (.md)'></form>";
  p += "<a class='btn' href='/notes.md'>All notes (.md)</a><a class='btn' href='/notes.json'>JSON</a>" + (arch ? "<a class='btn' href='/archive'>Archive · " + String(arch) + "</a>" : String()) + "</div></div>";

  std::vector<WifiCred> nets = cfg::wifiNetworks();
  p += "<div class='card'><h2>Wi-Fi</h2>";
  p += "<p class='mute'>" + String(netUp() ? "Connected to " + html(netName()) + ", " + netAddress() + ", " + String(netSignal()) + " dBm." : "Not connected.") + "</p>";
  for (size_t i = 0; i < nets.size(); i++) {
    p += "<div class='net" + String(i == 0 ? " first" : "") + "'><b>" + html(nets[i].ssid) + "</b><form method='post' action='/wifi'><input type='hidden' name='do' value='forget'><input type='hidden' name='ssid' value='" + html(nets[i].ssid) + "'><button class='d' onclick='return confirm(\"Forget " + html(nets[i].ssid) + "?\")'>Forget</button></form></div>";
  }
  if (nets.empty()) p += "<p class='mute'>No saved networks.</p>";
  p += "<details><summary>Add a network</summary><form class='b' method='post' action='/wifi'><input type='hidden' name='do' value='add'><label>Network name</label><input type='text' name='ssid' autocapitalize='off' autocorrect='off'><label>Password (empty for an open network)</label><input type='password' name='pass' autocomplete='off'><p></p><input type='submit' value='Save network'></form></details></div>";

  p += "<div class='card'><h2>Settings</h2><form class='b' method='post' action='/settings'><input type='hidden' name='boxes' value='1'>";
  p += "<label>OpenRouter API key</label><div class='row' style='margin-bottom:6px'>" + String(sttConfigured() ? "<span class='chip ok'>Set</span><span class='mute' style='margin:0'>Paste a new one below to replace it</span>" : "<span class='chip warn'>Not set</span><span class='mute' style='margin:0'>Recordings wait until a key is set</span>") + "</div><input type='password' name='key' placeholder='sk-or-…' autocomplete='off'>";
  String model = cfg::str("stt.model");
  p += "<label>Transcription model</label><select name='model'>";
  bool known = false;
  for (const char *m : MODELS) { p += option(m, m, model); known |= model == m; }
  if (!known) p += option(model, model, model);
  p += "</select>";
  p += "<label>Language hint (pt, en; empty detects)</label><input type='text' name='lang' value='" + html(cfg::str("stt.language")) + "' autocapitalize='off'>";
  p += "<label>Device name</label><input type='text' name='name' value='" + html(cfg::str("device.name")) + "'>";
  String tz = cfg::str("device.timezone");
  p += "<label>Time zone</label><select name='tz'>";
  known = false;
  for (const Zone &z : ZONES) { p += option(z.tz, z.label, tz); known |= tz == z.tz; }
  if (!known) p += option(tz, "Custom: " + tz, tz);
  p += "</select>";
  p += "<label>Wi-Fi stays on this many minutes after the last touch (0: always on)</label><input type='number' name='idle' min='0' max='1440' value='" + String(cfg::num("wifi.idle_min")) + "'>";
  p += "<label>On battery, deep sleep after this many minutes without a touch (0: never)</label><input type='number' name='sleep' min='0' max='1440' value='" + String(cfg::num("sleep.min")) + "'>";
  p += "<label class='check'><input type='checkbox' name='archive' " + String(cfg::flag("export.archive") ? "checked" : "") + "> Archive notes after exporting</label>";
  p += "<label class='check'><input type='checkbox' name='sound' " + String(cfg::flag("sound") ? "checked" : "") + "> Beeps on the board</label>";
  p += "<p></p><input class='p' type='submit' value='Save settings'></form></div>";

  UpdInfo u = updInfo();
  p += "<div class='card'><h2>About</h2><p class='mute'>BrainOS " + String(VERSION) + " on " + html(name) + ". " + (u.checkedMs ? html(u.status) : String("Updates are checked only when you ask.")) + "</p><div class='row'><form method='post' action='/update'><button name='do' value='check'>Check for update</button>";
  if (u.available) p += "<button class='p' name='do' value='install' onclick='return confirm(\"Install " + html(u.latest) + "? The board restarts.\")'>Install " + html(u.latest) + "</button>";
  p += "</form></div></div>";
  p += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleRoot() { netTouch(); sendPage(""); }

static void back(const char *where) {
  server.sendHeader("Location", where);
  server.send(303, "text/plain", "");
}

// Only the fields that came with the form change; checkboxes come as a set (boxes=1).
static void handleSettings() {
  netTouch();
  String err;
  if (server.arg("key").length()) cfg::setStr("stt.api_key", server.arg("key"), err);
  if (!err.length() && server.hasArg("model")) cfg::setStr("stt.model", server.arg("model"), err);
  if (!err.length() && server.hasArg("lang")) cfg::setStr("stt.language", server.arg("lang"), err);
  if (!err.length() && server.hasArg("name")) cfg::setStr("device.name", server.arg("name"), err);
  if (!err.length() && server.hasArg("tz") && cfg::setStr("device.timezone", server.arg("tz"), err)) { setenv("TZ", server.arg("tz").c_str(), 1); tzset(); }
  if (!err.length() && server.arg("idle").length()) cfg::setNum("wifi.idle_min", server.arg("idle").toInt(), err);
  if (!err.length() && server.arg("sleep").length()) cfg::setNum("sleep.min", server.arg("sleep").toInt(), err);
  if (!err.length() && server.hasArg("boxes")) {
    cfg::setFlag("export.archive", server.hasArg("archive"), err);
    if (!err.length()) { cfg::setFlag("sound", server.hasArg("sound"), err); soundOn = cfg::flag("sound"); }
  }
  uiNotesChanged();
  sendPage(err.length() ? "Not saved: " + err : "Saved");
}

static void handleWifi() {
  netTouch();
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  String d = server.arg("do"), ssid = server.arg("ssid"), err, msg;
  if (d == "forget") {
    for (size_t i = 0; i < nets.size(); i++) if (nets[i].ssid == ssid) nets.erase(nets.begin() + i--);
    msg = cfg::setWifi(nets, err) ? "Forgot " + ssid : "Not saved: " + err;
  } else if (d == "add") {
    for (size_t i = 0; i < nets.size(); i++) if (nets[i].ssid == ssid) nets.erase(nets.begin() + i--);
    nets.push_back({ssid, server.arg("pass")});
    msg = cfg::setWifi(nets, err) ? "Saved " + ssid + ": the board joins it when its current network is out of reach" : "Not saved: " + err;
  }
  uiNotesChanged();
  sendPage(msg);
}

static void handleNote() {
  netTouch();
  Note *n = notesFind(server.arg("id"));
  String d = server.arg("do");
  if (n) {
    if (d == "delete") notesRemove(n->id);
    else if (d == "pin" && !n->archived) notesPin(*n);
    else if (d == "archive" && !n->audio.length()) { n->archived = true; n->pinned = false; notesSave(); }
    else if (d == "restore") { n->archived = false; n->exported = false; notesSave(); }
    else if (d == "edit" && !n->archived) { String t = server.arg("text"); t.trim(); if (t.length()) { n->text = t; n->modified = time(nullptr); notesSave(); } }
  }
  uiNotesChanged();
  back(server.arg("back") == "/archive" ? "/archive" : "/");   // only our own two pages
}

static void handleNew() {
  netTouch();
  String t = server.arg("text"); t.trim();
  if (t.length()) notesAdd(t, "", 0);
  uiNotesChanged();
  back("/");
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
  String p = head(install ? "Installing" : "Checking", "<meta http-equiv='refresh' content='" + String(install ? 90 : 12) + ";url=/'>");
  p += "<div class='card'><h2>" + String(install ? "Installing" : "Checking for an update") + "</h2><p class='mute'>" + (install ? "The board downloads the release, checks it and restarts. This page comes back in a minute and a half." : "This page comes back in a few seconds.") + "</p></div></div></body></html>";
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleArchive() {
  netTouch();
  String p = head("Archive");
  p += "<header><h1>Archive</h1><p class='mute'><a href='/'>Back to the notes</a></p></header><div class='card'>";
  bool first = true, any = false;
  for (auto &n : notesAll()) if (n.archived) { noteCard(p, n, first, "/archive"); first = false; any = true; }
  if (!any) p += "<p class='mute'>The archive is empty.</p>";
  p += "</div></div></body></html>";
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
  server.on("/wifi", HTTP_POST, handleWifi);
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
  if (!netUp()) { if (started) { server.stop(); started = false; } return; }
  if (!started) { server.begin(); started = true; Serial.println("web: http://brain.local"); }
  server.handleClient();
}
