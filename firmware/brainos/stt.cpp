#include "stt.h"
#include <atomic>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include "config.h"
#include "provjson.h"
#include "net.h"
#include "notes.h"
#include "chat.h"
#include "cost.h"

// A job: the WAV bytes (read by the loop; this task never touches flash), or a chat request
// body built on the loop, and the answer.
enum JobKind : uint8_t { JOB_STT, JOB_ASK, JOB_TIDY };
static struct {
  JobKind kind;
  String noteId;            // the note it is for; "" for the passing chat
  int chatGen;              // the passing chat it is for
  uint8_t *wav; size_t len;
  int secs;                 // the recording's length, for its cost
  String req;               // JOB_ASK / JOB_TIDY: the request body
  String text, error;
  uint32_t cost;            // millionths of a dollar
  bool fatal;               // the service refused this for good: stop retrying it
  std::atomic<int> state;   // 0 idle, 1 running, 2 done; atomic so the strings above are visible to the other core
} job;
static TaskHandle_t worker;
static String lastError;
static uint32_t lastFailAt;
static int failures;

bool sttRunning() { return job.state == 1; }
const char *sttLastError() { return lastError.c_str(); }
bool sttConfigured() { return cfg::isSet("stt.api_key"); }

static String logSafe(const String &s, int n) { String t = s.substring(0, n); t.replace("\n", " "); return t; }

static int post(const char *path, const uint8_t *body, size_t len, const String &type, String &reply) {
  String headers = "Authorization: Bearer " + cfg::str("stt.api_key") + "\r\nHTTP-Referer: https://github.com/carcaralivre/brainos\r\nX-Title: BrainOS\r\nContent-Type: " + type + "\r\n";
  return netPost("openrouter.ai", path, headers, body, len, reply, 90000);
}

static bool textOf(const String &reply, String &text) {   // {"text": "..."} from the service
  pj::Val v, f;
  return pj::parse(reply.c_str(), reply.length(), v) && pj::member(v, "text", f) && pj::str(f, text);
}
static bool answerOf(const String &reply, String &text, uint32_t &cost) {   // choices[0].message.content, usage.cost (dollars)
  pj::Val v, a, c, m, t, u, k;
  if (!pj::parse(reply.c_str(), reply.length(), v) || !pj::member(v, "choices", a) || !pj::at(a, 0, c) || !pj::member(c, "message", m) || !pj::member(m, "content", t) || !pj::str(t, text)) return false;
  cost = 0;
  if (pj::member(v, "usage", u) && pj::member(u, "cost", k) && k.type == pj::NUM) cost = (uint32_t)(strtod(k.p, nullptr) * 1e6 + 0.5);   // the number ends where strtod stops
  return true;
}

// The assistant (a question or Tidy): the body was built on the loop.
static void chatTask() {
  uint32_t start = millis();
  String reply;
  int code = post("/api/v1/chat/completions", (const uint8_t *)job.req.c_str(), job.req.length(), "application/json", reply);
  if (code != 200) {
    job.error = netFailText("OpenRouter", code);
    job.fatal = code >= 400 && code < 500 && code != 429 && code != 401;
    if (reply.indexOf("model") >= 0 && code == 400) job.error = "OpenRouter doesn't know the assistant model";
    Serial.printf("ask: failed, %d: %s\n", code, logSafe(reply, 200).c_str());
  } else if (!answerOf(reply, job.text, job.cost)) {
    job.error = "The assistant sent no answer";
    Serial.printf("ask: no answer: %s\n", logSafe(reply, 120).c_str());
  } else {
    job.text.trim();
    Serial.printf("ask: answered in %lu ms, %lu micro-dollars: %s\n", (unsigned long)(millis() - start), (unsigned long)job.cost, logSafe(job.text, 80).c_str());
    if (!job.text.length()) { job.error = "The assistant sent an empty answer"; job.fatal = true; }
  }
}

static void sttTask() {
  static const char *BOUNDARY = "----brainos7f3a";
  String model = cfg::str("stt.model"), lang = cfg::str("stt.language"), prompt = cfg::str("stt.prompt");
  String head = String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" + model + "\r\n";
  if (lang.length()) head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n" + lang + "\r\n";
  if (prompt.length()) head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\n" + prompt + "\r\n";   // style hints: case, punctuation, names
  head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String tail = String("\r\n--") + BOUNDARY + "--\r\n";
  size_t len = head.length() + job.len + tail.length();
  uint8_t *body = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!body) { job.error = "No memory for the request"; return; }
  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), job.wav, job.len);
  memcpy(body + head.length() + job.len, tail.c_str(), tail.length());
  uint32_t start = millis();
  String reply;
  int code = post("/api/v1/audio/transcriptions", body, len, String("multipart/form-data; boundary=") + BOUNDARY, reply);
  free(body);
  if (code != 200) {
    job.error = netFailText("OpenRouter", code);
    job.fatal = code >= 400 && code < 500 && code != 429 && code != 401;   // a bad request or an unknown model will not get better; a key problem or a rate limit may
    if (reply.indexOf("model") >= 0 && code == 400) job.error = "OpenRouter doesn't know the model";
    Serial.printf("stt: failed, %d: %s\n", code, logSafe(reply, 200).c_str());
  } else if (!textOf(reply, job.text)) {
    job.error = "OpenRouter sent no text";
    Serial.printf("stt: no text: %s\n", logSafe(reply, 120).c_str());
  } else {
    job.text.trim();
    job.cost = costTranscription(model, job.secs);
    Serial.printf("stt: heard \"%s\" in %lu ms\n", job.text.c_str(), (unsigned long)(millis() - start));
    if (!job.text.length()) { job.error = "Nothing was heard"; job.fatal = true; }
  }
}

