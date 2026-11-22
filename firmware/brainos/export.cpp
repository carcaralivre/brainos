#include "export.h"
#include <esp_random.h>
#include <qrcode.h>
#include <vector>
#include "config.h"
#include "notes.h"
#include "provjson.h"
#include "psalloc.h"
#include "screen.h"
#include "ui.h"

static const size_t PART_MAX = 950;        // bytes of JSON a QR holds comfortably at version 25
static const uint32_t INTERVAL_MS = 2000;
static std::vector<String, PsAlloc<String>> parts;
static std::vector<String, PsAlloc<String>> ids;
static int part;
static uint32_t shownAt;
static String batch;

int exportParts() { return parts.size(); }
int exportPart() { return part; }
int exportNotes() { return ids.size(); }

static String noteJson(const Note &n) {
  return "{\"id\":\"" + n.id + "\",\"c\":" + String(n.created) + ",\"p\":" + (n.pinned ? "1" : "0") + ",\"t\":" + pj::esc(n.text) + "}";
}

bool exportStart() {
  parts.clear(); ids.clear(); part = 0;
  char b[8]; snprintf(b, sizeof(b), "%04x", (unsigned)(esp_random() & 0xFFFF)); batch = b;
  auto &all = notesAll();
  std::vector<int> idx;
  for (size_t i = 0; i < all.size(); i++) if (!all[i].archived && !all[i].audio.length() && all[i].text.length()) idx.push_back(i);   // everything that is not archived, exported before or not
  if (idx.empty()) return false;
  std::sort(idx.begin(), idx.end(), [&](int a, int c) { return all[a].created < all[c].created; });
  String head = "{\"v\":1,\"b\":\"" + batch + "\",\"i\":I,\"n\":N,\"dev\":" + pj::esc(cfg::str("device.name")) + ",\"notes\":[";
  // Every part the same size: split the notes into n parts of about equal bytes, then pad the
  // shorter ones, so all the codes come out with the same density.
  std::vector<String> jsons;
  size_t total = 0;
  for (int i : idx) {
    String j = noteJson(all[i]);
    if (j.length() + head.length() + 4 > PART_MAX) j = j.substring(0, PART_MAX - head.length() - 8) + "…\"}";
    total += j.length() + 1;
    jsons.push_back(j);
    ids.push_back(all[i].id);
  }
  size_t room = PART_MAX - head.length() - 20;
  int n = max<int>(1, (total + room - 1) / room), k = 0;
  size_t target = total / n + 1;
  String cur;
  for (auto &j : jsons) {
    if (cur.length() && (cur.length() + 1 + j.length() > target && k < n - 1 || cur.length() + 1 + j.length() > room)) { parts.push_back(cur); cur = ""; k++; }
    cur += (cur.length() ? "," : "") + j;
  }
  if (cur.length()) parts.push_back(cur);
  n = parts.size();
  size_t longest = 0;
  for (auto &p : parts) longest = max(longest, p.length());
  for (int k = 0; k < n; k++) {
    String h = head; h.replace("\"i\":I", "\"i\":" + String(k + 1)); h.replace("\"n\":N", "\"n\":" + String(n));
    String pad;
    for (size_t z = parts[k].length(); z < longest; z++) pad += ' ';
    parts[k] = h + parts[k] + "],\"pad\":\"" + pad + "\"}";   // the pad field is always there: same bytes, same QR version, same size
  }
  shownAt = millis();
  Serial.printf("export: batch %s, %u notes in %d parts\n", batch.c_str(), (unsigned)ids.size(), n);
  return true;
}

static int qrX, qrY, qrPx;
static void qrDraw(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q), m = max(1, qrPx / (n + 8));  // four modules of quiet zone a side
  int ox = qrX + (qrPx - n * m) / 2, oy = qrY + (qrPx - n * m) / 2;
  canvas->fillRect(qrX, qrY, qrPx, qrPx, PAPER);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) canvas->fillRect(ox + x * m, oy + y * m, m, m, INK);
}

void exportDraw() {
  if (parts.empty()) { textCentre(W / 2, H / 2, F_S12, "Nothing to export"); return; }
  qrX = 24; qrY = 70; qrPx = W - 48;
  esp_qrcode_config_t c = ESP_QRCODE_CONFIG_DEFAULT();
  c.display_func = qrDraw;
  c.max_qrcode_version = 25;
  c.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  if (esp_qrcode_generate(&c, parts[part].c_str()) != ESP_OK) textCentre(W / 2, H / 2, F_S12, "Couldn't make the code");
  char b[48];
  snprintf(b, sizeof(b), "Part %d of %d", part + 1, (int)parts.size());
  textCentre(W / 2, qrY + qrPx + 50, F_B18, b);
  snprintf(b, sizeof(b), "%d note%s", (int)ids.size(), ids.size() == 1 ? "" : "s");
  textCentre(W / 2, qrY + qrPx + 86, F_S12, b);
  uiButton(24, H - 76, W - 48, 52, "Done", false);
}

void exportTick() {
  if (screen != SCREEN_EXPORT || parts.size() < 2) return;
  if (millis() - shownAt < INTERVAL_MS) return;
  shownAt = millis();
  part = (part + 1) % parts.size();
  uiRender(false);
}

void exportDone(bool keep) {
  if (!keep) {
    bool archive = cfg::flag("export.archive");
    for (auto &id : ids) {
      Note *n = notesFind(id);
      if (!n) continue;
      n->exported = true;
      if (archive) { n->archived = true; n->pinned = false; }
    }
    if (!ids.empty()) notesSave();
  }
  parts.clear(); ids.clear(); part = 0;
}
