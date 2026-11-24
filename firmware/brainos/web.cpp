// The page at http://brain.local: notes, Wi-Fi, settings, update. Plain HTML forms, one
// page, served by the board itself; the QR reader lives on GitHub Pages (the camera needs
// https) and the page links to it.
#include "web.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <esp_random.h>
#include "mbedtls/sha256.h"
#include "board.h"
#include "cost.h"
#include "config.h"
#include "dates.h"
#include "glyphs.h"
#include "screen.h"
#include "ui.h"
#include "net.h"
#include "notes.h"
#include "ota.h"
#include "provjson.h"
#include "stt.h"
#include "semver.h"
#include <vector>

static WebServer server(80);
static bool started;
extern void uiNotesChanged();
extern void updateRequest(bool install);
extern const char *const VERSION;

static const char *READER = "https://carcaralivre.github.io/brainos-reader/";

// Undo: the notes as they were before the last delete, archive or export, for 6 s (a few more
// here, for a slow phone). A deleted recording's audio file stays until then. Undoing an archive
// gives back only where the note was (and its pin), so a transcription that landed meanwhile stays.
static std::vector<Note> undoNotes;
static String undoWhat;        // "Deleted", "Archived", "Exported and archived"
static bool undoDelete;        // the notes are gone (put them back whole) or only archived
static uint32_t undoAt;
static const uint32_t UNDO_SHOW = 6000, UNDO_KEEP = 10000;
static void undoForget() {
  for (auto &n : undoNotes) if (n.audio.length() && !notesFind(n.id)) LittleFS.remove(n.audio);
  undoNotes.clear(); undoWhat = "";
}
static void undoStart(const char *what, bool deleted) { undoForget(); undoWhat = what; undoDelete = deleted; undoAt = millis(); }
static bool undoLive() { return undoWhat.length() && millis() - undoAt < UNDO_KEEP; }

static String html(const String &s) {
  String o; o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}

static String when(uint32_t t) { return dateText(t, "%d %b %Y, %H:%M"); }

// A page goes out in pieces as it is written, so a long list never sits whole in memory and the
// board's buttons wait less.
struct Out {
  String buf;
  void begin(const char *type) { buf.reserve(4200); server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, type, ""); }
  Out &operator+=(const String &s) { buf += s; if (buf.length() > 4000) flush(); return *this; }
  void flush() { if (buf.length()) server.sendContent(buf); buf = ""; }
  void end() { flush(); server.sendContent(""); }
};

