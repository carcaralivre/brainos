// Export: the notes on the list, as a loop of QR codes read by the brainos-reader page.
#pragma once
#include <Arduino.h>
bool exportStart();              // false: nothing to export
void exportDraw(bool armed);     // the current part (call inside a render); armed: Archive all was tapped once
void exportTick();               // from loop(): the next part every couple of seconds
void exportDone(bool keep);      // the button: archive them (when set); keep = leave without archiving
int exportParts();
