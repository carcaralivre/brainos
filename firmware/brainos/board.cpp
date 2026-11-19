#include "board.h"
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include "pins.h"
#include "screen.h"

Readings now;
i2c_master_bus_handle_t sensorBus;
static i2c_master_dev_handle_t sht40, gauge;
bool soundOn = true;

// ---------------------------------------------------------------- buttons

// Presses are counted by interrupts, so one made while the panel refreshes isn't lost.
static volatile int upPresses, downPresses, okPresses;
static volatile uint32_t upAt, downAt, okAt;    // the last edge on each button
static volatile bool upIsDown, downIsDown, okIsDown;

// One press is one count, however much the switch bounces: an edge within
// 40 ms of the last one is bounce, and a press only counts after a release.
static void IRAM_ATTR buttonEdge(int pin, volatile bool &isDown, volatile uint32_t &at, volatile int &presses) {
  uint32_t t = millis();
  bool low = digitalRead(pin) == LOW;
  if (t - at > 40 && low != isDown) {
    isDown = low;
    if (low) presses = presses + 1;
  }
  at = t;
}

bool okDown() { return digitalRead(PIN_BTN_OK) == LOW; }
void okConsume() { okPresses = 0; }

ButtonPoll buttonsPoll() {
  static bool bothWas, okWas, okHeld;
  static uint32_t bothDownAt, okDownAt;
  ButtonPoll r = {BTN_NONE, 0};
  bool ok = okDown(), up = digitalRead(PIN_BTN_UP) == LOW, down = digitalRead(PIN_BTN_DOWN) == LOW;
  if (ok || up || down) lastActivity = millis();
  if (!up && millis() - upAt > 60) upIsDown = false;  // a release the interrupt took for bounce
  if (!down && millis() - downAt > 60) downIsDown = false;
  if (!ok && millis() - okAt > 60) okIsDown = false;

  // UP and DOWN held together for 2 s: sleep. Otherwise every press counted since the last look moves.
  if (up && down) {
    if (!bothWas) bothDownAt = millis();
    bothWas = true;
    if (millis() - bothDownAt > 2000) { bothWas = false; r.event = BTN_BOTH_LONG; return r; }
  } else if (!up && !down) {
    noInterrupts();
    int move = bothWas ? 0 : downPresses - upPresses;
    upPresses = downPresses = 0;
    interrupts();
    if (bothWas) { bothWas = false; r.event = BTN_BOTH; return r; }
    if (move) { r.event = BTN_MOVE; r.move = move; return r; }
  } else if (!bothWas && millis() - (up ? upAt : downAt) > 60) {  // one button, still down: act now, not on release
    noInterrupts();
    int move = downPresses - upPresses;
    upPresses = downPresses = 0;
    interrupts();
    if (move) { r.event = BTN_MOVE; r.move = move; return r; }
  }

  // OK: held 400 ms is a hold (voice); a short press is an action on release.
  if (ok && !okWas) { okDownAt = millis(); okHeld = false; }
  if (ok && !okHeld && millis() - okDownAt > 400) {
    okHeld = true;
    okPresses = 0;
    okWas = ok;
    r.event = BTN_OK_HOLD;
    return r;
  }
  if (!ok) {
    if (okPresses && !okHeld) { okPresses = 0; r.event = BTN_OK; }
    okPresses = 0;
    okHeld = false;
  }
  okWas = ok;
  return r;
}

// ---------------------------------------------------------------- power

static void powerLatch() {
  // After a deep sleep the pins are held; load the levels before letting go.
  for (int pin : {PIN_POWER_HOLD, PIN_POWER_LOCK}) {
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, 1);
  }
  gpio_deep_sleep_hold_dis();
  for (int pin : {PIN_POWER_HOLD, PIN_POWER_LOCK, PIN_BTN_OK, PIN_EPD_EN, PIN_TOUCH_EN,
                  PIN_TOUCH_RST, PIN_SD_EN, PIN_BUZZER, PIN_BAT_CHG_EN})
    gpio_hold_dis((gpio_num_t)pin);
}

// The BQ25616 charger's enable pin is active low. If nothing drives it low the board sees USB
// power but never charges.
static void chargerEnable() {
  gpio_hold_dis((gpio_num_t)PIN_BAT_CHG_EN);
  gpio_set_direction((gpio_num_t)PIN_BAT_CHG_EN, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level((gpio_num_t)PIN_BAT_CHG_EN, 0);
}

static void holdAt(int pin, int level) {
  gpio_hold_dis((gpio_num_t)pin);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)pin, level);
  gpio_hold_en((gpio_num_t)pin);
}

void beep(int hz, int ms) {
  if (!soundOn) return;
  tone(PIN_BUZZER, hz, ms);
  delay(ms + 10);
  noTone(PIN_BUZZER);
}