static const char CSS[] PROGMEM =
  ":root{--bg:#faf8f4;--card:#f5f2ec;--sunken:#fdfcfa;--accent:#efebe3;--border:#e5e0d5;--input:#d9d3c7;--fg:#1a1917;--muted:#6b6760;--tertiary:#716b62;--primary:#2d2d2b;--destructive:#a03030}"
  "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/20px Geist,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;-webkit-text-size-adjust:100%}"
  ".wrap{max-width:640px;margin:0 auto;padding:12px 12px 48px}"
  "header{position:sticky;top:0;background:var(--bg);border-bottom:1px solid var(--border);margin:0 -12px 12px;padding:0 12px;height:56px;display:flex;align-items:center;justify-content:space-between;gap:12px}"
  "h1{font-size:17px;font-weight:600;margin:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
  ".chips{display:flex;justify-content:center;flex-wrap:wrap;font:11px/16px 'Geist Mono',ui-monospace,SFMono-Regular,Menlo,monospace;color:var(--tertiary);margin:0 0 12px}.chip{white-space:nowrap}.chip+.chip:before{content:'\\00b7';margin:0 8px;color:var(--tertiary)}.chip.warn{color:var(--destructive)}"
  ".card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:16px;margin:0 0 12px;box-shadow:0 1px 2px rgba(0,0,0,.05)}"
  "h2{font:500 11px/16px Geist,-apple-system,sans-serif;text-transform:uppercase;letter-spacing:.08em;color:var(--tertiary);margin:0 0 12px}"
  ".note{padding:12px 0;border-top:1px solid var(--border)}.note.first{border-top:0;padding-top:0}"
  ".note p{margin:0 0 6px;white-space:pre-wrap;font-size:14px;line-height:20px}.meta{font:11px/16px 'Geist Mono',ui-monospace,Menlo,monospace;color:var(--tertiary);margin:0 0 8px}"
  ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}form{display:inline}.row form{display:flex;gap:8px;flex-wrap:wrap}form.b{display:block}.edit{margin-top:8px}[hidden]{display:none!important}"
  "button,input[type=submit],.btn{font:500 14px/20px Geist,-apple-system,sans-serif;padding:0 12px;height:36px;border-radius:10px;border:1px solid transparent;background:transparent;color:var(--fg);text-decoration:none;display:inline-flex;align-items:center;cursor:pointer;transition:background-color .1s,color .1s,border-color .1s}"
  "button:hover,.btn:hover{background:var(--accent)}.o,input.o{border-color:var(--border)}.p,input.p{background:var(--primary);color:#fff}.p:hover,input.p:hover{background:#1a1917}.d{color:var(--destructive)}.destructive{background:var(--destructive);color:#fff;border-color:var(--destructive)}.destructive:hover{background:var(--destructive)}"
  ".w{width:100%;justify-content:center}"
  "label{display:block;font:11px/16px Geist,-apple-system,sans-serif;text-transform:uppercase;letter-spacing:.08em;color:var(--tertiary);margin:14px 0 6px}label:first-child{margin-top:0}"
  "input[type=text],input[type=password],input[type=number],select,textarea{font:14px/20px Geist,-apple-system,sans-serif;width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:10px;background:var(--sunken);color:var(--fg);-webkit-appearance:none;appearance:none;outline:none}"
  "input:focus,select:focus,textarea:focus{border-color:var(--primary);box-shadow:0 0 0 2px var(--input)}"
  "select{background:var(--sunken) url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8'%3E%3Cpath d='M1 1l5 5 5-5' fill='none' stroke='%23716b62' stroke-width='1.5'/%3E%3C/svg%3E\") no-repeat right 12px center;padding-right:32px}"
  "textarea{min-height:80px;line-height:20px}.check{display:flex;align-items:center;gap:10px;font:14px/20px Geist,-apple-system,sans-serif;text-transform:none;letter-spacing:0;color:var(--fg);margin:12px 0 0}.check input{width:18px;height:18px;margin:0;accent-color:var(--primary)}"
  ".pend{color:var(--muted)}.mute{color:var(--muted);margin:0 0 10px}a{color:var(--fg)}details{margin-top:8px}summary{cursor:pointer;color:var(--muted);list-style:none}summary::-webkit-details-marker{display:none}summary:before{content:'+ '}details[open] summary:before{content:'\\2212 '}"
  ".msg{border:1px dashed var(--border);border-radius:10px;padding:10px 12px;margin:0 0 12px;color:var(--muted)}"
  ".net{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-top:1px solid var(--border)}.net.first{border-top:0;padding-top:0}.net b{font-weight:500}"
  ".toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--primary);color:#fff;border-radius:12px;padding:6px 6px 6px 16px;display:flex;align-items:center;gap:12px;box-shadow:0 4px 16px rgba(0,0,0,.15)}"
  ".toast button{color:#fff;border-color:rgba(255,255,255,.3)}.toast button:hover{background:#1a1917}"
  "table{width:100%;border-collapse:collapse;font:12px/18px 'Geist Mono',ui-monospace,Menlo,monospace;margin:0 0 12px}td{padding:4px 0;border-top:1px solid var(--border)}td:last-child{text-align:right}"
  "h3{font:500 12px/16px Geist,-apple-system,sans-serif;color:var(--muted);margin:12px 0 4px}";

// Destructive controls arm on the first click ("Yes, …") and run on a second; a click anywhere
// else, or 3 s, disarms them. No dialogs.
static const char JS[] PROGMEM =
  "document.addEventListener('click',function(e){var b=e.target.closest('button[data-arm]');"
  "document.querySelectorAll('button[data-armed]').forEach(function(o){if(o!==b){delete o.dataset.armed;o.textContent=o.dataset.label;o.classList.remove('destructive')}});"
  "var t=e.target.closest('button[data-edit]');if(t){var f=t.closest('.note').querySelector('.edit');f.hidden=!f.hidden;if(!f.hidden)f.querySelector('textarea').focus();return}"
  "if(!b)return;if(b.dataset.armed)return;e.preventDefault();"
  "b.dataset.armed='1';b.dataset.label=b.textContent;b.textContent=b.dataset.arm;b.classList.add('destructive');"
  "setTimeout(function(){delete b.dataset.armed;b.textContent=b.dataset.label;b.classList.remove('destructive')},3000)});";

static String head(const String &title, const String &extra = "") {
  return "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>" + html(title) + "</title>" + extra + "<style>" + String(FPSTR(CSS)) + "</style></head><body><div class='wrap'>";
}

