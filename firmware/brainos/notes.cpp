#include "notes.h"
#include <LittleFS.h>
#include <esp_random.h>
#include <algorithm>
#include <cstring>
#include "provjson.h"
#include "stt.h"

static const char *FILE_NAME = "/notes.jsonl";
static std::vector<Note, PsAlloc<Note>> items;
static uint32_t version;
uint32_t notesVersion() { return version; }

std::vector<Note, PsAlloc<Note>> &notesAll() { return items; }

String notesNewId() {
  char b[9];
  for (int n = 0; n < 8; n++) {
    uint32_t r = esp_random();
    snprintf(b, sizeof(b), "%08lx", (unsigned long)(r ^ (uint32_t)time(nullptr) << 8 ^ (uint32_t)n));
    if (!notesFind(b)) return String(b);
  }
  return String(b);
}

static bool parseLine(const String &line, Note &n) {
  pj::Val v, f;
  if (!pj::parse(line.c_str(), line.length(), v) || v.type != pj::OBJ) return false;
  long num; bool b;
  n = Note();
  if (!pj::member(v, "id", f) || !pj::str(f, n.id)) return false;
  if (pj::member(v, "t", f)) pj::str(f, n.text);
  if (pj::member(v, "a", f)) pj::str(f, n.audio);
  if (pj::member(v, "c", f) && pj::num(f, num)) n.created = num;
  if (pj::member(v, "m", f) && pj::num(f, num)) n.modified = num;
  if (pj::member(v, "s", f) && pj::num(f, num)) n.secs = num;
  if (pj::member(v, "p", f) && pj::boolean(f, b)) n.pinned = b;
  if (pj::member(v, "ar", f) && pj::boolean(f, b)) n.archived = b;
  if (pj::member(v, "o", f)) pj::str(f, n.orig);
  if (pj::member(v, "k", f) && pj::num(f, num)) n.cost = num;
  if (pj::member(v, "td", f) && pj::boolean(f, b)) n.tidying = b;
  if (pj::member(v, "q", f) && pj::boolean(f, b)) n.asked = b;
  if (pj::member(v, "qw", f) && pj::boolean(f, b)) n.asking = b;
  return true;
}

static String lineOf(const Note &n) {
  String s = "{\"id\":\"" + n.id + "\",\"t\":" + pj::esc(n.text) + ",\"c\":" + String(n.created) + ",\"m\":" + String(n.modified);
  if (n.audio.length()) s += ",\"a\":" + pj::esc(n.audio) + ",\"s\":" + String(n.secs);
  if (n.pinned) s += ",\"p\":true";
  if (n.archived) s += ",\"ar\":true";
  if (n.orig.length()) s += ",\"o\":" + pj::esc(n.orig);
  if (n.cost) s += ",\"k\":" + String(n.cost);
  if (n.tidying) s += ",\"td\":true";
  if (n.asked) s += ",\"q\":true";
  if (n.asking) s += ",\"qw\":true";
  return s + "}";
}

// Recordings no note points to (a power cut while saving, a delete that was never undone) would
// fill the flash a megabyte at a time: they go at start-up.
static void sweepQueue() {
  std::vector<String> stray;
  File d = LittleFS.open("/queue");
  if (!d) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String name = f.name();
    String path = "/queue/" + name.substring(name.lastIndexOf('/') + 1);
    bool used = false;
    for (auto &n : items) if (n.audio == path) used = true;
    if (!used) stray.push_back(path);
  }
  d.close();
  for (auto &p : stray) {
    LittleFS.remove(p);   // also the ".i" markers v0.8.0 to v0.8.8 left
    Serial.printf("notes: %s belonged to no note, removed\n", p.c_str());
  }
}

void notesBegin() {
  items.clear();
  if (!LittleFS.exists("/queue")) LittleFS.mkdir("/queue");
  File f = LittleFS.open(FILE_NAME, "r");
  if (!f) { Serial.println("notes: none saved"); return; }
  int bad = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    Note n;
    if (parseLine(line, n)) items.push_back(n); else bad++;
  }
  f.close();
  Serial.printf("notes: %u loaded%s\n", (unsigned)items.size(), bad ? " (some lines unreadable)" : "");
  if (!bad) sweepQueue();
}

bool notesSave() {
  File f = LittleFS.open("/notes.tmp", "w");
  if (!f) { Serial.println("notes: can't write"); return false; }
  bool ok = true;
  for (auto &n : items) {
    String line = lineOf(n) + "\n";
    if (f.write((const uint8_t *)line.c_str(), line.length()) != line.length()) { ok = false; break; }
  }
  f.close();
  if (!ok) {   // the flash is full: keep the old file whole rather than swap in a cut one
    LittleFS.remove("/notes.tmp");
    Serial.println("notes: NOT SAVED, the flash is full");
    return false;
  }
  LittleFS.rename("/notes.tmp", FILE_NAME);   // rename replaces the old file in one step: a power cut keeps one or the other
  version++;
  return true;
}

