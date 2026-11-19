// BrainOS STT spike for the Seeed reTerminal Sticky: hold the OK button, speak English, release;
// the chosen engine transcribes on-chip and the text, timing and memory go to the UART console.
// Serial commands (115200, one line each): "rec N" records N seconds without the button,
// "last" prints the last recording as base64 PCM (16 kHz mono s16le), "info" prints memory.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mic.h"

#define PIN_POWER_HOLD 45
#define PIN_POWER_LOCK 46
#define PIN_BTN_OK 4
#define PIN_BAT_CHG_EN 39
#define RATE 16000
#define MAX_SECONDS 20

static const char *TAG = "spike";
static int16_t *g_last;
static size_t g_last_n;
static volatile int g_rec_until_ms;  // "rec N": record until this tick

// ---------------------------------------------------------------- engines
#ifdef ENGINE_OIDO
#include "tinyasr.h"
#include "tinyasr_lm.h"
#include "tasr_nemo.h"
#ifndef BEAM
#define BEAM 4
#define TOPK 6
#define LMW 0.60f
#define LMB 2.50f
#endif
static tasr_nemo_t *g_m;
static tasr_decoder_t *g_dec;
static tasr_nemo_stream_t *g_st;
static int g_streaming;
static int g_use_stream = 0;  // "mode stream" / "mode utt": feed the recording to the streaming encoder, or transcribe it whole

static void *fw_alloc(size_t n, int kind)
{
    void *p = NULL;
    if (kind == 1 && n <= 48 * 1024 && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > n + 72 * 1024)
        p = heap_caps_aligned_alloc(16, n ? n : 16, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_aligned_alloc(16, n ? n : 16, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) memset(p, 0, n);
    else ESP_LOGE(TAG, "alloc %u kind %d failed", (unsigned)n, kind);
    return p;
}
static void fw_free(void *p) { heap_caps_free(p); }

static const void *map_partition(uint8_t subtype, const char *name, size_t *size)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, subtype, name);
    if (!p) return NULL;
    const void *ptr;
    esp_partition_mmap_handle_t h;
    esp_err_t e = esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &ptr, &h);
    if (e != ESP_OK) { ESP_LOGE(TAG, "mmap %s (%u bytes at 0x%x): %s", name, (unsigned)p->size, (unsigned)p->address, esp_err_to_name(e)); return NULL; }
    *size = p->size;
    return ptr;
}

