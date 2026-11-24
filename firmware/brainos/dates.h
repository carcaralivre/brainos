// Names of months and weekdays, and dates as text: one place for the screens, the page, the
// costs and the assistant's context.
#pragma once
#include <Arduino.h>
#include <time.h>

inline const char *monthShort(int m) { static const char *N[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}; return N[(m % 12 + 12) % 12]; }
inline const char *monthLong(int m) { static const char *N[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"}; return N[(m % 12 + 12) % 12]; }
inline const char *weekday(int d) { static const char *N[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}; return N[(d % 7 + 7) % 7]; }
inline String dateText(uint32_t t, const char *fmt) {   // strftime in the local time zone
  time_t s = t; struct tm tm; localtime_r(&s, &tm);
  char b[40]; strftime(b, sizeof b, fmt, &tm);
  return b;
}
inline bool clockKnown() { return time(nullptr) > 1700000000; }
