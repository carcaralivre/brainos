// Export: the notes not yet exported, as a loop of QR codes read by the brainos-reader page.
#pragma once
#include <Arduino.h>
bool exportStart();              // false: nothing to export
void exportDraw();               // the current part (call inside a render)
void exportTick();               // from loop(): the next part every couple of seconds
void exportDone(bool keep);      // OK: mark exported (and archive if set); keep = cancel without marking
int exportParts();
int exportPart();
int exportNotes();
