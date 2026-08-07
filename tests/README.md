# Manual tests

** DO NOT RUN** on production systems, or a host without physical access. Broken GRUB could stop the system from booting up automatically.  

Nothing here runs automatically and nothing here ships in the package -- the
install rules in `CMakeLists.txt` name each installed file, so `tests/` is never
picked up by the `deb` or `rpm` targets.

```
45_grub_boot_switch_test   drop into /etc/grub.d/ to put test entries in the real menu
fixtures/debian.cfg        a realistic grub.cfg (submenu + os-prober) to parse
fixtures/empty.cfg         a grub.cfg with no entries at all
```

## 1. The picker, against a fixture

The app takes an optional grub.cfg path and reads its entry list from there
instead of the system's own. The system's `grub.cfg` is untouched, and **Rescan**
is greyed out (regenerating it would not change the file you are looking at).

```sh
sudo grub-boot-switch-manager tests/fixtures/debian.cfg
```

The path under the window title is the fixture, a banner warns that this is not
the system's `grub.cfg`, and the **Boot entries** group repeats the path with a
count of what came out of it -- `4 entries` here. Expect exactly those four in every dropdown, and note what
is **absent**: the two per-kernel entries inside "Advanced options" are skipped,
which is what keeps a binding alive across a kernel upgrade.

```
Debian GNU/Linux
Windows Boot Manager (on /dev/nvme0n1p1)
Fedora Linux 39 (Workstation Edition) (on /dev/nvme0n1p5)
UEFI Firmware Settings
```

Then the awkward cases, generated rather than checked in:

```sh
sh tests/45_grub_boot_switch_test > /tmp/tricky.cfg
sudo grub-boot-switch-manager /tmp/tricky.cfg
```

Expect seven entries in the dropdowns and a toast carrying three notes:

```
2 entries have no bindable id or title;
1 entry shares an id with an earlier one and is hidden;
1 entry has a title identical to another
```

The eleven cases and what each should do are documented inline in
`45_grub_boot_switch_test`. In short: #2 has no id and is offered anyway, bound
by its title; #8 (unsafe id) and #11 (no id, digit-first title) are dropped; #6
is hidden behind #5's identical id; #9's submenu is skipped whole; and #3/#4 are
both offered but flagged as indistinguishable.

Finally the empty case, which is what a fresh install looks like:

```sh
sudo grub-boot-switch-manager tests/fixtures/empty.cfg
```

Every position shows only `N/A`, and no automatic Rescan fires -- an empty
fixture is an empty fixture, not a machine that needs `update-grub`.

Careful: **Apply** is live in this mode. It writes the real
`/etc/grub-boot-switch/bindings` and runs the real `update-grub`, so if you press
it while viewing a fixture you will bind positions to names that your actual
`grub.cfg` may not contain. Read-only inspection is the intended use.

## 1a. Which positions the app shows

The rows are one per `selN=` line in `/etc/grub-boot-switch/bindings`, so the
file decides. Delete two lines and the app agrees:

```sh
sudo sed -i '/^sel3=/d; /^sel4=/d' /etc/grub-boot-switch/bindings
grub-boot-switch-manager       # rows 1, 2, 5, 6 ... -- no 3, no 4
```

Bind something and press **Apply**, then look at the file again: the values
changed in place, the comments and the gaps are still there, and no `sel3=` or
`sel4=` came back. Restore them by hand (or reinstall the conffile) when done --
the boot side is unaffected either way, since a position with no line is unbound
and an unbound position shows the menu.

Take it to the extreme and comment every `selN=` line out: instead of an empty
window the app puts a warning where the rows were -- `No switch positions to
bind`, naming the file and whether it is missing or merely has no `selN=` lines
-- and **Apply** greys out, since there is nothing to write.

## 2. The real menu, end to end

```sh
sudo install -m0755 tests/45_grub_boot_switch_test /etc/grub.d/
sudo update-grub
```

The fixture is numbered **45** deliberately: its entries land after `10_linux`
and `30_os-prober`, so entry 0 is still your real OS and the machine's normal
boot is unchanged. No entry loads a kernel or chainloads anything -- selecting
one just returns you to the menu with an error.

Now open the app:

```sh
grub-boot-switch-manager
```

Check, in order:

1. The dropdowns list the test entries per section 1 -- seven of the eleven, and
   none of the submenu's children.
2. A toast appears with the three notes.
3. Bind position **1** to **`[GBS TEST] 7 - digit title`** and press **Apply**.
4. Confirm what was recorded is the id, not the title:

```sh
grep '^sel1=' /etc/grub-boot-switch/bindings
#   sel1="gbs-test-digit-title"
grep -A2 'bootswitch}" = 1 ' /boot/grub/grub.cfg
#   … set default="gbs-test-digit-title"; set timeout=0 …
```

That is the point of the whole binding design: had the title been recorded,
GRUB would parse the leading `7` as a menu *index* and boot whatever entry sits
at position 7.

5. Bind position **2** to **`[GBS TEST] 2 no id`** -- the entry with no id -- and
   Apply. This one has nothing but its title to go on, so the title is what
   lands in both files, and GRUB matches it:

```sh
grep '^sel2=' /etc/grub-boot-switch/bindings
#   sel2="[GBS TEST] 2 no id"
grep 'bootswitch}" = 2 ' /boot/grub/grub.cfg
#   … set default="[GBS TEST] 2 no id"; set timeout=0 …
```

6. Set position **2** back to `N/A`, Apply, and confirm an unbound position emits
   the menu instead:

```sh
grep 'bootswitch}" = 2 ' /boot/grub/grub.cfg
#   … set timeout_style=menu; set timeout=-1 …
```

7. Remove a bound entry from the menu and confirm the app notices. Comment out
   case #7 in the fixture (the one position 1 was bound to in step 3),
   `sudo update-grub`, reopen the app: position 1 shows
   `target removed: gbs-test-digit-title` and reads as unbound.

Clean up:

```sh
sudo rm /etc/grub.d/45_grub_boot_switch_test
sudo update-grub
```

## 3. Boot-side behaviour

The generated block is plain shell in `/boot/grub/grub.cfg` between the
`--- grub-boot-switch (generated) ---` markers. Read it directly, and check the
syntax with GRUB's own checker:

```sh
sudo sed -n '/--- grub-boot-switch (generated)/,/--- end grub-boot-switch/p' /boot/grub/grub.cfg
sudo grub-script-check /boot/grub/grub.cfg
```

To exercise a switch position without the hardware, put the marker on any ext2
filesystem carrying the magic UUID (see `SWITCH_UUID` in
`/etc/grub-boot-switch/config`) and reboot:

```sh
sudo mkdir -p /mnt/sw/grub-boot-switch
sudo touch /mnt/sw/grub-boot-switch/sel01     # sel00 = menu, sel01..15 = slots
```

Exactly one `selNN` file should exist at a time. With none present, `bootswitch`
stays `none`, no block fires, and GRUB's own default and timeout govern --
exactly as if the package were not installed.
