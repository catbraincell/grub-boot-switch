#include "tusb.h"

//--------------------------------------------------------------------
// Device descriptor
//--------------------------------------------------------------------
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCafe,
    .idProduct          = 0x4023,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const* tud_descriptor_device_cb(void) {
  return (uint8_t const*)&desc_device;
}

//--------------------------------------------------------------------
// HID report descriptor: one System Control collection, so no report ID.
//--------------------------------------------------------------------
uint8_t const desc_hid_report[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_SYSTEM_CONTROL),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
      HID_USAGE_MIN(HID_USAGE_DESKTOP_SYSTEM_POWER_DOWN),
      HID_USAGE_MAX(HID_USAGE_DESKTOP_SYSTEM_WAKE_UP),
      HID_LOGICAL_MIN(0),
      HID_LOGICAL_MAX(1),
      HID_REPORT_SIZE(1),
      HID_REPORT_COUNT(3),
      HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
      HID_REPORT_COUNT(5),                 // pad to a whole byte
      HID_INPUT(HID_CONSTANT | HID_VARIABLE),
    HID_COLLECTION_END,
};

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

// The host never reads or writes reports here; buttons push them on their own.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t* buffer,
                               uint16_t reqlen) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const* buffer,
                           uint16_t bufsize) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

//--------------------------------------------------------------------
// Configuration descriptor: MSC (2 bulk) + HID (1 interrupt IN)
//--------------------------------------------------------------------
enum { ITF_NUM_MSC = 0, ITF_NUM_HID, ITF_NUM_TOTAL };

#define EPNUM_MSC_OUT 0x01
#define EPNUM_MSC_IN  0x81
#define EPNUM_HID_IN  0x82

#define CONFIG_TOTAL_LEN \
  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          0x00, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID_IN,
                       CFG_TUD_HID_EP_BUFSIZE, 10),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

//--------------------------------------------------------------------
// String descriptors
//--------------------------------------------------------------------
char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: English (0x0409)
    "RP2040",                    // 1: Manufacturer
    "Grub Boot Switch",          // 2: Product
    "GBS-0001",                  // 3: Serial
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t chr_count;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
      return NULL;
    const char* str = string_desc_arr[index];
    chr_count = (uint8_t)strlen(str);
    if (chr_count > 31) chr_count = 31;
    for (uint8_t i = 0; i < chr_count; i++)
      _desc_str[1 + i] = str[i];
  }

  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
