/*
 * grub-boot-switch + power keys, for STM32F0 / F1 / F4 (libopencm3).
 *
 * Composite USB device:
 *   Interface 0 : Mass Storage  -> the read-only ext2 /grub-boot-switch/selNN
 *                 drive, with NN overlaid from a GPIO binary switch (PB0..PB3).
 *   Interface 1 : HID           -> system control, driven by two momentary
 *                 buttons (PB8, PB9).
 *
 * Button -> action:
 *   Power   : HID System Control "System Power Down"
 *   Sleep   : HID System Control "System Sleep"
 *
 * Both are real HID System Control usages the OS acts on (subject to its power
 * settings). Edit the BUTTONS[] table to change pins or actions.
 *
 *   USB D-/D+ : PA11 / PA12 (all families).
 *   Switch    : PB0..PB3 (bit 0 = PB0), pulled up, close to GND for a 1.
 *   Buttons   : PB8, PB9, pulled up, press = to GND (active low).
 */

#include <string.h>
#include <stdint.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/msc.h>
#include <libopencm3/usb/hid.h>
#if defined(STM32F0)
#include <libopencm3/stm32/crs.h>
#endif

#include "ext2_image.h"

/* Some libopencm3 versions don't export these class constants. */
#ifndef USB_CLASS_MSC
#define USB_CLASS_MSC 0x08
#endif
#ifndef USB_MSC_SUBCLASS_SCSI
#define USB_MSC_SUBCLASS_SCSI 0x06
#endif
#ifndef USB_MSC_PROTOCOL_BBB
#define USB_MSC_PROTOCOL_BBB 0x50
#endif
#ifndef USB_CLASS_HID
#define USB_CLASS_HID 0x03
#endif

#define EP_MSC_OUT 0x01
#define EP_MSC_IN  0x82
#define EP_HID_IN  0x83

/* ============================================================ GPIO: switch */
#define SW_PORT GPIOB
static const uint16_t SW_PINS[] = {
    GPIO0, GPIO1, GPIO2, GPIO3    /* 4 bits -> N = 0..15 */
};
#define NUM_SW (sizeof(SW_PINS) / sizeof(SW_PINS[0]))

/* ============================================================ GPIO: buttons */
/* HID System Control bits (one report byte, three defined bits) */
#define SYS_POWER (1u << 0)   /* System Power Down */
#define SYS_SLEEP (1u << 1)   /* System Sleep      */
#define SYS_WAKE  (1u << 2)   /* System Wake Up    */

typedef struct {
    uint16_t pin;
    uint8_t  bits;   /* SYS_POWER / SYS_SLEEP / SYS_WAKE */
} button_t;

#define BTN_PORT GPIOB
static const button_t BUTTONS[] = {
    { GPIO8, SYS_POWER },   /* Power */
    { GPIO9, SYS_SLEEP },   /* Sleep */
};
#define NUM_BTN (sizeof(BUTTONS) / sizeof(BUTTONS[0]))

static void io_init(void) {
    rcc_periph_clock_enable(RCC_GPIOB);
#if defined(STM32F1)
    for (unsigned i = 0; i < NUM_SW; i++)
        gpio_set_mode(SW_PORT, GPIO_MODE_INPUT,
                      GPIO_CNF_INPUT_PULL_UPDOWN, SW_PINS[i]);
    gpio_set(SW_PORT, GPIO0|GPIO1|GPIO2|GPIO3);                     /* pull-up */
    for (unsigned i = 0; i < NUM_BTN; i++)
        gpio_set_mode(BTN_PORT, GPIO_MODE_INPUT,
                      GPIO_CNF_INPUT_PULL_UPDOWN, BUTTONS[i].pin);
    gpio_set(BTN_PORT, GPIO8|GPIO9);                                /* pull-up  */
#else /* F0 / F4 */
    for (unsigned i = 0; i < NUM_SW; i++)
        gpio_mode_setup(SW_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, SW_PINS[i]);
    for (unsigned i = 0; i < NUM_BTN; i++)
        gpio_mode_setup(BTN_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, BUTTONS[i].pin);
#endif
}

static uint8_t read_switch_N(void) {
    uint32_t v = 0;
    for (unsigned i = 0; i < NUM_SW; i++)
        if (gpio_get(SW_PORT, SW_PINS[i]) == 0) v |= (1u << i);  /* active low */
    return (v > 99) ? 0 : (uint8_t)v;
}

