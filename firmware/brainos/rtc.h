// PCF8563 real-time clock on the sensor bus: keeps the time while the battery lasts.
#pragma once
#include <time.h>
bool rtcBegin();                 // after sensorsBegin()
void rtcToSystem();              // settimeofday() from the chip, if valid
void rtcFromSystem();            // write the system clock into the chip