bool notesPin(Note &n) {
  if (n.pinned) { n.pinned = false; n.modified = time(nullptr); return notesSave(); }
  for (;;) {
    int count = 0, oldest = -1;
    for (size_t i = 0; i < items.size(); i++) if (items[i].pinned) { count++; if (oldest < 0 || items[i].modified < items[oldest].modified) oldest = i; }
    if (count < 3) break;
    items[oldest].pinned = false;
  }
  n.pinned = true;
  n.modified = time(nullptr);
  return notesSave();
}

Note *notesFind(const String &id) {
  for (auto &n : items) if (n.id == id) return &n;
  return nullptr;
}

Note *notesAdd(const String &text, const String &audio, uint16_t secs, const String &id) {
  Note n;
  n.id = id.length() ? id : notesNewId();
  n.text = text;
  n.audio = audio;
  n.secs = secs;
  n.created = n.modified = (uint32_t)time(nullptr);
  items.push_back(n);
  if (!notesSave()) {
    items.pop_back();
    return nullptr;
  }
  return &items.back();
}

bool notesRemove(const String &id, bool keepAudio) {
  for (size_t i = 0; i < items.size(); i++)
    if (items[i].id == id) {
      Note kept = items[i];
      items.erase(items.begin() + i);
      if (!notesSave()) { items.insert(items.begin() + i, kept); return false; }
      if (kept.audio.length() && !keepAudio) LittleFS.remove(kept.audio);
      return true;
    }
  return false;
}

bool notesRemoveArchived() {
  std::vector<String> audio;
  std::vector<Note, PsAlloc<Note>> keep;
  for (auto &n : items) {
    if (n.archived) { if (n.audio.length()) audio.push_back(n.audio); }
    else keep.push_back(n);
  }
  auto was = items;
  items = keep;
  if (!notesSave()) { items = was; return false; }
  for (auto &p : audio) LittleFS.remove(p);
  return true;
}

bool notesApply(const String &id, const char *op, const String &text, bool keepAudio) {
  Note *n = notesFind(id);
  if (!n || !op) return false;
  if (!strcmp(op, "pin")) { if (n->archived) return true; return notesPin(*n); }
  if (!strcmp(op, "archive")) {
    if (n->busy() || n->archived) return true;
    n->archived = true; n->pinned = false; n->modified = time(nullptr); return notesSave();
  }
  if (!strcmp(op, "restore")) {
    if (!n->archived) return true;
    n->archived = false; n->modified = time(nullptr); return notesSave();
  }
  if (!strcmp(op, "delete")) return notesRemove(id, keepAudio);
  if (!strcmp(op, "tidy")) {
    if (n->archived || n->busy() || !n->text.length() || n->asked) return true;
    n->tidying = true; return notesSave();
  }
  if (!strcmp(op, "cancel")) {
    if (!(n->asking || n->tidying)) return true;
    if (n->asking) n->text += "\n\n(no answer: cancelled)";
    n->asking = n->tidying = false; n->modified = time(nullptr); return notesSave();
  }
  if (!strcmp(op, "original")) {
    if (n->busy() || !n->orig.length()) return true;
    n->text = n->orig; n->orig = ""; n->modified = time(nullptr); return notesSave();
  }
  if (!strcmp(op, "edit")) {
    if (n->archived || n->busy()) return true;
    String t = text; t.trim();
    if (!t.length() || t == n->text) return true;
    n->text = t; n->orig = ""; n->modified = time(nullptr); return notesSave();
  }
  return true;
}

int notesCount(bool archived) {
  int c = 0;
  for (auto &n : items) if (n.archived == archived) c++;
  return c;
}

int notesPending() {
  int c = 0;
  for (auto &n : items) if (n.busy()) c++;
  return c;
}

std::vector<int> notesSorted(int which, bool newestFirst) {
  std::vector<int> idx;
  for (size_t i = 0; i < items.size(); i++) if (which == 2 || items[i].archived == (which == 1)) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return newestFirst ? items[a].created > items[b].created : items[a].created < items[b].created; });
  return idx;
}

String notesWaitText(const Note &n) {
  if (!n.busy()) return "";
  String why = n.stuck.length() ? n.stuck : sttProblem();
  if (n.audio.length()) return String(n.asked ? "Question, " : "Recording, ") + n.secs + " s, " + (why.length() ? why : String("sending"));
  String what = n.asking ? "asking" : "tidying";
  return why.length() ? "(" + what + ": " + why + ")" : "(" + what + "…)";
}

void notesFixClock(uint32_t bootTime) {
  bool changed = false;
  for (auto &n : items) {
    if (n.created < 1700000000) { n.created += bootTime; changed = true; }
    if (n.modified < 1700000000) { n.modified += bootTime; changed = true; }
  }
  if (changed) { Serial.println("notes: dated by the clock that was just set"); notesSave(); }
}
