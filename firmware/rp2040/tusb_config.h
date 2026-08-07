#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU          OPT_MCU_RP2040
#define CFG_TUSB_OS           OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64

// Composite: Mass Storage (the switch drive) + HID (the power keys).
#define CFG_TUD_MSC 1
#define CFG_TUD_HID 1
#define CFG_TUD_CDC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

// Bulk endpoint buffer = one logical block.
#define CFG_TUD_MSC_EP_BUFSIZE 512

// The System Control report is a single byte.
#define CFG_TUD_HID_EP_BUFSIZE 8

#endif // _TUSB_CONFIG_H_
