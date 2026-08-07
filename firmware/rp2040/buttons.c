// Momentary buttons -> USB HID System Control: power / sleep.
// Same table shape as the STM32 build, so one wiring note covers both.
#include "bsp/board.h"
#include "pico/stdlib.h"
#include "tusb.h"

// HID System Control bits (one report byte, three defined bits)
#define SYS_POWER (1u << 0)   // System Power Down
#define SYS_SLEEP (1u << 1)   // System Sleep
#define SYS_WAKE  (1u << 2)   // System Wake Up

typedef struct {
  uint8_t pin;
  uint8_t bits;   // SYS_POWER / SYS_SLEEP / SYS_WAKE
} button_t;

//--------------------------------------------------------------------
// Buttons wire the GPIO to GND when pressed; internal pull-ups give a 1
// when open. Edit this table to change pins or actions.
//--------------------------------------------------------------------
static const button_t BUTTONS[] = {
    { 10, SYS_POWER },   // Power
    { 11, SYS_SLEEP },   // Sleep
};
#define NUM_BTN (sizeof(BUTTONS) / sizeof(BUTTONS[0]))

#define POLL_MS 10        // debounce interval
#define HOLD_MS 50        // how long the event stays asserted before release

void buttons_init(void) {
  for (unsigned i = 0; i < NUM_BTN; i++) {
    gpio_init(BUTTONS[i].pin);
    gpio_set_dir(BUTTONS[i].pin, GPIO_IN);
    gpio_pull_up(BUTTONS[i].pin);
  }
}

static void send(const button_t* b, bool press) {
  uint8_t report = press ? b->bits : 0;
  tud_hid_report(0, &report, 1);   // no report ID: one collection
}

// Called from the main loop. One button at a time: a press and its release
// must not interleave with another button's report.
void buttons_task(void) {
  static uint8_t prev[NUM_BTN];   // 1 = was pressed
  static uint32_t next_poll_ms;
  static uint32_t release_ms;
  static int held = -1;           // index whose press report is outstanding

  if (!tud_hid_ready()) return;   // not configured, or endpoint still busy

  uint32_t now = board_millis();

  if (held >= 0) {
    if (now >= release_ms) {
      send(&BUTTONS[held], false);
      held = -1;
    }
    return;
  }

  if (now < next_poll_ms) return;
  next_poll_ms = now + POLL_MS;

  for (unsigned i = 0; i < NUM_BTN; i++) {
    uint8_t pressed = (gpio_get(BUTTONS[i].pin) == 0);   // active low
    if (pressed && !prev[i] && held < 0) {
      send(&BUTTONS[i], true);
      held = (int)i;
      release_ms = now + HOLD_MS;
    }
    prev[i] = pressed;
  }
}
