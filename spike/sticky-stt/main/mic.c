// PDM microphone capture for the reTerminal Sticky: CLK GPIO19, DATA GPIO20, load switch GPIO38.
// GPIO19/20 are the chip's USB pins; the board routes USB through a CH343 bridge instead.
#include "mic.h"
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_MIC_CLK 19
#define PIN_MIC_DATA 20
#define PIN_MIC_EN 38
#define RATE 16000

static const char *TAG = "mic";

size_t mic_record(bool (*keep_going)(void), size_t max_samples, int16_t **pcm)
{
    *pcm = NULL;
    gpio_reset_pin(PIN_MIC_CLK);
    gpio_reset_pin(PIN_MIC_DATA);
    gpio_reset_pin(PIN_MIC_EN);
    gpio_set_direction(PIN_MIC_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_MIC_EN, 1);

    i2s_chan_handle_t rx = NULL;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&cc, NULL, &rx) != ESP_OK) { ESP_LOGE(TAG, "no I2S channel"); goto off; }
    i2s_pdm_rx_config_t pc = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.clk = PIN_MIC_CLK, .din = PIN_MIC_DATA, .invert_flags = {.clk_inv = false}},
    };
    if (i2s_channel_init_pdm_rx_mode(rx, &pc) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
        ESP_LOGE(TAG, "PDM RX didn't start");
        i2s_del_channel(rx);
        goto off;
    }
    int16_t *buf = heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    size_t n = 0, got = 0;
    if (buf) {
        int16_t junk[1600];
        i2s_channel_read(rx, junk, sizeof(junk), &got, portMAX_DELAY);  // the mic's power-up thump
        while (n < max_samples && keep_going()) {
            size_t want = max_samples - n;
            if (want > 1600) want = 1600;
            if (i2s_channel_read(rx, buf + n, want * 2, &got, portMAX_DELAY) != ESP_OK) break;
            n += got / 2;
        }
    } else ESP_LOGE(TAG, "no PSRAM for the recording");
    i2s_channel_disable(rx);
    i2s_del_channel(rx);
    gpio_set_level(PIN_MIC_EN, 0);
    if (!buf || !n) { free(buf); return 0; }
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += buf[i];
    int mean = (int)(sum / (int64_t)n), peak = 1;
    for (size_t i = 0; i < n; i++) { int a = abs(buf[i] - mean); if (a > peak) peak = a; }
    int gain = 23000 * 256 / peak;
    if (gain > 64 * 256) gain = 64 * 256;
    for (size_t i = 0; i < n; i++) {
        int v = (buf[i] - mean) * gain / 256;
        buf[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
    ESP_LOGI(TAG, "%u ms recorded, peak %d, gain %.1fx", (unsigned)(n * 1000 / RATE), peak, gain / 256.0);
    *pcm = buf;
    return n;
off:
    gpio_set_level(PIN_MIC_EN, 0);
    return 0;
}
