#include "bsp/board.h"
#include "tusb.h"

// Newer pico-sdk/TinyUSB dropped BOARD_TUD_RHPORT from the board headers.
// The RP2040 has a single USB controller, so port 0 is always the right one.
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

void switch_gpio_init(void);   // from msc_disk.c
void buttons_init(void);       // from buttons.c
void buttons_task(void);

int main(void) {
  board_init();
  switch_gpio_init();
  buttons_init();
  tud_init(BOARD_TUD_RHPORT);

  while (1) {
    tud_task();                // TinyUSB device task; READ10 is serviced here
    buttons_task();            // power / sleep keys
  }
}
