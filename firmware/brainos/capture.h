// Recording from the PDM microphone, kept as a WAV in /queue until it is transcribed.
#pragma once
#include <Arduino.h>
// Record while keepGoing() says so (up to 30 s; atLimit() is called when that is reached, before saving), normalise, write /queue/<id>.wav.
// Returns the seconds recorded (0: nothing or too short); path and id come back through the args.
int captureRecord(bool (*keepGoing)(), void (*atLimit)(), String &path, String &id);
const char *captureError();
