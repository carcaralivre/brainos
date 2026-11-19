#include "touch.h"
#include <Arduino.h>
#include "board.h"
#include "pins.h"
#include "screen.h"
#include "src/sticky_drivers/gt911.h"

static i2c_master_bus_handle_t touchBus;
static GT911 touch;
bool touchOk, touchQuick;

bool touchBegin() {
  pinMode(PIN_TOUCH_EN, OUTPUT);
  digitalWrite(PIN_TOUCH_EN, HIGH);
  delay(250);
  touchBus = i2cBus(I2C_NUM_0, PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  if (!touchBus || !touch.begin(PIN_TOUCH_INT, PIN_TOUCH_RST, NW, NH, touchBus)) return false;
  uint16_t w = 0, h = 0;
  touch.readResolution(w, h);
  Serial.printf("touch: GT911 at 0x%02X, sensor %u x %u\n", touch.address(), w, h);
  touchOk = true;
  return true;
}

// The sensor's axes are the portrait ones; this is Seeed's conversion to the landscape
// panel, then the canvas rotation is undone.
static void touchToScreen(const GTPoint &p, int &x, int &y) {
  int cx = min<int>(p.x, NW), cy = min<int>(p.y, NH);
  int nx = (NH - cy) * (NW - 1) / NH, ny = (NH - 1) - cx * (NH - 1) / NW;
  switch (rotation) {
    case 1: x = ny, y = NW - 1 - nx; break;
    case 2: x = NW - 1 - nx, y = NH - 1 - ny; break;
    case 3: x = NH - 1 - ny, y = nx; break;
    default: x = nx, y = ny;
  }
}

TouchEvent touchPoll() {
  static int downX, downY, lastX, lastY;
  static uint32_t lastSeen, lastTap;
  static bool wasTouching;
  TouchEvent e = {TOUCH_NONE, 0, 0, 0, 0};
  if (!touchOk) return e;
  GTPoint points[GT911::kMaxTouchPoints];
  int8_t count = touch.read_points(points);
  if (count > 0) {
    touchToScreen(points[0], lastX, lastY);
    if (!wasTouching) downX = lastX, downY = lastY;
    wasTouching = true;
    lastSeen = lastActivity = millis();
  } else if (wasTouching && (count == 0 || millis() - lastSeen > 150)) {  // 0: lifted; kNoUpdate: no new report yet
    wasTouching = false;
    int dx = lastX - downX, dy = lastY - downY;
    if (millis() - lastTap > (touchQuick ? 60u : 300u)) {
      lastTap = millis();
      e.x = downX, e.y = downY, e.dx = dx, e.dy = dy;
      e.kind = (abs(dx) > 70 || abs(dy) > 70) ? TOUCH_SWIPE : TOUCH_TAP;
    }
  }
  return e;
}