static int engine_open(void)
{
    tasr_alloc = fw_alloc;
    tasr_free = fw_free;
    size_t msize = 0, lsize = 0;
    const uint8_t *mp = map_partition(0x40, "model", &msize);
    if (!mp) { ESP_LOGE(TAG, "no model partition"); return -1; }
    uint32_t mbytes; memcpy(&mbytes, mp, 4);   // image = [u32 size][pad to 16][TNM1 blob]
    if (mbytes == 0 || mbytes > msize || memcmp(mp + 16, "TNM1", 4)) { ESP_LOGE(TAG, "model partition holds no TNM1 image"); return -1; }
    g_m = tasr_nemo_load(mp + 16, mbytes);
    if (!g_m) { ESP_LOGE(TAG, "model load failed"); return -1; }
    const uint8_t *lp = map_partition(0x42, "lm", &lsize);
    uint32_t lbytes = 0;
    if (lp) memcpy(&lbytes, lp, 4);
    ESP_LOGI(TAG, "lm partition: mapped %p, size %u, header says %u bytes", lp, (unsigned)lsize, (unsigned)lbytes);
    if (lp && lbytes > 0 && lbytes < lsize) {
        tasr_lm_t *lm = tasr_lm_load(lp + 16, lbytes);
        if (lm) {
            size_t mv = tasr_lm_to_ram(lm);
            g_dec = tasr_decoder_create(lm, 1025, BEAM, TOPK, tasr_lm_weight(lm, LMW), tasr_lm_bonus(lm, LMB));
            ESP_LOGI(TAG, "LM %u bytes in PSRAM, beam %d topk %d", (unsigned)mv, BEAM, TOPK);
        }
    } else ESP_LOGW(TAG, "no LM: greedy decoding");
    g_streaming = tasr_nemo_stream_supported(g_m);
    if (g_streaming) g_st = tasr_nemo_stream_new(g_m, 0, 0, g_dec);
    size_t free_ps = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t reserve = 1700000;  // the 20 s recording (640 KB) + utterance buffers
    size_t budget = free_ps > reserve ? free_ps - reserve : 0;
    size_t moved = tasr_nemo_place_weights(g_m, budget);
    ESP_LOGI(TAG, "oido: %u weight bytes, %u in PSRAM, streaming %d | free PSRAM %u internal %u",
             (unsigned)tasr_nemo_weight_bytes(g_m), (unsigned)moved, g_streaming,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return 0;
}

static void engine_run(const int16_t *pcm, int n, char *text, int maxlen)
{
    if (g_st && g_use_stream) {
        tasr_nemo_stream_reset(g_st);
        tasr_nemo_stream_feed(g_st, pcm, n);
        tasr_nemo_stream_finish(g_st, text, maxlen);
    } else tasr_nemo_transcribe(g_m, pcm, n, g_dec, text, maxlen, NULL, 0, NULL);
}
static const char *engine_name(void) { return "oido"; }
#endif

#ifdef ENGINE_BABYTALK
#include "sram_pool.h"
#include "stt_engine.h"
static int engine_open(void)
{
    int rc = sram_pool_reserve();
    if (rc) ESP_LOGW(TAG, "sram_pool_reserve: %d", rc);
    rc = stt_engine_open();
    if (rc) { ESP_LOGE(TAG, "stt_engine_open: %d", rc); return rc; }
    size_t cached = stt_engine_cache(6 * 1024 * 1024);
    stt_info_t info;
    stt_engine_info(&info);
    ESP_LOGI(TAG, "babytalk: model %u bytes, %u cached in PSRAM, v%d ops %d int4 %d | free PSRAM %u internal %u",
             (unsigned)info.model_bytes, (unsigned)cached, info.version, info.ops, info.int4_ops,
             (unsigned)info.psram_free, (unsigned)info.internal_free);
    return 0;
}
static void engine_run(const int16_t *pcm, int n, char *text, int maxlen)
{
    stt_result_t r;
    int rc = stt_engine_run(pcm, n, &r);
    if (rc) { snprintf(text, maxlen, "(error %d)", rc); return; }
    snprintf(text, maxlen, "%s", r.text);
    printf("TIMING fe %.0f ms, model %.0f ms, dec %.0f ms\n", r.fe_ms, r.model_ms, r.dec_ms);
}
static const char *engine_name(void) { return "babytalk"; }
#endif

// ---------------------------------------------------------------- board
static bool ok_down(void) { return gpio_get_level(PIN_BTN_OK) == 0; }
static bool keep_button(void) { return ok_down(); }
static bool keep_timed(void) { return (int)(esp_timer_get_time() / 1000) < g_rec_until_ms; }

static void board_init(void)
{
    gpio_reset_pin(PIN_POWER_HOLD); gpio_reset_pin(PIN_POWER_LOCK);
    gpio_set_direction(PIN_POWER_HOLD, GPIO_MODE_OUTPUT); gpio_set_level(PIN_POWER_HOLD, 1);
    gpio_set_direction(PIN_POWER_LOCK, GPIO_MODE_OUTPUT); gpio_set_level(PIN_POWER_LOCK, 1);
    gpio_reset_pin(PIN_BAT_CHG_EN); gpio_set_direction(PIN_BAT_CHG_EN, GPIO_MODE_OUTPUT); gpio_set_level(PIN_BAT_CHG_EN, 0);
    gpio_reset_pin(PIN_BTN_OK); gpio_set_direction(PIN_BTN_OK, GPIO_MODE_INPUT); gpio_set_pull_mode(PIN_BTN_OK, GPIO_PULLUP_ONLY);
    uart_driver_install(UART_NUM_0, 16384, 0, 0, NULL, 0);
}

static void print_mem(void)
{
    printf("MEM internal free %u (min %u), PSRAM free %u (min %u)\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

static void transcribe(int16_t *pcm, size_t n)
{
    static char text[2048];
    free(g_last); g_last = pcm; g_last_n = n;
    int64_t t0 = esp_timer_get_time();
    engine_run(pcm, (int)n, text, sizeof(text));
    double dt = (esp_timer_get_time() - t0) / 1e6;
    printf("TEXT: %s\nSTAT: %.2f s audio, %.2f s compute, RTF %.2f, engine %s\n", text, n / (double)RATE, dt, dt / (n / (double)RATE), engine_name());
    print_mem();
}

static void dump_last(void)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!g_last) { printf("WAV 0\n"); return; }
    const uint8_t *b = (const uint8_t *)g_last;
    size_t len = g_last_n * 2;
    printf("WAV %u\n", (unsigned)g_last_n);
    char line[77];
    int col = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = b[i] << 16 | (i + 1 < len ? b[i + 1] << 8 : 0) | (i + 2 < len ? b[i + 2] : 0);
        line[col++] = tbl[v >> 18 & 63]; line[col++] = tbl[v >> 12 & 63];
        line[col++] = i + 1 < len ? tbl[v >> 6 & 63] : '='; line[col++] = i + 2 < len ? tbl[v & 63] : '=';
        if (col == 76) { line[col] = 0; puts(line); col = 0; }
    }
    if (col) { line[col] = 0; puts(line); }
    printf("END\n");
}