/* ============================================================ MSC blocks */
static inline void ov(uint8_t *buf, uint32_t start, uint32_t off, char c) {
    if (off >= start && off < start + 512u) buf[off - start] = (uint8_t)c;
}

static int read_block(uint32_t lba, uint8_t *copy_to) {
    uint32_t start = lba * 512u;
    if (start >= EXT2_IMAGE_SIZE) { memset(copy_to, 0, 512); return 0; }
    uint32_t n = 512u;
    if (start + n > EXT2_IMAGE_SIZE) {
        n = EXT2_IMAGE_SIZE - start;
        memset(copy_to + n, 0, 512u - n);
    }
    memcpy(copy_to, ext2_image + start, n);

    uint8_t N = read_switch_N();
    char tens = (char)('0' + N / 10);
    char ones = (char)('0' + N % 10);
    ov(copy_to, start, OFF_NAME_TENS, tens);
    ov(copy_to, start, OFF_NAME_ONES, ones);
    ov(copy_to, start, OFF_CONT_TENS, tens);
    ov(copy_to, start, OFF_CONT_ONES, ones);
    return 0;
}

static int write_block(uint32_t lba, const uint8_t *copy_from) {
    (void)lba; (void)copy_from; return 0;   /* read-only: discard */
}

/* ============================================================ HID reports */
/* One top-level collection, so no report ID: the report is a single byte of
 * System Control bits (power / sleep / wake).                              */
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)   */
    0x09, 0x80,        /* Usage (System Control)         */
    0xA1, 0x01,        /* Collection (Application)       */
    0x19, 0x81,        /*   Usage Min (System Power Down)*/
    0x29, 0x83,        /*   Usage Max (System Wake Up)   */
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x03,
    0x81, 0x02,        /*   Input (Var) 3 bits           */
    0x95, 0x05, 0x81, 0x03, /* padding                   */
    0xC0,
};

static const struct {
    struct usb_hid_descriptor hid_descriptor;
    struct {
        uint8_t bReportDescriptorType;
        uint16_t wDescriptorLength;
    } __attribute__((packed)) hid_report;
} __attribute__((packed)) hid_function = {
    .hid_descriptor = {
        .bLength = sizeof(hid_function),
        .bDescriptorType = USB_DT_HID,
        .bcdHID = 0x0100,
        .bCountryCode = 0,
        .bNumDescriptors = 1,
    },
    .hid_report = {
        .bReportDescriptorType = USB_DT_REPORT,
        .wDescriptorLength = sizeof(hid_report_descriptor),
    },
};

/* ============================================================ USB descriptors */
static const struct usb_device_descriptor dev_desc = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,          /* per-interface classes (composite) */
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,
    .idProduct = 0x5741,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

/* --- MSC interface (0) --- */
static const struct usb_endpoint_descriptor msc_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = EP_MSC_OUT, .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 64, .bInterval = 0,
}, {
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = EP_MSC_IN, .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 64, .bInterval = 0,
}};

static const struct usb_interface_descriptor msc_iface = {
    .bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0, .bAlternateSetting = 0, .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_MSC,
    .bInterfaceSubClass = USB_MSC_SUBCLASS_SCSI,
    .bInterfaceProtocol = USB_MSC_PROTOCOL_BBB,
    .iInterface = 0, .endpoint = msc_endp,
};

/* --- HID interface (1) --- */
static const struct usb_endpoint_descriptor hid_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = EP_HID_IN,
    .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize = 8,           /* the report is a single byte */
    .bInterval = 10,               /* poll every 10 ms */
}};

static const struct usb_interface_descriptor hid_iface = {
    .bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 1, .bAlternateSetting = 0, .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_HID,
    .bInterfaceSubClass = 0,       /* no boot */
    .bInterfaceProtocol = 0,       /* none */
    .iInterface = 0,
    .endpoint = hid_endp,
    .extra = &hid_function, .extralen = sizeof(hid_function),
};

static const struct usb_interface ifaces[] = {
    { .num_altsetting = 1, .altsetting = &msc_iface },
    { .num_altsetting = 1, .altsetting = &hid_iface },
};

static const struct usb_config_descriptor config_desc = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0,             /* filled in by libopencm3 */
    .bNumInterfaces = 2,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0x80,
    .bMaxPower = 0x32,
    .interface = ifaces,
};

