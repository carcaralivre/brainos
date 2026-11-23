#include "notes.h"
#include <LittleFS.h>
#include <esp_random.h>
#include "provjson.h"

static const char *FILE_NAME = "/notes.jsonl";
static std::vector<Note, PsAlloc<Note>> items;

std::vector<Note, PsAlloc<Note>> &notesAll() { return items; }

String notesNewId() {
  char b[9];
  uint32_t r = esp_random();
  snprintf(b, sizeof(b), "%08lx", (unsigned long)(r ^ (uint32_t)time(nullptr) << 8));
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
}

void notesSave() {
  File f = LittleFS.open("/notes.tmp", "w");
  if (!f) { Serial.println("notes: can't write"); return; }
  for (auto &n : items) f.println(lineOf(n));
  f.close();
  LittleFS.rename("/notes.tmp", FILE_NAME);   // rename replaces the old file in one step: a power cut keeps one or the other
}

void notesPin(Note &n) {
  if (n.pinned) { n.pinned = false; n.modified = time(nullptr); notesSave(); return; }
  for (;;) {
    int count = 0, oldest = -1;
    for (size_t i = 0; i < items.size(); i++) if (items[i].pinned) { count++; if (oldest < 0 || items[i].modified < items[oldest].modified) oldest = i; }
    if (count < 3) break;
    items[oldest].pinned = false;
  }
  n.pinned = true;
  n.modified = time(nullptr);
  notesSave();
}

Note *notesFind(const String &id) {
  for (auto &n : items) if (n.id == id) return &n;
  return nullptr;
}

Note &notesAdd(const String &text, const String &audio, uint16_t secs, const String &id) {
  Note n;
  n.id = id.length() ? id : notesNewId();
  n.text = text;
  n.audio = audio;
  n.secs = secs;
  n.created = n.modified = (uint32_t)time(nullptr);
  items.push_back(n);
  notesSave();
  return items.back();
}

void notesRemove(const String &id, bool keepAudio) {
  for (size_t i = 0; i < items.size(); i++)
    if (items[i].id == id) {
      if (items[i].audio.length() && !keepAudio) LittleFS.remove(items[i].audio);
      items.erase(items.begin() + i);
      notesSave();
      return;
    }
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
