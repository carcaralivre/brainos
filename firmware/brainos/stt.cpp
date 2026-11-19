#include "stt.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include "config.h"
#include "json.h"
#include "net.h"
#include "notes.h"

// A job: the WAV bytes (read by the loop; this task never touches flash) and the answer.
static struct {
  String noteId;
  uint8_t *wav; size_t len;
  String text, error;
  volatile int state;  // 0 idle, 1 running, 2 done
} job;
static TaskHandle_t worker;
static String lastError;
static uint32_t lastFailAt;
static int failures;

bool sttRunning() { return job.state == 1; }
const char *sttLastError() { return lastError.c_str(); }
bool sttConfigured() { return cfg::isSet("stt.api_key"); }

static String logSafe(const String &s, int n) { String t = s.substring(0, n); t.replace("\n", " "); return t; }

static int post(const uint8_t *body, size_t len, const String &type, String &reply) {
  String headers = "Authorization: Bearer " + cfg::str("stt.api_key") + "\r\nHTTP-Referer: https://github.com/carcaralivre/brainos\r\nX-Title: BrainOS\r\nContent-Type: " + type + "\r\n";
  return netPost("openrouter.ai", "/api/v1/audio/transcriptions", headers, body, len, reply, 90000);
}

static void workerTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (job.state != 1) continue;
    static const char *BOUNDARY = "----brainos7f3a";
    String model = cfg::str("stt.model"), lang = cfg::str("stt.language");
    String head = String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" + model + "\r\n";
    if (lang.length()) head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n" + lang + "\r\n";
    head += String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    String tail = String("\r\n--") + BOUNDARY + "--\r\n";
    size_t len = head.length() + job.len + tail.length();
    uint8_t *body = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    job.text = ""; job.error = "";
    if (!body) job.error = "No memory for the request";
    else {
      memcpy(body, head.c_str(), head.length());
      memcpy(body + head.length(), job.wav, job.len);
      memcpy(body + head.length() + job.len, tail.c_str(), tail.length());
      uint32_t start = millis();
      String reply;
      netSetBusy(true);
      int code = post(body, len, String("multipart/form-data; boundary=") + BOUNDARY, reply);
      netSetBusy(false);
      free(body);
      if (code != 200) {
        job.error = netFailText("OpenRouter", code);
        if (reply.indexOf("model") >= 0 && code == 400) job.error = "OpenRouter doesn't know the model";
        Serial.printf("stt: failed, %d: %s\n", code, logSafe(reply, 200).c_str());
      } else if (reply.indexOf("\"text\"") < 0) {
        job.error = "OpenRouter sent no text";
        Serial.printf("stt: no text: %s\n", logSafe(reply, 120).c_str());
      } else {
        job.text = jsonString(reply, "text");
        job.text.trim();
        Serial.printf("stt: heard \"%s\" in %lu ms\n", job.text.c_str(), (unsigned long)(millis() - start));
        if (!job.text.length()) job.error = "Nothing was heard";
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
      Serial.printf("stt: %s\n", lastError.c_str());
    } else if (n) {
      LittleFS.remove(n->audio);
      n->audio = ""; n->secs = 0;
      n->text = job.text;
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
  job.noteId = n->id; job.wav = wav; job.len = len; job.state = 1;
  Serial.printf("stt: sending %s (%u bytes)\n", n->audio.c_str(), (unsigned)len);
  xTaskNotifyGive(worker);
}