static const char *usb_strings[] = {
    "STM32", "Grub Boot Switch + Keys", "GBS-0001",
};
static uint8_t usbd_control_buffer[256];

/* ============================================================ HID plumbing */
/* Answer GET_DESCRIPTOR for the HID report descriptor. Registered from inside
 * hid_set_config because libopencm3 clears control callbacks on SET_CONFIG. */
static enum usbd_request_return_codes hid_control_request(
        usbd_device *dev, struct usb_setup_data *req,
        uint8_t **buf, uint16_t *len,
        void (**complete)(usbd_device *, struct usb_setup_data *)) {
    (void)dev; (void)complete;
    if (req->bmRequestType != 0x81 ||
        req->bRequest != USB_REQ_GET_DESCRIPTOR ||
        req->wValue != 0x2200)
        return USBD_REQ_NOTSUPP;
    *buf = (uint8_t *)hid_report_descriptor;
    *len = sizeof(hid_report_descriptor);
    return USBD_REQ_HANDLED;
}

static void hid_set_config(usbd_device *dev, uint16_t wValue) {
    (void)wValue;
    usbd_ep_setup(dev, EP_HID_IN, USB_ENDPOINT_ATTR_INTERRUPT, 8, NULL);
    usbd_register_control_callback(dev,
        USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        hid_control_request);
}

/* ============================================================ button -> HID */
static void hid_write(usbd_device *dev, const uint8_t *r, uint16_t n) {
    while (usbd_ep_write_packet(dev, EP_HID_IN, r, n) == 0)
        usbd_poll(dev);            /* wait until the endpoint accepts it */
}

/* crude delay that keeps USB serviced so the host can read the packet */
static void settle(usbd_device *dev) {
    for (volatile uint32_t i = 0; i < 300000; i++)
        if ((i & 0x3FF) == 0) usbd_poll(dev);
}

static void fire(usbd_device *dev, const button_t *b) {
    uint8_t r = b->bits, z = 0;
    hid_write(dev, &r, 1); settle(dev); hid_write(dev, &z, 1);
}

static void poll_buttons(usbd_device *dev) {
    static uint8_t prev[NUM_BTN];  /* 1 = was pressed */
    for (unsigned i = 0; i < NUM_BTN; i++) {
        uint8_t pressed = (gpio_get(BTN_PORT, BUTTONS[i].pin) == 0); /* active low */
        if (pressed && !prev[i]) {
            fire(dev, &BUTTONS[i]);
            while (gpio_get(BTN_PORT, BUTTONS[i].pin) == 0)  /* debounce: wait release */
                usbd_poll(dev);
        }
        prev[i] = pressed;
    }
}

/* ============================================================ per-family USB */
static usbd_device *usb_setup(void) {
    usbd_device *dev;
#if defined(STM32F1)
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
    rcc_periph_clock_enable(RCC_GPIOA);
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
    gpio_clear(GPIOA, GPIO12);                     /* blue-pill re-enum kick */
    for (volatile uint32_t i = 0; i < 800000; i++) __asm__("nop");
    dev = usbd_init(&st_usbfs_v1_usb_driver, &dev_desc, &config_desc,
                    usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
#elif defined(STM32F0)
    rcc_clock_setup_in_hsi48_out_48mhz();
    rcc_periph_clock_enable(RCC_CRS);
    crs_autotrim_usb_enable();
    dev = usbd_init(&st_usbfs_v2_usb_driver, &dev_desc, &config_desc,
                    usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
#elif defined(STM32F4)
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
    rcc_periph_clock_enable(RCC_GPIOA);
    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
    gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);
    rcc_periph_clock_enable(RCC_OTGFS);
    dev = usbd_init(&otgfs_usb_driver, &dev_desc, &config_desc,
                    usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
#else
#error "Define STM32F0, STM32F1, or STM32F4 (set DEVICE in the Makefile)."
#endif
    usbd_register_set_config_callback(dev, hid_set_config);  /* MSC registers its own */
    return dev;
}

int main(void) {
    usbd_device *dev = usb_setup();
    io_init();

    usb_msc_init(dev, EP_MSC_IN, 64, EP_MSC_OUT, 64,
                 "STM32", "GrubBootSwitch", "1.0",
                 DISK_BLOCK_COUNT, read_block, write_block);

    while (1) {
        usbd_poll(dev);
        poll_buttons(dev);
    }
    return 0;
}
