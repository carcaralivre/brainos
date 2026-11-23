// The board: power latch, charger, buttons, buzzer, battery gauge, deep sleep. From Sticky OS.
#pragma once
#include <Arduino.h>
#include <driver/i2c_master.h>

struct Readings {
  bool battery;             // the gauge answered
  int millivolts, percent;
  bool usb;
};
extern Readings now;
extern i2c_master_bus_handle_t sensorBus;
extern bool soundOn;

i2c_master_dev_handle_t i2cDevice(i2c_master_bus_handle_t bus, uint8_t address);
i2c_master_bus_handle_t i2cBus(i2c_port_num_t port, int sda, int scl);

void boardBegin();      // power latch, charger, button pins and interrupts, buzzer pin
void sensorsBegin();    // the I2C bus on GPIO0/1 (after boot: GPIO0 is a strapping pin) and the battery gauge
void readSensors();     // battery and USB, once a minute
void beep(int hz, int ms);
void logHeap(const char *step);
void deepSleep();       // the caller draws the sleeping screen first; OK wakes the board (a restart)

// Buttons: polled each loop; presses are counted by interrupts so none is lost during a refresh.
enum ButtonEvent { BTN_NONE, BTN_OK, BTN_OK_HOLD, BTN_UP_HOLD, BTN_MOVE, BTN_BOTH, BTN_BOTH_LONG };
struct ButtonPoll {
  ButtonEvent event;
  int move;   // BTN_MOVE: +1 down, -1 up (counted presses)
};
ButtonPoll buttonsPoll();
bool okDown();           // the OK button is held right now
bool upDown();           // the Up button is held right now
void okConsume();        // forget presses (after a held OK was acted on)
