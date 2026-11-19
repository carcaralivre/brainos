#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
// Record 16 kHz mono int16 from the Sticky's PDM microphone into a PSRAM buffer while
// keep_going() is true, up to max_samples. Returns the sample count (0 on failure).
// The buffer is normalised (DC removed, peak at about 70 %), like Sticky OS does.
size_t mic_record(bool (*keep_going)(void), size_t max_samples, int16_t **pcm);
