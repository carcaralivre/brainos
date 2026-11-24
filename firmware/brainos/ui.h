// The screens and how taps, buttons and swipes move between them.
#pragma once
#include <Arduino.h>
enum Screen { SCREEN_LOCK, SCREEN_NOTES, SCREEN_NOTE, SCREEN_ARCHIVE, SCREEN_EXPORT, SCREEN_SETTINGS, SCREEN_LISTENING, SCREEN_CHAT,
              SCREEN_KEYBOARD, SCREEN_WIFI, SCREEN_WIFI_SCAN, SCREEN_ABOUT };
extern Screen screen;
void uiRender(bool full);
void uiTap(int x, int y);
void uiSwipe(int x, int y, int dx, int dy);
void uiOk();                 // a short OK press
void uiMove(int by);         // UP/DOWN presses: page or move the selection
void uiLock();               // lock, remembering where to come back to
void uiSaveForSleep();       // before deep sleep (the picture stays as it is)
bool uiRestoreAfterSleep();  // after a wake: the same screen, no lock
void uiGo(Screen s);         // open a screen and draw it
void uiOpenNote(const String &id, int focus);   // open a note at the page holding byte `focus` of its text (0: the top)
void uiOpenChat(Screen back);   // the passing chat, opened from `back`
String uiOpenNoteId();          // the note on SCREEN_NOTE
void uiSay(const char *msg);    // a line above the buttons of the note or the chat, until the next press
void uiNotice(const char *msg); // a recording that failed: the reason, big, for a moment
void uiSleeping(bool on);       // the lock screen shows the padlock (drawn before deep sleep)
void uiMinute();                // once a minute: redraw the lock screen if the battery or the date changed
void uiNotesChanged();
// The geometry every screen shares: a 56 px top bar, 24 px side margins, buttons 52 px tall
// with 16 px between them, the footer row 24 px from the bottom.
static const int KB_NOTE_MAX = 4000;   // characters a note takes from the keyboard and from brain.local
static const int UI_BAR = 56, UI_MARGIN = 24, UI_BTN = 52, UI_GAP = 16;
static const int UI_FOOT_Y = 800 - UI_MARGIN - UI_BTN;
static const int UI_HALF = (480 - 2 * UI_MARGIN - UI_GAP) / 2;   // a half-width button
void uiButton(int x, int y, int w, int h, const char *label, bool filled);   // the standard button, rounded
void uiFooter(const char *left, bool leftFilled, const char *right, bool rightFilled);   // two half-width buttons on the footer row
int uiFooterHit(int x, int y);     // -1: not on the footer row; 0: the left half; 1: the right half
void uiTitle(const char *title);   // the centred title of the top bar
bool uiListeningDone();            // OK let go: the listening screen says "Saving…"