// "pcm N": the Mac sends N samples as base64 lines (16 kHz mono s16le), then a line "END".
static int b64v(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+') return 62; if (c == '/') return 63; return -1;
}
static void receive_pcm(size_t n_samples)
{
    size_t len = n_samples * 2, got = 0;
    uint8_t *buf = heap_caps_malloc(len + 4, MALLOC_CAP_SPIRAM);
    if (!buf) { printf("TEXT: (no memory)\n"); return; }
    printf("SEND\n");
    uint32_t acc = 0; int bits = 0; char line[8]; int ll = 0; int64_t last = esp_timer_get_time();
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100)) != 1) {
            if (esp_timer_get_time() - last > 10000000) { printf("TEXT: (timeout after %u bytes)\n", (unsigned)got); free(buf); return; }
            continue;
        }
        last = esp_timer_get_time();
        if (c == '\n' || c == '\r') { if (ll == 3 && !memcmp(line, "END", 3)) break; ll = 0; continue; }
        if (ll < 7) line[ll++] = (char)c;
        int v = b64v(c);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (got < len) buf[got++] = (uint8_t)(acc >> bits); }
    }
    printf("GOT %u of %u bytes\n", (unsigned)got, (unsigned)len);
    if (got < len) len = got & ~1u;
    transcribe((int16_t *)buf, len / 2);
}

static void handle_line(char *line)
{
    int secs;
    if (sscanf(line, "rec %d", &secs) == 1 && secs > 0 && secs <= MAX_SECONDS) {
        printf("RECORDING %d s\n", secs);
        g_rec_until_ms = (int)(esp_timer_get_time() / 1000) + secs * 1000;
        int16_t *pcm; size_t n = mic_record(keep_timed, (size_t)RATE * MAX_SECONDS, &pcm);
        if (n) transcribe(pcm, n); else printf("TEXT: (nothing recorded)\n");
    } else if (sscanf(line, "pcm %d", &secs) == 1 && secs > 0 && secs <= RATE * MAX_SECONDS) {
        receive_pcm((size_t)secs);
    } else if (!strncmp(line, "mode ", 5)) {
#ifdef ENGINE_OIDO
        g_use_stream = !strcmp(line + 5, "stream"); printf("MODE %s\n", g_use_stream ? "stream" : "utterance");
#else
        printf("MODE n/a\n");
#endif
    } else if (!strcmp(line, "last")) dump_last();
    else if (!strcmp(line, "info")) { printf("ENGINE %s\n", engine_name()); print_mem(); }
    else printf("? %s\n", line);
}

void app_main(void)
{
    board_init();
    printf("\nBrainOS STT spike, engine %s\n", engine_name());
    print_mem();
    if (engine_open()) { printf("ENGINE FAILED\n"); }
    print_mem();
    printf("READY: hold OK and speak, or send 'rec N'\n");
    char line[64]; int len = 0;
    for (;;) {
        if (ok_down()) {
            vTaskDelay(pdMS_TO_TICKS(40));
            if (ok_down()) {
                printf("LISTENING\n");
                int16_t *pcm; size_t n = mic_record(keep_button, (size_t)RATE * MAX_SECONDS, &pcm);
                if (n > RATE / 2) transcribe(pcm, n); else { free(pcm); printf("TEXT: (too short)\n"); }
                while (ok_down()) vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        uint8_t c;
        while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
            if (c == '\n' || c == '\r') { if (len) { line[len] = 0; handle_line(line); len = 0; } }
            else if (len < (int)sizeof(line) - 1) line[len++] = (char)c;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
