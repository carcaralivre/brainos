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

// A job: the WAV bytes (read by the loop; this task never touches flash) and the answer.
static struct {
  String noteId;
  uint8_t *wav; size_t len;
  String text, error;
  bool fatal;               // the service refused this recording for good: stop retrying it
  bool chat;                // this job asks the assistant (a JSON body in `req`) instead of transcribing
  String req;               // the chat request
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
static bool answerOf(const String &reply, String &text) {   // choices[0].message.content
  pj::Val v, a, c, m, t;
  return pj::parse(reply.c_str(), reply.length(), v) && pj::member(v, "choices", a) && pj::at(a, 0, c) && pj::member(c, "message", m) && pj::member(m, "content", t) && pj::str(t, text);
}

// The assistant: the question, with the instructions from the settings, to a chat model.
static void askTask() {
  String body = "{\"model\":" + pj::esc(cfg::str("ask.model")) + ",\"max_tokens\":500,\"messages\":[";
  String sys = cfg::str("ask.prompt");
  if (sys.length()) body += "{\"role\":\"system\",\"content\":" + pj::esc(sys) + "},";
  body += "{\"role\":\"user\",\"content\":" + pj::esc(job.req) + "}]}";
  uint32_t start = millis();
  String reply;
  netSetBusy(true);
  int code = post("/api/v1/chat/completions", (const uint8_t *)body.c_str(), body.length(), "application/json", reply);
  netSetBusy(false);
  if (code != 200) {
    job.error = netFailText("OpenRouter", code);
    job.fatal = code >= 400 && code < 500 && code != 429 && code != 401;
    if (reply.indexOf("model") >= 0 && code == 400) job.error = "OpenRouter doesn't know the assistant model";
    Serial.printf("ask: failed, %d: %s\n", code, logSafe(reply, 200).c_str());
  } else if (!answerOf(reply, job.text)) {
    job.error = "The assistant sent no answer";
    Serial.printf("ask: no answer: %s\n", logSafe(reply, 120).c_str());
  } else {
    job.text.trim();
    Serial.printf("ask: answered in %lu ms: %s\n", (unsigned long)(millis() - start), logSafe(job.text, 80).c_str());
    if (!job.text.length()) { job.error = "The assistant sent an empty answer"; job.fatal = true; }
  }
}

static void workerTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (job.state != 1) continue;
    job.text = ""; job.error = ""; job.fatal = false;
    if (job.chat) { askTask(); job.state = 2; continue; }
    static const char *BOUNDARY = "----brainos7f3a";
    String model = cfg::str("stt.model"), lang = cfg::str("stt.language"), prompt = cfg::str("stt.prompt");
    String head = String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" + model + "\r\n";
    if (lang.length()) head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n" + lang + "\r\n";
    if (prompt.length()) head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\n" + prompt + "\r\n";   // style hints: case, punctuation, names
    head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    String tail = String("\r\n--") + BOUNDARY + "--\r\n";
    size_t len = head.length() + job.len + tail.length();
    uint8_t *body = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!body) job.error = "No memory for the request";
    else {
      memcpy(body, head.c_str(), head.length());
      memcpy(body + head.length(), job.wav, job.len);
      memcpy(body + head.length() + job.len, tail.c_str(), tail.length());
      uint32_t start = millis();
      String reply;
      netSetBusy(true);
      int code = post("/api/v1/audio/transcriptions", body, len, String("multipart/form-data; boundary=") + BOUNDARY, reply);
      netSetBusy(false);
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
        Serial.printf("stt: heard \"%s\" in %lu ms\n", job.text.c_str(), (unsigned long)(millis() - start));
        if (!job.text.length()) { job.error = "Nothing was heard"; job.fatal = true; }
      }
    }
    job.state = 2;
  }
}

void sttBegin() {
  // The stack in PSRAM: this task does TLS (big buffers) and never writes flash.
  xTaskCreatePinnedToCoreWithCaps(workerTask, "stt", 20480, nullptr, 1, &worker, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static Note *nextPending() {
  for (auto &n : notesAll()) if (n.audio.length()) return &n;
  return nullptr;
}

void sttTick() {
  if (job.state == 2) {  // apply the answer on the loop, which may write flash
    job.state = 0;
    free(job.wav); job.wav = nullptr;
    Note *n = notesFind(job.noteId);
    if (job.error.length()) {
      lastError = job.error; lastFailAt = millis(); failures++;
      Serial.printf("%s: %s\n", job.chat ? "ask" : "stt", lastError.c_str());
      if (job.fatal && n && job.chat) {   // keep the question, say why there is no answer
        n->asking = false;
        n->text += "\n\n(no answer: " + job.error + ")";
        n->modified = time(nullptr);
        notesSave();
      } else if (job.fatal && n) {   // keep the note, drop the audio: it would fail the same way every time
        LittleFS.remove(n->audio);
        n->audio = ""; n->secs = 0; n->asking = false;
        n->text = "(not transcribed: " + job.error + ")";
        n->modified = time(nullptr);
        notesSave();
      }
    } else if (n && job.chat) {
      n->asking = false;
      n->text += "\n\n" + job.text;
      n->modified = time(nullptr);
      notesSave();
      lastError = ""; failures = 0;
    } else if (n) {
      LittleFS.remove(n->audio);
      n->audio = ""; n->secs = 0;
      n->text = job.text;
      n->asking = n->asked;   // a question: the assistant's turn next
      n->modified = time(nullptr);
      notesSave();
      lastError = ""; failures = 0;
    }
    extern void uiNotesChanged();
    uiNotesChanged();
    return;
  }
  if (job.state == 1 || !netUp() || !sttConfigured()) return;
  if (failures && millis() - lastFailAt < (failures > 3 ? 300000UL : 30000UL)) return;  // back off after a failure
  for (auto &q : notesAll()) if (q.asking && !q.audio.length()) {   // a transcribed question: ask the assistant
    job.noteId = q.id; job.req = q.text; job.chat = true; job.wav = nullptr; job.len = 0; job.state = 1;
    Serial.printf("ask: sending %s\n", q.id.c_str());
    xTaskNotifyGive(worker);
    return;
  }
  Note *n = nextPending();
  if (!n) return;
  File f = LittleFS.open(n->audio, "r");
  if (!f) { Serial.printf("stt: %s is missing, dropping it\n", n->audio.c_str()); n->audio = ""; n->text = "(recording lost)"; notesSave(); return; }
  size_t len = f.size();
  uint8_t *wav = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!wav) { f.close(); return; }
  size_t got = f.read(wav, len);
  f.close();
  if (got != len) { free(wav); return; }
  job.noteId = n->id; job.wav = wav; job.len = len; job.chat = false; job.req = ""; job.state = 1;
  Serial.printf("stt: sending %s (%u bytes)\n", n->audio.c_str(), (unsigned)len);
  xTaskNotifyGive(worker);
}