void boardBegin() {
  powerLatch();
  chargerEnable();
  pinMode(PIN_BTN_OK, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_EXTERNAL_POWER, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  attachInterrupt(PIN_BTN_UP, [] { buttonEdge(PIN_BTN_UP, upIsDown, upAt, upPresses); }, CHANGE);
  attachInterrupt(PIN_BTN_DOWN, [] { buttonEdge(PIN_BTN_DOWN, downIsDown, downAt, downPresses); }, CHANGE);
  attachInterrupt(PIN_BTN_OK, [] { buttonEdge(PIN_BTN_OK, okIsDown, okAt, okPresses); }, CHANGE);
}

void logHeap(const char *step) {
  Serial.printf("heap: %s, internal free %u, largest block %u, lowest %u, psram free %u\n", step,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void deepSleep() {
  while (okDown()) delay(20);  // or the held button wakes it at once
  delay(60);
  holdAt(PIN_POWER_HOLD, 1);
  holdAt(PIN_POWER_LOCK, 1);
  holdAt(PIN_BAT_CHG_EN, 0);  // keep charging while asleep, if USB power is there
  for (int pin : {PIN_EPD_EN, PIN_TOUCH_EN, PIN_TOUCH_RST, PIN_SD_EN, PIN_BUZZER}) holdAt(pin, 0);
  gpio_hold_dis((gpio_num_t)PIN_BTN_OK);
  gpio_set_direction((gpio_num_t)PIN_BTN_OK, GPIO_MODE_INPUT);
  gpio_pullup_en((gpio_num_t)PIN_BTN_OK);
  gpio_hold_en((gpio_num_t)PIN_BTN_OK);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BTN_OK, ESP_EXT1_WAKEUP_ANY_LOW);
  gpio_deep_sleep_hold_en();
  Serial.println("power: deep sleep, OK wakes");
  Serial.flush();
  esp_deep_sleep_start();
}

// ---------------------------------------------------------------- sensors

i2c_master_dev_handle_t i2cDevice(i2c_master_bus_handle_t bus, uint8_t address) {
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = address;
  cfg.scl_speed_hz = 100000;
  i2c_master_dev_handle_t dev = nullptr;
  i2c_master_bus_add_device(bus, &cfg, &dev);
  return dev;
}

i2c_master_bus_handle_t i2cBus(i2c_port_num_t port, int sda, int scl) {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = port;
  cfg.sda_io_num = (gpio_num_t)sda;
  cfg.scl_io_num = (gpio_num_t)scl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = 1;
  i2c_master_bus_handle_t bus = nullptr;
  i2c_new_master_bus(&cfg, &bus);
  return bus;
}

void sensorsBegin() {
  sensorBus = i2cBus(I2C_NUM_1, PIN_SENSOR_SDA, PIN_SENSOR_SCL);
  if (!sensorBus) return;
  sht40 = i2cDevice(sensorBus, ADDR_SHT40);
  gauge = i2cDevice(sensorBus, ADDR_BQ27220);
}

static bool gaugeWord(uint8_t reg, int16_t &value) {
  uint8_t raw[2];
  if (!gauge || i2c_master_transmit_receive(gauge, &reg, 1, raw, 2, 100) != ESP_OK) return false;
  value = (int16_t)(raw[0] | raw[1] << 8);
  return true;
}

void readSensors() {
  now = {};
  now.usb = digitalRead(PIN_EXTERNAL_POWER);
  uint8_t measure = 0xFD, raw[6];  // SHT40: high-precision single shot
  if (sht40 && i2c_master_transmit(sht40, &measure, 1, 100) == ESP_OK) {
    delay(10);
    if (i2c_master_receive(sht40, raw, 6, 100) == ESP_OK) {
      now.climate = true;
      now.tempC = -45 + 175.0f * (raw[0] << 8 | raw[1]) / 65535;
      now.humidity = constrain(-6 + 125.0f * (raw[3] << 8 | raw[4]) / 65535, 0.0f, 100.0f);
    }
  }
  int16_t mv, soc, ma;  // BQ27220: Voltage 0x08, Current 0x0C, StateOfCharge 0x2C
  if (gaugeWord(0x08, mv) && gaugeWord(0x2C, soc) && gaugeWord(0x0C, ma)) {
    now.battery = true;
    now.millivolts = mv;
    now.percent = soc;
    now.milliamps = ma;
    int16_t v;
    now.remainingMah = gaugeWord(0x10, v) ? v : -1;
    now.fullMah = gaugeWord(0x12, v) ? v : -1;
    now.averageMa = gaugeWord(0x14, v) ? v : 0;
    now.minutesLeft = gaugeWord(0x16, v) ? (uint16_t)v : 65535;
    now.averageMw = gaugeWord(0x24, v) ? v : 0;
    now.cycles = gaugeWord(0x2A, v) ? v : -1;
    now.health = gaugeWord(0x2E, v) ? v : -1;
  }
  Serial.printf("sensors: climate %s %.1f C %.0f %%, battery %s %d mV %d %% %d mA, usb power %d\n",
                now.climate ? "ok" : "MISSING", now.tempC, now.humidity,
                now.battery ? "ok" : "MISSING", now.millivolts, now.percent, now.milliamps, now.usb);
}
