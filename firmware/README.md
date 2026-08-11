# grub-boot-switch firmware

The microcontroller shows up on your PC as a small **read-only USB drive**. On
that drive there is one file:

```
/grub-boot-switch/selNN        # NN = 00..15
```

`NN` is whatever number your selector is set to. Out of the box that is a binary
switch — four lines, so sixteen positions — and a one-line macro swaps it for a
potentiometer on an ADC pin. GRUB reads the drive at boot and picks a boot entry
from it. Both builds also expose two power buttons over USB: power and sleep.

## Dependencies

You need `arm-none-eabi-gcc` plus the SDK for your board

```sh
# RP2040
git clone --recursive https://github.com/raspberrypi/pico-sdk.git 

# STM32 -- must be compiled once, for each family you target
git clone --recursive https://github.com/libopencm3/libopencm3.git
cd libopencm3
make TARGETS='stm32/f0 stm32/f1 stm32/f4' -j$(nproc)
```

Then pointing the paths to the sdk's
```sh
cmake -B build -S . -DPICO_SDK_PATH=/where/pico-sdk -DOPENCM3_PATH=/where/libopencm3
```

## Compiling

Each MCU is its own target, named after the exact part. From the repository
root:

```sh
cmake -B build -S .
cmake --build build --target rp2040           # -> build/gbswitch-rp2040.uf2
cmake --build build --target stm32f103c8t6    # -> build/gbswitch-stm32f103c8t6.bin
```

The parts with a target out of the box:

| target | board it is meant for |
|---|---|
| `stm32f103c8t6` | F1 blue pill |
| `stm32f072rbt6` | F0, crystal-less USB |
| `stm32f401ccu6` | F4 black pill |
| `stm32f407vgt6` | F4 discovery |
| `rp2040` | Raspberry Pi Pico |

For a part that is not listed, add it -- anything in libopencm3's
`ld/devices.data` works, and the core, FPU and memory map come from there:

```sh
cmake -B build -S . -DGBS_STM32_DEVICES="stm32f103c8t6;stm32f411ceu6"
```

For stm32 targets check the xtal freq first:

- **F0**: set up for crystal-less USB (HSI48).
- **F1**: needs an 8 MHz crystal. A bare F103 with no crystal cannot do USB.
- **F4**: assumes an 8 MHz crystal. The WeAct "black pill" has 25 MHz -- pick the
  matching clock preset in `gbswitch.c`. Boards without VBUS sensing may need
  `NOVBUSSENS` set.

## Flashing

The flashing guide shows up after a target is successfully compiled.

## Rebuilding the disk image

Both builds ship with a ready-made `ext2_image.h`, so you normally skip this.
Only do it if you want to rebuild the image yourself:

```sh
cd firmware/scripts
python3 mkimage.py                  # writes ext2_image.h + grub-boot-switch.img
cp ext2_image.h ../rp2040/
cp ext2_image.h ../stm32/
```

You can sanity-check the generated image on your PC:

```sh
e2fsck -fn grub-boot-switch.img
debugfs -R "ls -l /grub-boot-switch" grub-boot-switch.img
```

## Wiring

| | RP2040 | STM32 |
|---|---|---|
| USB | the board's USB port | PA11 / PA12 |
| Switch (4 lines) | GPIO 2..5, bit 0 = GPIO 2 | PB0..PB3, bit 0 = PB0 |
| Potentiometer | A2 (GPIO 28) | PA0 |
| Buttons | GPIO 10, 11 | PB8, PB9 |

Buttons are, in order: power, sleep. The potentiometer is only read when the
build is switched to it (see below); by default the pot pin is untouched.

Everything is pulled **up** internally and wired to **GND**, so a closed contact
reads low.

The switch lines are active low: **low = 1, high = 0**. A line reads 1 when you
connect it to GND, and 0 when left open. Four lines give **N = 0..15**; an
unwired board reads all lines high, which is 0 — the GRUB menu.

The buttons are the same: pressing one connects the pin to **GND**.

## Changing the switch pins

On **RP2040**, edit `SW_PINS[]` near the top of `rp2040/msc_disk.c`. It is just a
list of GPIO numbers, lowest bit first:

```c
static const uint8_t SW_PINS[] = { 2, 3, 4, 5 };
```

Put any free GPIO numbers there, in the order you want the bits.

On **STM32**, edit `SW_PINS[]` in `stm32/gbswitch.c`, using `GPIOn` names, lowest
bit first:

```c
static const uint16_t SW_PINS[] = { GPIO0, GPIO1, GPIO2, GPIO3 };
```

All switch pins must be on the same port. To use a different port, change
`SW_PORT` (e.g. to `GPIOA`) and enable that port's clock in `io_init()`.

The count follows the size of the list, so adding lines adds positions: five
lines give 0..31, six give 0..63, seven give 0..99 (values above 99 read as 0).

## Using a potentiometer instead

A single pot can replace the switch lines: wire its two ends to **3V3** and
**GND** and its wiper to the ADC pin — **A2** (GPIO 28) on the Pico, **PA0** on
STM32. Then uncomment one line near the top of `rp2040/msc_disk.c` or
`stm32/gbswitch.c` and rebuild:

```c
#define SEL_ADC          /* rp2040/msc_disk.c: // #define SEL_ADC */

#define NUM_SLOTS 16
```

`NUM_SLOTS` cuts the converter's range into that many equal slots, so turning
the pot from one stop to the other walks `N` through `0 .. NUM_SLOTS-1`. The
firmware accepts anything from 2 to 100 — `selNN` carries two digits. 

The reading is averaged and each slot is held until the pot is a little past its
edge, so a wiper parked on a boundary does not flicker between two entries.

Both wirings can stay on the board — the macro only picks which one the firmware
reads. The switch pins are left alone in pot builds, and the pot pin is left
alone otherwise. To move the pot to another pin, edit `POT_GPIO`/`POT_CH` next
to that macro (RP2040: the two must be the same input, A0 = GPIO 26/channel 0,
A1 = 27/1, A2 = 28/2), or `POT_PORT`/`POT_PIN`/`POT_CH` on STM32.

## Changing the buttons

Both builds use the same `BUTTONS[]` table, one row per button --
`rp2040/buttons.c` or `stm32/gbswitch.c`:

```c
/* rp2040/buttons.c */      /* stm32/gbswitch.c */
{ 10, SYS_POWER },          { GPIO8, SYS_POWER },
{ 11, SYS_SLEEP },          { GPIO9, SYS_SLEEP },
```

- First field: the pin. A plain GPIO number on RP2040; a `GPIOn` name on STM32,
  where all buttons share one port, set by `BTN_PORT`.
- Second field: the event. `SYS_POWER`, `SYS_SLEEP` or `SYS_WAKE` -- the three
  HID System Control usages. OR them together to fire more than one.

Add or remove rows freely; the count is derived from the table.

These are real USB power events, which the OS acts on subject to its own power
settings. Reboot and logout have no standard USB equivalent, so there is no
button for them.

## Layout

```
firmware/
├── scripts/
│   ├── mkimage.py            # builds ext2_image.h + grub-boot-switch.img
│   └── grub-boot-switch.img  # sample image, for testing on your PC
├── rp2040/                   # pico-sdk + TinyUSB
│   ├── main.c  msc_disk.c  buttons.c  usb_descriptors.c
│   └── tusb_config.h  ext2_image.h
└── stm32/                    # libopencm3
    ├── gbswitch.c  ext2_image.h
```