static void workerTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (job.state != 1) continue;
    job.text = ""; job.error = ""; job.fatal = false; job.cost = 0;
    if (job.kind == JOB_STT) sttTask(); else chatTask();
    job.state = 2;
  }
}

void sttBegin() {
  // The stack in PSRAM: this task does TLS (big buffers) and never writes flash.
  xTaskCreatePinnedToCoreWithCaps(workerTask, "stt", 20480, nullptr, 1, &worker, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

extern void uiNotesChanged();
extern void uiTextGrew(const String &noteId, int offset);   // the open note (or the chat, for "") grew at offset: show that page

// A result, applied on the loop (which may write flash).
static void apply() {
  free(job.wav); job.wav = nullptr;
  costAdd(job.cost);   // spent whether or not the note or chat is still there
  if (job.error.length()) {
    lastError = job.error; lastFailAt = millis(); failures++;
    Serial.printf("%s: %s\n", job.kind == JOB_STT ? "stt" : job.kind == JOB_ASK ? "ask" : "tidy", lastError.c_str());
  } else { lastError = ""; failures = 0; }
  bool ok = !job.error.length();

  if (!job.noteId.length()) {   // the passing chat
    if (job.chatGen != chat.gen) return;   // left behind: dropped
    chat.cost += job.cost;
    if (job.kind == JOB_STT && (ok || job.fatal)) {
      LittleFS.remove(chat.audio);
      chat.audio = ""; chat.secs = 0;
      if (ok) { chat.turns.push_back(job.text); chat.asking = true; uiTextGrew("", chat.turns.size() - 1); }
      else chat.note = "(not transcribed: " + job.error + ")";
    } else if (job.kind == JOB_ASK && (ok || job.fatal)) {
      chat.asking = false;
      chat.turns.push_back(ok ? chatAnswerText(job.text) : "(no answer: " + job.error + ")");
      uiTextGrew("", chat.turns.size() - 1);
    }
    uiNotesChanged();
    return;
  }

  Note *n = notesFind(job.noteId);
  if (!n) return;
  n->cost += job.cost;
  int at = -1;   // where the note grew
  if (job.kind == JOB_STT && (ok || job.fatal)) {
    LittleFS.remove(n->audio);
    n->audio = ""; n->secs = 0;
    String heard = ok ? job.text : "(not transcribed: " + job.error + ")";
    at = n->text.length() ? n->text.length() + 2 : 0;
    n->text = n->text.length() ? n->text + "\n\n" + heard : heard;
    n->asking = ok && n->asked;   // a question: the assistant's turn next
  } else if (job.kind == JOB_ASK && (ok || job.fatal)) {
    n->asking = false;
    at = n->text.length() + 2;
    n->text += "\n\n" + (ok ? chatAnswerText(job.text) : "(no answer: " + job.error + ")");
  } else if (job.kind == JOB_TIDY && (ok || job.fatal)) {
    n->tidying = false;
    if (ok) { if (!n->orig.length()) n->orig = n->text; n->text = job.text; at = 0; }
  }
  n->modified = time(nullptr);
  notesSave();
  if (at >= 0) uiTextGrew(n->id, at);
  uiNotesChanged();
}

static bool sendAudio(const String &path, int secs, const String &noteId) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t len = f.size();
  uint8_t *wav = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!wav) { f.close(); return true; }   // no memory now: try again later
  size_t got = f.read(wav, len);
  f.close();
  if (got != len) { free(wav); return true; }
  job.kind = JOB_STT; job.noteId = noteId; job.chatGen = chat.gen; job.wav = wav; job.len = len; job.secs = secs; job.req = "";
  job.state = 1;
  Serial.printf("stt: sending %s (%u bytes)\n", path.c_str(), (unsigned)len);
  xTaskNotifyGive(worker);
  return true;
}

static void sendChat(JobKind kind, const String &noteId, const String &body) {
  job.kind = kind; job.noteId = noteId; job.chatGen = chat.gen; job.wav = nullptr; job.len = 0; job.secs = 0; job.req = body;
  job.state = 1;
  Serial.printf("%s: sending %s\n", kind == JOB_TIDY ? "tidy" : "ask", noteId.length() ? noteId.c_str() : "the chat");
  xTaskNotifyGive(worker);
}

void sttTick() {
  if (job.state == 2) { job.state = 0; apply(); return; }
  if (job.state == 1 || !netUp() || !sttConfigured()) return;
  if (failures && millis() - lastFailAt < (failures > 3 ? 300000UL : 30000UL)) return;  // back off after a failure
  // The passing chat first: someone is looking at it.
  if (chat.audio.length()) {
    if (!sendAudio(chat.audio, chat.secs, "")) { chat.audio = ""; chat.note = "(recording lost)"; uiNotesChanged(); }
    return;
  }
  if (chat.asking) { sendChat(JOB_ASK, "", askBodyFor(chat.turns, true)); return; }
  for (auto &q : notesAll()) if (q.asking && !q.audio.length()) { sendChat(JOB_ASK, q.id, askBodyFor(chatTurnsOf(q.text), false)); return; }
  for (auto &q : notesAll()) if (q.tidying && !q.audio.length() && !q.asking) { sendChat(JOB_TIDY, q.id, tidyBodyFor(q.text)); return; }
  for (auto &n : notesAll()) if (n.audio.length()) {
    if (!sendAudio(n.audio, n.secs, n.id)) {
      Serial.printf("stt: %s is missing, dropping it\n", n.audio.c_str());
      n.audio = ""; n.text = n.text.length() ? n.text + "\n\n(recording lost)" : "(recording lost)"; notesSave(); uiNotesChanged();
    }
    return;
  }
}
