// Speech to text in the cloud (OpenRouter, Whisper): one recording at a time, on its own task.
#pragma once
#include <Arduino.h>
void sttBegin();
void sttTick();                 // call from loop(): feeds waiting recordings when Wi-Fi is up, applies results
bool sttRunning();
const char *sttLastError();     // "" when the last job worked
bool sttConfigured();           // a key is set
