# STT spike: local English speech to text on the reTerminal Sticky

Goal: pick the on-chip engine for BrainOS voice notes. Candidates: babytalk (tlack/babytalk) and Oído (lokutor-ai/oido). Both are ESP-IDF 5.5 C components with ESP32-S3 vector kernels; both ship their models in-tree.

## Candidates

| | babytalk | Oído |
|---|---|---|
| Model | Citrinet-256 CTC, int4 6.0 MB / int8 9.8 MB (+ "noisy" variants) | v1 `nemo4.tnm` int4 8.3 MB, `nemo8.tnm` int8 14 MB; v2 `oido_v2a` 7.7 MB, `oido_v2b` 5.9 MB (streaming); LM 1.3 MB |
| Published speed (S3, 240 MHz) | 0.35 s per 1 s of speech (int4) | v2b RTF 0.54, v2a 0.72; v1 ~1.8-2.0x real time |
| Published WER | 6.3 % clean (int8), ~50 % noisy rooms | v1 int4 + LM 3.8 / 8.4; v2b + LM 10.6 / 22.4 (test-clean / test-other) |
| Internal RAM | 84.5 KB shared block (`sram_pool`) | >= 122 KB free needed |
| PSRAM | ~0.2 MB | ~2.4 MB peak |
| Host build (Mac) | no (board over WiFi TCP) | yes: `esp32/host/tasr_cli` |
| Licence | MIT, model CC-BY-4.0 | GPLv3, model CC BY-NC-SA 4.0 |

## Log

### 2026-11-18: Oído host engine on the Mac
- `cd oido/esp32/host && make` builds with clang, no extra deps.
- Three of the project's sample clips (6-8 s each) transcribe correctly with `oido_v2b.tnm` + `oido_v2.tlm`; ~0.05 s CPU each on the Mac.
- Device not connected yet; babytalk needs the board for any measurement.

### 2026-11-18: note-style phrases, synthetic voice (macOS `say`, Samantha), host engine
Eight short "fridge note" phrases (3-5 s). Not Eduardo's voice; only a vocabulary sanity check.

| Model | Right | Typical misses |
|---|---|---|
| `oido_v2b` + LM (5.9 MB, streaming) | 4 / 8 exact, rest 1-2 words off | "buy" -> "by", "app" -> "abb", "e-ink" -> "the ink", "invoice" -> "in voice" |
| `nemo4` int4 + LM (8.3 MB, utterance, slower) | 3 / 8 exact | "week" -> "wig", dropped "book", "eight" -> "day" |

Proper nouns and product words (e-ink, app, Mariana) are the weak spot, as expected for a small English model. Real measurement waits for recordings from the Sticky's own microphone.

### 2026-11-18: spike firmware for the Sticky builds for both engines
`spike/sticky-stt/`: one ESP-IDF 5.5.1 app, engine chosen with `STICKY_ENGINE=oido|babytalk` (`tools/build.sh`). PDM mic on GPIO19/20 (load switch GPIO38), hold OK (GPIO4) to record up to 20 s, serial commands `rec N`, `last` (base64 PCM out), `info`. Partitions: app 2 MB at 0x10000, `model` 16 MB at 0x210000, `lm` 2 MB at 0x1210000. Flash mode DIO (the upstream Arduino image is DIO too). Both variants link: 316 KB (babytalk) and 323 KB (Oído). One build-system catch: the engine choice must be an environment variable, because ESP-IDF resolves component requirements in an early phase that doesn't see `-D` cache variables. Not run on hardware yet.

### 2026-11-18: hardware validated with upstream Sticky OS
Flashed the unmodified upstream build (bootloader + partitions + boot_app0 + app, 115200 baud, DIO; 3.9 MB took 197 s). Boot log: SSD1677 panel, GT911 touch, SHT40/BQ27220/IMU all ok, battery 92 %, 241 KB internal RAM and 7.8 MB PSRAM free at idle. Port on this Mac: `/dev/cu.usbmodem5CBC4434271` (CH343 shows as usbmodem).

### 2026-11-18: flash MMU wraps at 16 MB (design constraint for BrainOS)
First Oído run on the board: model loaded (5.84 MB weights, 5.15 MB placed in PSRAM, streaming), but the `lm` partition at 0x1210000 read back the model's own header: `esp_partition_mmap` on this ESP32-S3 aliases physical flash addresses modulo 16 MB. Every memory-mapped partition (app slots, STT model, LM) must live in the first 16 MB; the upper 16 MB can hold LittleFS and other partitions that are read, not mapped. Spike layout is now app 2 MB @0x10000, lm 2 MB @0x210000, model 12.4 MB @0x410000. After load: 74 KB internal RAM and 1.97 MB PSRAM free.

### 2026-11-19: result and decision
Eduardo read the ten phrases on the board (Oído v2b + LM, streaming mode): **WER 42.5 %** (37/87), RTF 0.65. The same recordings on the Mac: v2b 27.6 %, v2a 24.1 %, nemo4 18.4 %, nemo8 19.5 %. Recordings are noisy (SNR 12-16 dB) and the voice is non-native. Conclusion: even the best local model is at one word in five wrong, so **local STT is dropped**. BrainOS transcribes in the cloud (OpenRouter, Whisper) in Portuguese or English, queues audio when offline. The spike code stays in `spike/sticky-stt/` as a record; it is not part of the product. Lasting lesson: memory-mapped partitions must stay below 16 MB on this board.
