// The cloud, one request at a time on its own task: speech to text (OpenRouter, Whisper), the
// assistant's answers and Tidy, for the notes and the passing chat.
#pragma once
#include <Arduino.h>
void sttBegin();
void sttTick();                 // call from loop(): sends what waits when Wi-Fi is up, applies results
bool sttRunning();
const char *sttLastError();     // "" when the last job worked
bool sttConfigured();           // a key is set
