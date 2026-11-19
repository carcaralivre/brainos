// The screens and how taps, buttons and swipes move between them.
#pragma once
#include <Arduino.h>
enum Screen { SCREEN_LOCK, SCREEN_NOTES, SCREEN_NOTE, SCREEN_MENU, SCREEN_ARCHIVE, SCREEN_EXPORT, SCREEN_SETTINGS, SCREEN_LISTENING, SCREEN_SAVED,
              SCREEN_KEYBOARD, SCREEN_WIFI, SCREEN_WIFI_SCAN, SCREEN_TIME, SCREEN_ABOUT, SCREEN_UPDATE };
extern Screen screen;
void uiRender(bool full);
void uiTap(int x, int y);
void uiSwipe(int x, int y, int dx, int dy);
void uiOk();                 // a short OK press
void uiMove(int by);         // UP/DOWN presses: page or move the selection
void uiHome();               // UP+DOWN: back to the lock screen
void uiGo(Screen s);         // open a screen and draw it
void uiShowSaved(int secs);  // after a recording: "Saved" for a moment
void uiNotesChanged();
bool uiKeyboardShown();
