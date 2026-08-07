// USB Mass Storage callbacks. Serves the read-only ext2 image from flash and
// overlays the two decimal digits of N (read from the selection input) into
// both the "selNN" directory-entry name and the file content on the fly.
//
// ext2 has no metadata checksums, so mutating those bytes in the outgoing
// sector stream yields a fully valid filesystem for any N in 0..99.

#include <string.h>
#include "bsp/board.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "ext2_image.h"

//--------------------------------------------------------------------
// Selection input. Two ways to pick N; exactly one is compiled in:
//
//   SEL_ADC not defined : the binary strapping pins       (default)
//   SEL_ADC defined     : a potentiometer on ADC pin A2
//
// Uncomment the line below to read the pot instead. Both wirings can
// stay on the board -- this only chooses which one the firmware reads.
//--------------------------------------------------------------------
#define SEL_ADC

// Pot positions: the ADC range is split into this many equal slots, so
// N runs 0..NUM_SLOTS-1. Unused by the strapping build.
#define NUM_SLOTS 16

#if NUM_SLOTS < 2 || NUM_SLOTS > 100
#error "NUM_SLOTS must be 2..100 -- selNN carries two digits"
#endif

#ifdef SEL_ADC
//--------------------------------------------------------------------
// Potentiometer: wiper to A2, the two ends to 3V3 and GND. Turning it
// from one stop to the other walks N through every slot.
//--------------------------------------------------------------------
#define POT_GPIO  28          // A2 on the Pico header
#define POT_CH    2           // ADC input behind GPIO 28
#define ADC_FULL  4096u       // 12-bit converter
#define POT_AVG   4           // samples per read, averaged
#define POT_HYST  (ADC_FULL / (NUM_SLOTS * 8u))   // dead-band at a slot edge

void sel_init(void) {
  adc_init();
  adc_gpio_init(POT_GPIO);
  adc_select_input(POT_CH);
}

// The slot under the wiper. A pot parked on a boundary would otherwise
// dither between two values, and a single host read spans many sectors:
// the overlaid digits have to be the same in all of them. So a slot,
// once taken, is only given up once the reading is POT_HYST past its
// edge.
static uint8_t read_sel_N(void) {
  static uint8_t cur = 0xFF;   // 0xFF = no slot taken yet

  uint32_t v = 0;
  for (unsigned i = 0; i < POT_AVG; i++) v += adc_read();
  v /= POT_AVG;

  uint32_t slot = (v * NUM_SLOTS) / ADC_FULL;
  if (slot >= NUM_SLOTS) slot = NUM_SLOTS - 1;

  if (cur == 0xFF) {
    cur = (uint8_t)slot;
  } else if (slot != cur) {
    uint32_t lo = ((uint32_t)cur * ADC_FULL) / NUM_SLOTS;         // edges of
    uint32_t hi = ((uint32_t)(cur + 1) * ADC_FULL) / NUM_SLOTS;   // the held slot
    if ((slot < cur && v + POT_HYST < lo) || (slot > cur && v > hi + POT_HYST))
      cur = (uint8_t)slot;
  }
  return cur;
}

#else
//--------------------------------------------------------------------
// GPIO binary switch: 4 pins -> N = 0..15. Add pins here for more
// positions; anything above 99 reads as 0.
// Bit i is on pin SW_PINS[i]. Wire each switch line to a GPIO and to GND
// when closed; internal pull-ups give a 1 when open. Low = 1, high = 0.
//--------------------------------------------------------------------
static const uint8_t SW_PINS[] = { 2, 3, 4, 5 };
#define NUM_SW_PINS (sizeof(SW_PINS) / sizeof(SW_PINS[0]))

void sel_init(void) {
  for (unsigned i = 0; i < NUM_SW_PINS; i++) {
    gpio_init(SW_PINS[i]);
    gpio_set_dir(SW_PINS[i], GPIO_IN);
    gpio_pull_up(SW_PINS[i]);
  }
}

static uint8_t read_sel_N(void) {
  uint32_t v = 0;
  for (unsigned i = 0; i < NUM_SW_PINS; i++)
    if (!gpio_get(SW_PINS[i])) v |= (1u << i);   // active low
  return (v > 99) ? 0 : (uint8_t)v;
}
#endif

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

  uint8_t N = read_sel_N();
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
