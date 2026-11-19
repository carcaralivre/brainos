// GT911 touch: polled each loop; a lift after little movement is a tap, more is a swipe.
#pragma once
enum TouchKind { TOUCH_NONE, TOUCH_TAP, TOUCH_SWIPE };
struct TouchEvent {
  TouchKind kind;
  int x, y, dx, dy;   // tap: x, y; swipe: start x, y and movement
};
extern bool touchOk;
extern bool touchQuick;   // taps come fast (the keyboard): 60 ms between taps instead of 300
bool touchBegin();
TouchEvent touchPoll();
