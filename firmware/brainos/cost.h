// What the cloud costs: each transcription (by its length and the model's price a minute) and
// each answer (OpenRouter says what it cost). Kept by day in /costs.log (400 days) and, in NVS,
// by month and since the board was set up. Deleting a note never takes its cost away.
#pragma once
#include <Arduino.h>

void costBegin();                                 // after LittleFS is mounted
void costAdd(uint32_t micro);                     // millionths of a US dollar, spent now
uint32_t costTranscription(const String &model, int secs);   // what a recording of secs costs with this model
String costText(uint64_t micro);                  // "US$ 0.004", "US$ 0" for nothing

// Sums, in millionths of a dollar. Days are local calendar days; weeks start on Monday.
uint64_t costDaysAgo(int days);                   // one day: 0 today, 1 yesterday
uint64_t costWeeksAgo(int weeks);                 // one week: 0 this week (Monday to today)
uint64_t costMonthsAgo(int months);               // one calendar month: 0 this month
uint64_t costTotal();
String costDayLabel(int days);                    // "Nov 23"
String costWeekLabel(int weeks);                  // "Nov 17" (its Monday)
String costMonthLabel(int months);                // "Nov 2026"