struct Zone { const char *label, *tz; };
static const Zone ZONES[] = {
  {"Brasília (UTC-3)", "<-03>3"}, {"Fernando de Noronha (UTC-2)", "<-02>2"}, {"Manaus (UTC-4)", "<-04>4"}, {"Rio Branco (UTC-5)", "<-05>5"},
  {"Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"}, {"London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"Paris, Berlin, Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"New York", "EST5EDT,M3.2.0,M11.1.0"}, {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"}, {"UTC", "UTC0"},
};
static const char *const ASK_MODELS[] = {"openai/gpt-4o-mini", "openai/gpt-4.1-mini", "anthropic/claude-3.5-haiku", "google/gemini-2.5-flash", "meta-llama/llama-3.3-70b-instruct"};
static const char *const MODELS[] = {"openai/gpt-4o-mini-transcribe", "openai/gpt-4o-transcribe", "openai/whisper-large-v3-turbo", "openai/whisper-large-v3", "openai/whisper-1"};

static String option(const String &value, const String &label, const String &current) {
  return "<option value='" + html(value) + "'" + (value == current ? " selected" : "") + ">" + html(label) + "</option>";
}

static void noteCard(Out &p, const Note &n, bool first, const char *back) {
  p += "<div class='note" + String(first ? " first" : "") + "'>";
  p += "<div class='meta'>" + when(n.created) + " · " + costText(n.cost) + (n.pinned ? " · pinned" : "") + (n.asked ? " · chat" : "") + (n.orig.length() ? " · tidied" : "") + "</div>";
  if (n.text.length()) p += "<p>" + html(n.text) + "</p>";
  String wait = notesWaitText(n);   // the same words as on the board
  if (wait.length()) p += "<p class='pend'>" + html(wait) + "</p>";
  p += "<div class='row'><form method='post' action='/note'><input type='hidden' name='id' value='" + n.id + "'><input type='hidden' name='back' value='" + back + "'>";
  if (n.archived) p += "<button class='o' name='do' value='restore'>Restore</button>";
  else {
    p += "<button class='o' name='do' value='pin'>" + String(n.pinned ? "Unpin" : "Pin") + "</button>";
    if (!n.busy()) p += "<button class='o' name='do' value='archive'>Archive</button>";
    if (n.asking || n.tidying) p += "<button class='o' name='do' value='cancel'>Cancel</button>";   // the same third button as on the board
    else if (!n.busy() && n.orig.length()) p += "<button class='o' name='do' value='original'>Original</button>";
    else if (!n.busy() && n.text.length() && !n.asked) p += "<button class='o' name='do' value='tidy'>Tidy</button>";
  }
  bool canEdit = !n.archived && !n.busy();
  if (canEdit) p += "<button type='button' class='o' data-edit>Edit</button>";   // opens the box under the row
  p += "<button class='o d' name='do' value='delete' data-arm='Yes, delete'>Delete</button></form></div>";
  if (canEdit) p += "<form class='b edit' hidden method='post' action='/note'><input type='hidden' name='id' value='" + n.id + "'><input type='hidden' name='do' value='edit'><textarea name='text' aria-label='Edit note' maxlength='" + String(KB_NOTE_MAX) + "'>" + html(n.text) + "</textarea><p></p><input class='o' type='submit' value='Save'></form>";
  p += "</div>";
}

// The last action's Undo, at the bottom of the page for what is left of its 6 s.
static String undoToast(const char *back) {
  if (!undoWhat.length() || millis() - undoAt >= UNDO_SHOW) return "";
  uint32_t left = UNDO_SHOW - (millis() - undoAt);
  return "<div class='toast' id='undo'><span>" + html(undoWhat) + "</span><form method='post' action='/undo'><input type='hidden' name='back' value='" + back + "'><button class='o'>Undo</button></form></div>"
         "<script>setTimeout(function(){var u=document.getElementById('undo');if(u)u.remove()}," + String(left) + ")</script>";
}

static void costRows(Out &p, const char *title, int count, uint64_t (*sum)(int), String (*label)(int)) {
  p += "<h3>" + String(title) + "</h3><table>";
  for (int i = 0; i < count; i++) p += "<tr><td>" + label(i) + "</td><td>" + costText(sum(i)) + "</td></tr>";
  p += "</table>";
}

static const size_t PAGE_NOTES = 50;   // the newest notes on the page; "Show all" for the rest
// The page asks every 5 s whether anything changed (/state: a few bytes, and not counted as using the
// board, so an open tab never keeps it awake) and reloads when it did, unless you are typing or a part is open.
static String stateNow() { return String(notesVersion()) + "." + sttProblem().length() + "." + notesPending(); }
static const char RELOAD[] PROGMEM =
  "setInterval(function(){var a=document.activeElement;if(a&&/INPUT|TEXTAREA|SELECT/.test(a.tagName))return;"
  "if(document.querySelector('details[open],.edit:not([hidden]),button[data-armed]'))return;"
  "fetch('/state').then(function(r){return r.ok?r.text():V}).then(function(t){if(t!==V)location.reload()}).catch(function(){})},5000)</script>";

static void sendPage(const String &msg) {
  String name = cfg::str("device.name");
  auto &all = notesAll();
  std::vector<int> idx = notesSorted(0, true);
  bool showAll = server.arg("all") == "1";
  Out p;
  p.begin("text/html; charset=utf-8");
  p += head(name);
  int pend = notesPending(), arch = notesCount(true);
  p += "<header><h1>" + html(name) + "</h1><div class='row'><a class='btn o' href='" + String(READER) + "'>QR reader</a><form method='post' action='/logout'><button class='o' type='submit'>Log out</button></form></div></header><div class='chips'>";
  p += "<span class='chip'>Battery " + String(now.percent) + "%" + (now.usb ? ", charging" : "") + "</span>";
  p += "<span class='chip'>" + String(netUp() ? html(netName()) : "No Wi-Fi") + "</span>";
  p += sttConfigured() ? "<span class='chip'>OpenRouter key set</span>" : "<span class='chip warn'>No OpenRouter key</span>";
  p += "<span class='chip'>" + String(VERSION) + "</span></div>";
  if (msg.length()) p += "<div class='msg'>" + html(msg) + "</div>";
  if (strlen(sttLastError())) p += "<p class='pend'>Last problem with OpenRouter: " + html(sttLastError()) + "</p>";

  p += "<div class='card'><h2>New note</h2><form class='b' method='post' action='/new'><textarea name='text' aria-label='New note' placeholder='Type a note' maxlength='" + String(KB_NOTE_MAX) + "'></textarea><p></p><input class='p' type='submit' value='Save note'></form></div>";

  p += "<div class='card'><h2>Notes · " + String(idx.size()) + (pend ? ", " + String(pend) + " waiting" : String()) + "</h2>";
  if (idx.empty()) p += "<p class='mute'>None yet. Hold OK on the board and speak.</p>";
  bool first = true;
  size_t shown = 0;
  for (int i : idx) {
    if (!showAll && shown == PAGE_NOTES) { p += "<p style='margin:12px 0 0'><a class='btn o' href='/?all=1'>Show all " + String(idx.size()) + " notes</a></p>"; break; }
    noteCard(p, all[i], first, "/"); first = false; shown++;
  }
  p += "</div>";

  p += "<div class='card'><h2>Export</h2><p class='mute'>On the board, Settings → Export shows the notes as QR codes. Open the QR reader (button at the top), tap Scan and point the camera at the board; when the reader has every part, tap Archive all on the board.</p>";
  int fresh = 0;
  for (int i : idx) if (notesExportable(all[i])) fresh++;
  p += "<p class='mute'>Or export here: " + String(fresh) + " note" + (fresh == 1 ? "" : "s") + " as a Markdown file" + (cfg::flag("export.archive") ? ", which moves them to the Archive" : "") + ".</p><div class='row'>";
  if (fresh) p += "<form method='post' action='/export' onsubmit='setTimeout(function(){location.href=\"/\"},1500)'><input class='o' type='submit' value='Export these notes (.md)'></form>";
  p += "<a class='btn o' href='/notes.md'>All notes (.md)</a><a class='btn o' href='/notes.json'>JSON</a>" + (arch ? "<a class='btn o' href='/archive'>Archive · " + String(arch) + "</a>" : String()) + "</div></div>";

  std::vector<WifiCred> nets = cfg::wifiNetworks();
  p += "<div class='card'><h2>Wi-Fi</h2>";
  p += "<p class='mute'>" + String(netUp() ? "Connected to " + html(netName()) + ", " + netAddress() + ", " + String(netSignal()) + " dBm." : "Not connected.") + "</p>";
  for (size_t i = 0; i < nets.size(); i++) {
    p += "<div class='net" + String(i == 0 ? " first" : "") + "'><b>" + html(nets[i].ssid) + "</b><form method='post' action='/wifi'><input type='hidden' name='do' value='forget'><input type='hidden' name='ssid' value='" + html(nets[i].ssid) + "'><button class='d' data-arm='Yes, forget'>Forget</button></form></div>";
  }
  if (nets.empty()) p += "<p class='mute'>No saved networks.</p>";
  p += "<details><summary>Add a network</summary><form class='b' method='post' action='/wifi'><input type='hidden' name='do' value='add'><label for='ssid'>Network name</label><input type='text' id='ssid' name='ssid' autocapitalize='off' autocorrect='off'><label for='pass'>Password (empty for an open network)</label><input type='password' id='pass' name='pass' autocomplete='off'><p></p><input class='o' type='submit' value='Save network'></form></details></div>";

  p += "<div class='card'><h2>Settings</h2><form class='b' method='post' action='/settings'><input type='hidden' name='boxes' value='1'>";
  p += "<label for='key'>OpenRouter API key</label><div class='row' style='margin-bottom:6px'>" + String(sttConfigured() ? "<span class='mute' style='margin:0'>Set. Paste a new one below to replace it</span>" : "<span class='mute' style='margin:0;color:var(--destructive)'>Not set. Recordings wait until a key is set</span>") + "</div><input type='password' id='key' name='key' placeholder='sk-or-…' autocomplete='off'>";
  String lang = cfg::str("stt.language");
  p += "<label for='lang'>Transcription language</label><select id='lang' name='lang'>";
  p += option("", "Let the model choose", lang);
  p += option("pt", "Portuguese", lang);
  p += option("en", "English", lang);
  p += option("es", "Spanish", lang);
  p += option("fr", "French", lang);
  p += option("de", "German", lang);
  p += option("it", "Italian", lang);
  if (lang.length() && lang != "pt" && lang != "en" && lang != "es" && lang != "fr" && lang != "de" && lang != "it") p += option(lang, lang, lang);
  p += "</select>";
  String model = cfg::str("stt.model");
  p += "<label for='model'>Transcription model</label><select id='model' name='model'>";
  bool known = false;
  for (const char *m : MODELS) { p += option(m, m, model); known |= model == m; }
  if (!known) p += option(model, model, model);
  p += "</select>";
  p += "<label for='prompt'>Transcription style: how to write, and names or words it should know (a hint, best with an example)</label><textarea id='prompt' name='prompt' placeholder='Tudo em minúsculas, sem ponto final. Nomes: BrainOS, Ana. Exemplo: comprar café e leite'>" + html(cfg::str("stt.prompt")) + "</textarea>";
  String ask = cfg::str("ask.model");
  p += "<label for='askmodel'>Assistant model (hold Up to record a question; the answer goes into the note)</label><select id='askmodel' name='askmodel'>";
  known = false;
  for (const char *m : ASK_MODELS) { p += option(m, m, ask); known |= ask == m; }
  if (!known) p += option(ask, ask, ask);
  p += "</select>";
  p += "<label for='askprompt'>Assistant instructions (keep answers short: the screen is small)</label><textarea id='askprompt' name='askprompt'>" + html(cfg::str("ask.prompt")) + "</textarea>";
  p += "<label for='name'>Device name</label><input type='text' id='name' name='name' value='" + html(cfg::str("device.name")) + "'>";
  String tz = cfg::str("device.timezone");
  p += "<label for='tz'>Time zone (a network the board hasn't seen before sets it again)</label><select id='tz' name='tz'>";
  known = false;
  for (const Zone &z : ZONES) { p += option(z.tz, z.label, tz); known |= tz == z.tz; }
  if (!known) p += option(tz, "Custom: " + tz, tz);
  p += "</select>";
  p += "<label for='sleep'>On battery, sleep after this many minutes without a touch or a button (Wi-Fi and this page go off; OK wakes it)</label><input type='number' id='sleep' name='sleep' min='1' max='1440' value='" + String(cfg::num("sleep.min")) + "'>";
  p += "<label class='check'><input type='checkbox' name='archive' " + String(cfg::flag("export.archive") ? "checked" : "") + "> Archive notes after exporting</label>";
  p += "<p></p><input class='o' type='submit' value='Save settings'></form>";
  p += "<h2 style='margin-top:20px'>Costs</h2><p class='mute'>What OpenRouter charged for transcriptions and answers (a transcription it doesn't report is priced by its length). Deleting a note keeps its cost here.</p>";
  costRows(p, "Last 7 days", 7, costDaysAgo, costDayLabel);
  costRows(p, "Last 4 weeks (from Monday)", 4, costWeeksAgo, costWeekLabel);
  costRows(p, "Last 12 months", 12, costMonthsAgo, costMonthLabel);
  p += "<p class='mute'>Since the board was set up: " + costText(costTotal()) + "</p></div>";

  UpdInfo u = updInfo();
  p += "<div class='card'><h2>About</h2><p class='mute'>BrainOS " + String(VERSION) + " on " + html(name) + ". " + (u.checkedMs ? html(u.status) : String("Updates are checked only when you ask.")) + "</p><div class='row'><form method='post' action='/update'><button class='o' name='do' value='check'>Check for update</button>";
  if (u.available) p += "<button class='o' name='do' value='install' data-arm='Yes, install'>Install " + html(u.latest) + "</button>";
  p += "</form></div></div>";
  p += "</div>" + undoToast("/") + "<script>" + String(FPSTR(JS)) + "</script><script>var V='" + stateNow() + "';" + String(FPSTR(RELOAD)) + "</body></html>";
  p.end();
}

// A form's answer is a redirect to the page, with one line to show there: a reload never sends the form again.
static String flash;
static void handleRoot() { String m = flash; flash = ""; sendPage(m); }

static void back(const char *where) {
  server.sendHeader("Location", where);
  server.send(303, "text/plain", "");
}

// ---------------------------------------------------------------- the password
// The first visit makes the page's password; after that a browser gives it once and carries a
// cookie (kept across the board's sleeps). The cookie belongs to the board's own name, so a page
// elsewhere pointed at the board comes without it. Forgotten: About on the board resets it.

static String hex(const uint8_t *b, size_t n) { String s; char t[3]; for (size_t i = 0; i < n; i++) { snprintf(t, sizeof t, "%02x", b[i]); s += t; } return s; }
static String randomHex(size_t bytes) { uint8_t b[32]; esp_fill_random(b, bytes); return hex(b, bytes); }
static String hashOf(const String &salt, const String &pass) {
  String in = salt + pass; uint8_t d[32];
  mbedtls_sha256((const uint8_t *)in.c_str(), in.length(), d, 0);
  return hex(d, 32);
}
bool webHasPassword() { return cfg::str("web.pass").length() > 0; }
static uint32_t setupArmedAt;
static const uint32_t SETUP_MS = 120000;
bool webSetupOpen() { return setupArmedAt && millis() - setupArmedAt < SETUP_MS; }
void webArmSetup() { setupArmedAt = millis(); }
void webForgetPassword() { String e; cfg::setStr("web.pass", "", e); cfg::setStr("web.session", "", e); webArmSetup(); }
static bool passwordIs(const String &pass) {
  String kept = cfg::str("web.pass");
  int cut = 0;
  return webPassSplit(kept.c_str(), &cut) && hashOf(kept.substring(0, cut), pass) == kept.substring(cut + 1);
}
static bool signedIn() {
  String want = cfg::str("web.session"), c = server.header("Cookie");
  if (!want.length()) return false;
  for (int at = 0; at >= 0 && at < (int)c.length();) {   // "a=1; s=<token>"
    int end = c.indexOf(';', at);
    String part = c.substring(at, end < 0 ? c.length() : end); part.trim();
    if (part == "s=" + want) return true;
    at = end < 0 ? -1 : end + 1;
  }
  return false;
}
static void signIn() { server.sendHeader("Set-Cookie", "s=" + cfg::str("web.session") + "; Max-Age=2592000; Path=/; HttpOnly; SameSite=Lax"); }
static void signOutCookie() { server.sendHeader("Set-Cookie", "s=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax"); }

static int wrongTries;
static uint32_t lockedAt;   // five wrong passwords: a minute's wait
static bool lockedOut() { return wrongTries >= 5 && millis() - lockedAt < 60000; }

// The page before the password: make one (the first visit), or give it.
static void gatePage() {
  bool make = !webHasPassword();
  String name = cfg::str("device.name"), msg = flash; flash = "";
  String p = head(name) + "<header><h1>" + html(name) + "</h1></header>";
  if (msg.length()) p += "<div class='msg'>" + html(msg) + "</div>";
  if (make && !webSetupOpen()) p += "<div class='card'><h2>Create a password</h2><p class='mute'>This page stays closed until you ask on the board: Settings, About, Create page password. You then have two minutes to choose a password here, 8 characters or more, one you use nowhere else.</p></div>";
  else if (make) p += "<div class='card'><h2>Create a password</h2><p class='mute'>This page shows your notes to anyone on the same Wi-Fi. Choose a password for it, 8 characters or more, one you use nowhere else. The board opened this window for two minutes.</p>"
                 "<form class='b' method='post' action='/setup'><label for='p1'>Password</label><input type='password' id='p1' name='p1' minlength='8' maxlength='64' autocomplete='new-password' required>"
                 "<label for='p2'>Again</label><input type='password' id='p2' name='p2' minlength='8' maxlength='64' autocomplete='new-password' required><p></p><input class='p' type='submit' value='Save password'></form></div>";
  else p += "<div class='card'><h2>Password</h2><form class='b' method='post' action='/login'><label for='p'>This page's password (forgotten? On the board: Settings, About, Reset page password)</label>"
            "<input type='password' id='p' name='p' maxlength='64' autocomplete='current-password' required autofocus><p></p><input class='p' type='submit' value='Open'></form></div>";
  server.send(200, "text/html; charset=utf-8", p + "</div></body></html>");
}

static void handleSetup() {
  String a = server.arg("p1"), err;
  if (webHasPassword()) { back("/"); return; }   // made already: this form is no way around it
  if (!webSetupOpen()) { flash = "On the board: Settings, About, Create page password"; back("/"); return; }
  if (a != server.arg("p2")) flash = "The two passwords differ";
  else if (a.length() < 8 || a.length() > 64) flash = "A password has 8 to 64 characters";
  else {
    String salt = randomHex(4);
    if (cfg::setStr("web.session", randomHex(24), err) && cfg::setStr("web.pass", salt + "$" + hashOf(salt, a), err)) { setupArmedAt = 0; signIn(); flash = "Password saved"; }
    else flash = "Not saved: " + err;
  }
  back("/");
}

static void handleLogin() {
  if (lockedOut()) flash = "Too many tries: wait a minute";
  else if (passwordIs(server.arg("p"))) { wrongTries = 0; signIn(); }
  else { if (wrongTries >= 5) wrongTries = 0; if (++wrongTries >= 5) lockedAt = millis(); flash = "Wrong password"; }
  back("/");
}

static void handleLogout() {
  String err;
  cfg::setStr("web.session", randomHex(24), err);
  signOutCookie();
  flash = "Logged out";
  back("/");
}

static String plain(String e) {   // a setting's error in the form's words
  static const char *const NAMES[][2] = {{"stt.api_key", "The OpenRouter key"}, {"stt.model", "The transcription model"}, {"stt.language", "The transcription language"}, {"stt.prompt", "The transcription style"},
    {"ask.model", "The assistant model"}, {"ask.prompt", "The assistant instructions"}, {"device.name", "The device name"}, {"device.timezone", "The time zone"}, {"sleep.min", "The minutes before sleep"}};
  for (auto &n : NAMES) e.replace(n[0], n[1]);
  e.replace(" bytes", " characters");
  return e;
}

// Only the fields that came with the form change; checkboxes come as a set (boxes=1).
static void handleSettings() {
  String err, key = server.arg("key");
  key.trim();   // a pasted key often brings a space or a line break
  if (key.length()) cfg::setStr("stt.api_key", key, err);
  if (!err.length() && server.hasArg("lang")) cfg::setStr("stt.language", server.arg("lang"), err);
  if (!err.length() && server.hasArg("model")) cfg::setStr("stt.model", server.arg("model"), err);
  if (!err.length() && server.hasArg("prompt")) { String t = server.arg("prompt"); t.replace("\r", ""); t.replace("\n", " "); t.trim(); cfg::setStr("stt.prompt", t, err); }
  if (!err.length() && server.hasArg("askmodel")) cfg::setStr("ask.model", server.arg("askmodel"), err);
  if (!err.length() && server.hasArg("askprompt")) { String t = server.arg("askprompt"); t.replace("\r", ""); t.replace("\n", " "); t.trim(); cfg::setStr("ask.prompt", t, err); }
  if (!err.length() && server.hasArg("name")) cfg::setStr("device.name", server.arg("name"), err);
  if (!err.length() && server.hasArg("tz") && cfg::setStr("device.timezone", server.arg("tz"), err)) {
    setenv("TZ", server.arg("tz").c_str(), 1); tzset();
    cfg::setFlag("device.tz_manual", true, err);
  }
  if (!err.length() && server.arg("sleep").length()) cfg::setNum("sleep.min", server.arg("sleep").toInt(), err);
  if (!err.length() && server.hasArg("boxes")) cfg::setFlag("export.archive", server.hasArg("archive"), err);
  sttRetry();   // a new key or model: what was refused goes out again
  uiNotesChanged();
  flash = err.length() ? "Not saved: " + plain(err) + ". The fields above it were saved." : "Saved";
  back("/");
}

static void handleWifi() {
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  String d = server.arg("do"), ssid = server.arg("ssid"), err, msg;
  if (d == "forget") {
    msg = cfg::setWifi(cfg::wifiWithout(ssid), err) ? "Forgot " + ssid : "Not saved: " + err;
  } else if (d == "add") {
    nets = cfg::wifiWithout(ssid);
    nets.push_back({ssid, server.arg("pass")});
    msg = cfg::setWifi(nets, err) ? "Saved " + ssid + ": the board joins it when its current network is out of reach" : "Not saved: " + err;
  }
  uiNotesChanged();
  flash = msg;
  back("/");
}

static const char *TOO_LONG = "Not saved: a note holds at most 4000 characters";
static bool tooLong(const String &t) { return utf8Count(t.c_str()) > KB_NOTE_MAX; }

static void handleNote() {
  Note *n = notesFind(server.arg("id"));
  String d = server.arg("do");
  if (n) {
    if (d == "edit") {
      String t = server.arg("text"); t.trim();
      if (tooLong(t)) { flash = TOO_LONG; back("/"); return; }
    }
    if (d == "delete") { undoStart("Deleted", true); undoNotes.push_back(*n); }
    else if (d == "archive" && !n->busy()) { undoStart("Archived", false); undoNotes.push_back(*n); }
    if (!notesApply(n->id, d.c_str(), server.arg("text"), d == "delete")) {
      if (d == "delete" || d == "archive") { undoNotes.clear(); undoWhat = ""; }
      flash = NOTES_SAVE_FAIL;
    }
  }
  uiNotesChanged();
  back(server.arg("back") == "/archive" ? "/archive" : "/");   // only our own two pages
}

static void handleNew() {
  String t = server.arg("text"); t.trim();
  if (tooLong(t)) { flash = TOO_LONG; back("/"); return; }
  if (t.length() && !notesAdd(t, "", 0)) flash = NOTES_SAVE_FAIL;
  uiNotesChanged();
  back("/");
}

static String indented(String t) { t.replace("\n", "\n  "); return t; }   // a note's own line breaks stay inside its list item

// A note in the Markdown: a list item with the date, its marks, the text, and the text before Tidy.
static String item(const Note &n, bool showArchived) {
  String s = "- **" + when(n.created) + "**" + (showArchived && n.archived ? " (archived)" : "") + (n.pinned ? " (pinned)" : "") + (n.asked ? " (asked)" : "") + "\n  ";
  s += n.text.length() ? indented(n.text) : String("(recording waiting for transcription)");
  if (n.orig.length()) s += "\n\n  (original: " + indented(n.orig) + ")";
  return s + "\n";
}

static void handleExport() {
  auto &all = notesAll();
  std::vector<int> idx;
  for (int i : notesSorted(0, false)) if (notesExportable(all[i])) idx.push_back(i);
  bool archive = cfg::flag("export.archive");
  if (archive && !idx.empty()) undoStart("Exported and archived", false);
  server.sendHeader("Content-Disposition", "attachment; filename=brainos-export.md");
  Out p;
  p.begin("text/markdown; charset=utf-8");
  p += "# " + cfg::str("device.name") + ", exported " + when(time(nullptr)) + "\n\n";
  for (int i : idx) {
    Note &n = all[i];
    p += item(n, false);
    if (archive) { undoNotes.push_back(n); n.archived = true; n.pinned = false; }
  }
  p.end();
  if (archive && !idx.empty()) {
    if (!notesSave()) {
      for (auto &was : undoNotes) {
        Note *n = notesFind(was.id);
        if (n) { n->archived = was.archived; n->pinned = was.pinned; }
      }
      undoNotes.clear(); undoWhat = "";
      flash = NOTES_SAVE_FAIL;
    }
  }
  uiNotesChanged();
}

static void handleUpdate() {
  bool install = server.arg("do") == "install" && updInfo().available;
  updateRequest(install);
  String p = head(install ? "Installing" : "Checking", "<meta http-equiv='refresh' content='" + String(install ? 90 : 12) + ";url=/'>");
  p += "<div class='card'><h2>" + String(install ? "Installing" : "Checking for an update") + "</h2><p class='mute'>" + (install ? "The board downloads the release, checks it and restarts. This page comes back in a minute and a half." : "This page comes back in a few seconds.") + "</p></div></div></body></html>";
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleArchive() {
  Out p;
  p.begin("text/html; charset=utf-8");
  p += head("Archive");
  p += "<header><h1>Archive</h1><a class='btn o' href='/'>Notes</a></header><div class='card'>";
  bool first = true, any = false;
  for (int i : notesSorted(1, true)) { noteCard(p, notesAll()[i], first, "/archive"); first = false; any = true; }
  if (!any) p += "<p class='mute'>The archive is empty.</p>";
  p += "</div></div>" + undoToast("/archive") + "<script>" + String(FPSTR(JS)) + "</script></body></html>";
  p.end();
}

static void handleMd() {
  server.sendHeader("Content-Disposition", "attachment; filename=notes.md");
  Out p;
  p.begin("text/markdown; charset=utf-8");
  p += "# " + cfg::str("device.name") + "\n\n";
  for (int i : notesSorted(2, true)) p += item(notesAll()[i], true);
  p.end();
}

static void handleJson() {
  Out p;
  p.begin("application/json");
  p += "[";
  bool first = true;
  for (auto &n : notesAll()) {
    if (!first) p += ",";
    first = false;
    p += "{\"id\":\"" + n.id + "\",\"text\":" + pj::esc(n.text) + ",\"created\":" + n.created + ",\"pinned\":" + (n.pinned ? "true" : "false") + ",\"archived\":" + (n.archived ? "true" : "false") + ",\"asked\":" + (n.asked ? "true" : "false") + ",\"pending\":" + (n.busy() ? "true" : "false") + ",\"cost_usd\":" + String(n.cost / 1e6, 6) + (n.orig.length() ? ",\"original\":" + pj::esc(n.orig) : String()) + "}";
  }
  p += "]";
  p.end();
}

static void handleUndo() {
  if (undoLive()) {
    for (auto &was : undoNotes) {
      Note *n = notesFind(was.id);
      if (!n) { if (undoDelete) notesAll().push_back(was); }   // a deleted note comes back whole
      else { n->archived = was.archived; n->pinned = was.pinned; }   // an archived one only goes back to the list
    }
    if (!notesSave()) flash = NOTES_SAVE_FAIL;
    else { undoNotes.clear(); undoWhat = ""; }
    uiNotesChanged();
  }
  back(server.arg("back") == "/archive" ? "/archive" : "/");
}

// A change asked for by another web page (one open in the same browser, on another site) is
// refused: the browser says where a form came from, and it has to be this page.
static bool fromThisPage() {
  String host = server.hostHeader(), origin = server.header("Origin"), ref = server.header("Referer");
  if (origin.length()) return origin == "http://" + host;
  if (ref.length()) return ref.startsWith("http://" + host + "/");
  return true;   // no browser around it (a script on the network): nothing to check against
}

// Every request: the board stays awake while the page is in use (`counts`); a change must come
// from this page; and nothing is shown or changed before the password.
static void route(const char *path, HTTPMethod method, void (*fn)(), bool counts = true, bool open = false) {
  server.on(path, method, [method, fn, counts, open] {
    if (counts) lastActivity = millis();
    if (method == HTTP_POST && !fromThisPage()) { server.send(403, "text/plain", "Only the page at brain.local can change the board"); return; }
    if (!open && !signedIn()) {
      if (method == HTTP_POST) back("/");
      else if (counts) gatePage();
      else server.send(401, "text/plain", "");
      return;
    }
    fn();
  });
}

void webBegin() {
  const char *headers[] = {"Origin", "Referer", "Cookie"};
  server.collectHeaders(headers, 3);
  route("/setup", HTTP_POST, handleSetup, true, true);
  route("/login", HTTP_POST, handleLogin, true, true);
  route("/logout", HTTP_POST, handleLogout);
  route("/", HTTP_GET, handleRoot);
  route("/settings", HTTP_POST, handleSettings);
  route("/wifi", HTTP_POST, handleWifi);
  route("/note", HTTP_POST, handleNote);
  route("/new", HTTP_POST, handleNew);
  route("/export", HTTP_POST, handleExport);
  route("/update", HTTP_POST, handleUpdate);
  route("/undo", HTTP_POST, handleUndo);
  route("/archive", HTTP_GET, handleArchive);
  route("/notes.md", HTTP_GET, handleMd);
  route("/notes.json", HTTP_GET, handleJson);
  route("/state", HTTP_GET, [] { server.send(200, "text/plain", stateNow()); }, false);
  server.onNotFound([] { server.send(404, "text/plain", "Not here"); });
}

void webTick() {
  if (undoWhat.length() && millis() - undoAt >= UNDO_KEEP) undoForget();   // too late to undo: a deleted recording's file goes now
  if (!netUp()) { if (started) { server.stop(); started = false; } return; }
  if (!started) { server.begin(); started = true; Serial.println("web: http://brain.local"); }
  server.handleClient();
}
