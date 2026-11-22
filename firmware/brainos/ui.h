// The screens and how taps, buttons and swipes move between them.
#pragma once
#include <Arduino.h>
enum Screen { SCREEN_LOCK, SCREEN_NOTES, SCREEN_NOTE, SCREEN_ARCHIVE, SCREEN_EXPORT, SCREEN_SETTINGS, SCREEN_LISTENING, SCREEN_SAVED,
              SCREEN_KEYBOARD, SCREEN_WIFI, SCREEN_WIFI_SCAN, SCREEN_ABOUT };
extern Screen screen;
void uiRender(bool full);
void uiTap(int x, int y);
void uiSwipe(int x, int y, int dx, int dy);
void uiOk();                 // a short OK press
void uiMove(int by);         // UP/DOWN presses: page or move the selection
void uiHome();               // UP+DOWN: lock
void uiLock();               // lock, remembering where to come back to
void uiSaveForSleep();       // before deep sleep (the picture stays as it is)
bool uiRestoreAfterSleep();  // after a wake: the same screen, no lock
void uiGo(Screen s);         // open a screen and draw it
void uiShowSaved(int secs);  // after a recording: "Saved" for a moment
void uiNotesChanged();
bool uiKeyboardShown();
void uiButton(int x, int y, int w, int h, const char *label, bool filled);   // the standard button: 52 px tall, rounded
void uiTitle(const char *title);   // the centred title of the top bar
bool uiListeningDone();            // OK let go: the listening screen says "Saving…"
