# grub-boot-switch

A USB hardware switch selects **which GRUB entry auto-boots**.  
Each switch position binds to a stable GRUB menuentry id. A small GUI app 
manages the bindings.  

Note: This is a GRUB tool -- no effect on systemd-boot or rEFInd systems.

## TODO

- On distros using **BootLoaderSpec** (recent Fedora), the Linux entries live 
  in `/boot/loader/entries/*.conf` and are injected by the `blscfg` module 
  rather than written as `menuentry` blocks -- so entry discovery (the GUI 
  picker) may find little to bind to there. The boot-side selection still works
  if you bind to ids that do exist in `grub.cfg`.

## Dependencies

Package build tools:

```sh
sudo dnf install cmake rpm-build dpkg    # dpkg: for the deb target only
sudo apt install cmake dpkg-dev rpm      # rpm: for the rpm target only
```

To build your custom firmware see [Rebuild Firmware](###Rebuild-Firmware)

## Install

```sh
cmake -B build -S .
cmake --build build --target TARGET
```

| target          | what it does                                                |
|-----------------|-------------------------------------------------------------|
| `deb`           | `build/grub-boot-switch{,-config}_<ver>_all.deb`            |
| `rpm`           | `build/grub-boot-switch{,-config}-<ver>-1.noarch.rpm` (needs `rpmbuild`) |
| `install`       | copies the files straight into a rootfs (manual install)    |
| mcu names       | build firmware for a specific microcontroller               |
| N/A             | builds every target                                         |

Then:

```sh
# debian or ubuntu
sudo apt install ./build/grub-boot-switch-config_0.1.0_all.deb \
                 ./build/grub-boot-switch_0.1.0_all.deb

# redhat or centos
sudo dnf install ./build/grub-boot-switch-config-0.1.0-1.noarch.rpm \
                 ./build/grub-boot-switch-0.1.0-1.noarch.rpm
```

Drop the second path on a headless box: `grub-boot-switch-config` boots the
machine on its own. You may modify `/etc/grub-boot-switch/bindings` directly
without GUI.

## Usage

Launch **GRUB Boot Switch**. It will elevate itself if not running as root.
Make assignments on the GUI app, then click `Apply`.

To modify the config file directly, list all menuentries first:
```sh
sudo grep -E "^menuentry " /boot/grub/grub.cfg # or /boot/grub2/grub.cfg
``` 
add the desired uuid in `$menuentry_id_option 'gnulinux-simple-1234-abcd'` to
the config file `/etc/grub-boot-switch/bindings` as:
```conf
sel1="gnulinux-simple-1234-abcd"
```

Then apply the changes:
```sh
sudo update-grub        # or: sudo grub2-mkconfig -o /boot/grub2/grub.cfg
```

## How it works

1. **The switch is a disk.**  
   The device holds a single file named `selXX` (two digits, `00` to `99`).
   GRUB tests which one exists 
   and sets that number into variable `bootswitch`. The file content is 
   completely ignored.
2. **The number picks a binding, and the binding becomes GRUB's `default`.**  
   Position 3 looks up the `sel3=BINDING` line in the bindings file and emits
   `set default="BINDING"` with `set timeout=0` -- boot it now, no menu.
3. **grub `default` matches entry by menuentry_id_option.**  
   A grub.cfg entry looks like 
   `menuentry 'Debian GNU/Linux' ... $menuentry_id_option 'gnulinux-simple-1234-abcd' ...`. 
   when `default` == `gnulinux-simple-1234-abcd` the GRUB boots to this entry.  
   The `$menuentry_id_option` is stable, so it survives kernel upgrades, OS 
   version bumps, disk renames and locale changes.
4. **An entry with no id falls back to its exact title.**  
   GRUB matches a title instead. This more fragile since the title could change
   after kernel upgrade.  
   **WARNING**: A title starting with a digit *can't be used at all* -- GRUB 
   would read it as a menu *index*.
5. **Fallback to the first menu entry.**  
   GRUB jumps to the first menu entry if title matching failed, no timeout.
6. **Empty stays in the GRUB menu.** 
   `sel00`, or any position with no binding, sets an infinite timeout: the 
   sets an infinite timeout: the machine stops at the normal menu and waits for
   user input.

Need a specific boot entry? Add your own `xx_probe` with a stable id and bind to it.

### Files

grub-boot-switch-config:
```
/etc/grub.d/99_grub-boot-switch       the generator (package-owned, not a conffile)
/etc/grub-boot-switch/config          SWITCH_UUID, MENU_TIMEOUT, MARKER_DIR (conffile)
/etc/grub-boot-switch/bindings        selN= -> menuentry id or title (conffile, app-managed)
/usr/lib/udev/rules.d/99-grub-boot-switch.rules  hides the device from the desktop
```

grub-boot-switch:
```
/usr/bin/grub-boot-switch-manager     GUI (runs as root)
/usr/share/applications/*.desktop     launcher
```

`cmake --build build --target install` is unsplit -- it stages every file above
into `DESTDIR`, the same as before.

## Advanced

### Add more slots

Put more `selX=` in the config file `/etc/grub-boot-switch/bindings` like:
```conf
sel16=""
sel17="linux-6.12.74-custom"
sel42="gnulinux-simple-1234-abcd"
```

### Add custom targets

Just make sure u have a very unique `$menuentry_id_option` there:
```sh
#!/bin/sh
# /etc/grub.d/15_linux_custom_kernel -- one entry for one exact kernel.
set -e

KVER=6.12.74-custom
BOOT_UUID=xxxx-xxxx   # fs holding /boot (blkid)
ROOT_UUID=aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee   # fs holding /

cat <<EOF
menuentry 'Linux ${KVER}' --class gnu-linux \$menuentry_id_option 'linux-${KVER}' {
	insmod part_gpt
	insmod ext2
	search --no-floppy --fs-uuid --set=root ${BOOT_UUID}
	linux /boot/vmlinuz-${KVER} root=UUID=${ROOT_UUID} ro quiet
	initrd /boot/initrd.img-${KVER}
}
EOF

exit 0
```

Add to the config file `/etc/grub-boot-switch/bindings`:
```conf
sel9="linux-6.12.74-custom"
```

### Raw Install
Use cmake install instead of a package manager.  

Install dependencies:
```sh
# debian or ubuntu
sudo apt install grub2-common udev                                  # -config
sudo apt install python3 python3-gi gir1.2-gtk-4.0 gir1.2-adw-1 pkexec util-linux

# redhat or centos
sudo dnf install grub2-tools systemd-udev                           # -config
sudo dnf install python3 python3-gobject gtk4 libadwaita polkit util-linux
```

Install files:  
```sh
DESTDIR=/path/to/rootfs cmake --build build --target install
sudo update-grub        # or: sudo grub2-mkconfig -o /boot/grub2/grub.cfg
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=block
```

DESTDIR is optional if install to root is intended.

### Mimimal Install (no GUI)

Run as root:
```sh
cp src/grub.d/99_grub-boot-switch /etc/grub.d/99_grub-boot-switch
chmod +x /etc/grub.d/99_grub-boot-switch
cp src/config                     /etc/grub-boot-switch/config
cp src/bindings                   /etc/grub-boot-switch/bindings
sudo update-grub        # or: sudo grub2-mkconfig -o /boot/grub2/grub.cfg
```

To uninstall, delete the generator *first*, then regenerate grub.cfg

```sh
sudo rm -f /etc/grub.d/99_grub-boot-switch
sudo update-grub
```

### Rebuild Firmware

| target          | what it does                                                |
|-----------------|-------------------------------------------------------------|
| `stm32f103c8t6` | `build/gbswitch-<part>.bin` -- firmware (needs libopencm3)  |
| `rp2040`        | `build/gbswitch-rp2040.uf2` -- firmware (needs pico-sdk)    |

Firmware targets are named after the exact MCU -- one per part. A missing SDK 
only drops the targets that need it:

```
-- grub-boot-switch 0.1.0 -- default build targets:
--   deb            ON   -> build/grub-boot-switch-config_0.1.0_all.deb + grub-boot-switch_0.1.0_all.deb
--   rpm            OFF  -- rpmbuild not found
--   stm32f103c8t6  ON   -> build/gbswitch-stm32f103c8t6.bin
--   stm32f072rbt6  ON   -> build/gbswitch-stm32f072rbt6.bin
--   stm32f401ccu6  ON   -> build/gbswitch-stm32f401ccu6.bin
--   stm32f407vgt6  ON   -> build/gbswitch-stm32f407vgt6.bin
--   rp2040         ON   -> build/gbswitch-rp2040.uf2
```

See [firmware/README.md](firmware/README.md) for the firmware itself: SDKs, 
choosing the STM32 part, flashing, and changing pins.

#### Firmware Dependencies

MCU toolchains:

```sh
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi
sudo dnf install arm-none-eabi-gcc-cs arm-none-eabi-newlib
```

MCU SDKs:

```sh
git clone --recursive https://github.com/raspberrypi/pico-sdk.git
git clone --recursive https://github.com/libopencm3/libopencm3.git
make -C libopencm3 TARGETS='stm32/f0 stm32/f1 stm32/f4' -j$(nproc)
```

Then point the configure step at them:

```sh
cmake -B build -S . -DPICO_SDK_PATH=/where/pico-sdk -DOPENCM3_PATH=/where/libopencm3
```

See [firmware/README.md](firmware/README.md) for choosing the STM32 part,
flashing, and changing pins.
