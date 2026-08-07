// USB Mass Storage callbacks. Serves the read-only ext2 image from flash and
// overlays the two decimal digits of N (read from a GPIO binary switch) into
// both the "selNN" directory-entry name and the file content on the fly.
//
// ext2 has no metadata checksums, so mutating those bytes in the outgoing
// sector stream yields a fully valid filesystem for any N in 0..99.

#include <string.h>
#include "bsp/board.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "ext2_image.h"

//--------------------------------------------------------------------
// GPIO binary switch: 4 pins -> N = 0..15. Add pins here for more
// positions; anything above 99 reads as 0.
// Bit i is on pin SW_PINS[i]. Wire each switch line to a GPIO and to GND
// when closed; internal pull-ups give a 1 when open. Low = 1, high = 0.
//--------------------------------------------------------------------
static const uint8_t SW_PINS[] = { 2, 3, 4, 5 };
#define NUM_SW_PINS (sizeof(SW_PINS) / sizeof(SW_PINS[0]))

void switch_gpio_init(void) {
  for (unsigned i = 0; i < NUM_SW_PINS; i++) {
    gpio_init(SW_PINS[i]);
    gpio_set_dir(SW_PINS[i], GPIO_IN);
    gpio_pull_up(SW_PINS[i]);
  }
}

static uint8_t read_switch_N(void) {
  uint32_t v = 0;
  for (unsigned i = 0; i < NUM_SW_PINS; i++)
    if (!gpio_get(SW_PINS[i])) v |= (1u << i);   // active low
  return (v > 99) ? 0 : (uint8_t)v;
}

//--------------------------------------------------------------------
// SCSI identity / capacity
//--------------------------------------------------------------------
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vid[8], uint8_t pid[16], uint8_t rev[4]) {
  (void)lun;
  memcpy(vid, "RP2040  ", 8);
  memcpy(pid, "GrubBootSwitch  ", 16);
  memcpy(rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) { (void)lun; return true; }

void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
  (void)lun;
  *block_count = DISK_BLOCK_COUNT;
  *block_size  = DISK_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void)lun; (void)power_condition; (void)start; (void)load_eject;
  return true;
}

// Read-only device: refuse writes.
bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return false; }

//--------------------------------------------------------------------
// READ10: copy from flash image, then overlay the live digits.
//--------------------------------------------------------------------
static inline void overlay(void* buf, uint32_t start, uint32_t n,
                           uint32_t abs_off, char c) {
  if (abs_off >= start && abs_off < start + n)
    ((uint8_t*)buf)[abs_off - start] = (uint8_t)c;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void* buffer, uint32_t bufsize) {
  (void)lun;
  uint32_t start = lba * DISK_BLOCK_SIZE + offset;

  if (start >= EXT2_IMAGE_SIZE) {           // beyond image -> zero fill
    memset(buffer, 0, bufsize);
    return (int32_t)bufsize;
  }

  uint32_t n = bufsize;
  if (start + n > EXT2_IMAGE_SIZE) {
    n = EXT2_IMAGE_SIZE - start;
    memset((uint8_t*)buffer + n, 0, bufsize - n);
  }
  memcpy(buffer, ext2_image + start, n);

  uint8_t N = read_switch_N();
  char tens = (char)('0' + N / 10);
  char ones = (char)('0' + N % 10);
  overlay(buffer, start, n, OFF_NAME_TENS, tens);
  overlay(buffer, start, n, OFF_NAME_ONES, ones);
  overlay(buffer, start, n, OFF_CONT_TENS, tens);
  overlay(buffer, start, n, OFF_CONT_ONES, ones);

  return (int32_t)bufsize;
}

// Writes rejected (read-only). Return a negative value to fail the command.
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t* buffer, uint32_t bufsize) {
  (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
  return -1;
}

//--------------------------------------------------------------------
// Handle the few SCSI commands not covered above.
//--------------------------------------------------------------------
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void* buffer, uint16_t bufsize) {
  (void)buffer; (void)bufsize;   // no command here returns data
  switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
      return 0;                             // nothing to do, succeed
    default:
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
      return -1;
  }
}
