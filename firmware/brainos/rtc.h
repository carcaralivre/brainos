// PCF8563 real-time clock on the sensor bus: keeps the time while the battery lasts.
#pragma once
#include <time.h>
bool rtcBegin();                 // after sensorsBegin()
bool rtcValid();                 // false after a power loss (the chip's VL flag)
bool rtcRead(struct tm &t);
bool rtcWrite(const struct tm &t);
void rtcToSystem();              // settimeofday() from the chip, if valid
void rtcFromSystem();            // write the system clock into the chip
