#include "chat.h"
#include <LittleFS.h>
#include <algorithm>
#include <time.h>
#include "config.h"
#include "provjson.h"

PassChat chat;

void chatClear() {
  if (chat.audio.length()) LittleFS.remove(chat.audio);
  int gen = chat.gen + 1;
  chat = PassChat();
  chat.gen = gen;
}

void chatRecorded(const String &path, uint16_t secs) {
  if (chat.audio.length()) LittleFS.remove(chat.audio);
  chat.audio = path; chat.secs = secs; chat.note = "";
}

Note *chatSave() {
  if (chat.waiting() || chat.turns.empty()) return nullptr;
  Note &n = notesAdd(chatJoin(chat.turns), "", 0);
  n.asked = true;
  n.cost = chat.cost;
  notesSave();
  chatClear();
  return &n;
}

std::vector<String> chatTurnsOf(const String &text) {
  std::vector<String> out;
  int at = 0;
  while (at <= (int)text.length()) {
    int cut = text.indexOf("\n\n", at);
    String t = text.substring(at, cut < 0 ? text.length() : cut);
    t.trim();
    if (t.length()) out.push_back(t);
    if (cut < 0) break;
    at = cut + 2;
  }
  return out;
}

String chatJoin(const std::vector<String> &turns) {
  String s;
  for (auto &t : turns) { if (s.length()) s += "\n\n"; s += t; }
  return s;
}

String chatAnswerText(String a) {
  a.trim();
  while (a.indexOf("\n\n") >= 0) a.replace("\n\n", "\n");
  return a;
}

static String day(uint32_t t) {
  time_t s = t; struct tm tm; localtime_r(&s, &tm);
  char b[24]; strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tm);
  return b;
}

// The notes on the list, for a question asked outside a note: the pinned ones always, then the
// newest, up to 30 notes or about 6000 characters, whichever comes first.
static String notesContext() {
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) if (!all[i].archived && all[i].text.length()) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) {
    if (all[a].pinned != all[b].pinned) return all[a].pinned;
    return all[a].created > all[b].created;
  });
  String s;
  int n = 0;
  for (int i : idx) {
    const Note &x = all[i];
    String line = "- " + day(x.created) + (x.pinned ? " (pinned)" : "") + ": " + x.text + "\n";
    if (!x.pinned && (n >= 30 || s.length() + line.length() > 6000)) break;
    s += line; n++;
  }
  return s;
}

String askBodyFor(const std::vector<String> &turns, bool withNotes) {
  String sys = cfg::str("ask.prompt");
  if (withNotes) {
    String ctx = notesContext();
    if (ctx.length()) sys += String(sys.length() ? "\n\n" : "") + "These are the user's notes, from their notebook, for context:\n" + ctx;
  }
  String body = "{\"model\":" + pj::esc(cfg::str("ask.model")) + ",\"max_tokens\":500,\"usage\":{\"include\":true},\"messages\":[";
  if (sys.length()) body += "{\"role\":\"system\",\"content\":" + pj::esc(sys) + "},";
  // The last 10 exchanges and the new question: an odd count, so the first one is a question.
  size_t from = turns.size() > 21 ? turns.size() - 21 : 0;
  if ((turns.size() - from) % 2 == 0 && from < turns.size()) from++;
  for (size_t i = from; i < turns.size(); i++) {
    bool user = (turns.size() - 1 - i) % 2 == 0;   // the last turn is the question
    body += String(i > from ? "," : "") + "{\"role\":\"" + (user ? "user" : "assistant") + "\",\"content\":" + pj::esc(turns[i]) + "}";
  }
  return body + "]}";
}

String tidyBodyFor(const String &text) {
  String sys = "You tidy a note that was dictated and transcribed. Fix punctuation and capitals, and remove filler words, "
               "hesitations and false starts (like um, uh, tipo, né, então, assim, quer dizer), without changing the meaning, "
               "the wording or the language. Keep the paragraphs and line breaks. Reply with the tidied note only.";
  return "{\"model\":" + pj::esc(cfg::str("ask.model")) + ",\"max_tokens\":1500,\"usage\":{\"include\":true},\"messages\":[{\"role\":\"system\",\"content\":" +
         pj::esc(sys) + "},{\"role\":\"user\",\"content\":" + pj::esc(text) + "}]}";
}
