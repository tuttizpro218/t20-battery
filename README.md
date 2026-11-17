# t20-battery

Battery level of the **HXSJ T20** 2.4 GHz mouse on your Linux status bar — polybar, waybar,
anything that can run a script. A ~75 KB static binary, no daemon, no vendor software.

```
 96%      fresh
 96%      dimmed — stale (mouse asleep or receiver absent), kept for 30 min
 4%       red — low battery
```

## Why

The T20's dongle does not use an HXSJ id: it reports as `1EA7:0066` — Sharkoon's
"Mediatrack Edge Mini Keyboard" — calling itself **"2.4G Mouse"**. That id pair is reused by
many cheap generic receivers. There is no standard HID battery usage, no Bluetooth battery,
and no vendor software, so no tool could see the battery.

The protocol lives in an undocumented vendor collection (`FFB5:0001`, report id `B5`):

* the receiver pushes `[B5 67 id[4] state level]` when the mouse wakes or re-links, and on a
  ~10 s light-sleep heartbeat;
* a single read-only query `[B5 05 00 00 00 00 00 00]` makes it stream its 256-byte status
  page (64 frames, ~130 ms); the mouse's last radio status sits at `0x91..0x96` —
  state at `0x95`, percent at `0x96`;
* the page answers even while the mouse is deep asleep, but holds the **last** value, and
  charging is not reported by this firmware at all (verified live with plug/unplug captures).

Full details, byte tables and captures: **[docs/PROTOCOL.md](docs/PROTOCOL.md)**.

## Build & install

```sh
make
make install            # -> ~/.local/bin/t20-battery

sudo cp udev/70-t20-hidraw.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The udev rule gives the active desktop user access to the receiver's hidraw node only
(`1ea7:0066`) — nothing else. Without it the binary prints nothing; with it, no sudo needed
ever again.

Test it:

```sh
~/.local/bin/t20-battery          # " 96%"
~/.local/bin/t20-battery --plain  # "T20 96%"
~/.local/bin/t20-battery --raw    # "02 60" (state, level)
```

## Polybar

Add to your theme's `user_modules.ini` (see [polybar/module-t20.ini](polybar/module-t20.ini)):

```ini
[module/t20]
type = custom/script
exec = ~/.local/bin/t20-battery
interval = 15
```

…add `t20` to your bar's `modules-right`, reload the bar. The script emits its own `%{F...}`
colors (red < 15%, dimmed when stale); the icon is Iosevka Nerd Font's mouse glyph, `--plain`
gives plain text.

Any other bar works the same way: run the binary on a timer and read its single output line.

## Behaviour notes

* The value refreshes whenever the mouse reports over the radio — a wake or the ~10 s
  heartbeat. When the mouse is charging or deep asleep the number holds; a flick of the mouse
  updates it.
* If the dongle is unplugged, the last value is shown dimmed for 30 minutes, then hidden.
* The firmware does not report charging (see PROTOCOL.md), so there is no charging indicator.

## Tools

`tools/b5read.py` is the probe utility from the reverse-engineering session: it dumps the
full status page (`single`) or diffs it between reads while you plug/unplug hardware (`loop`).
Handy if you ever want to check what the firmware is up to:

```sh
sudo python3 tools/b5read.py single
```

## Credits

Protocol reverse-engineered 2026-11-17 from live hidraw captures on Debian 13 (kernel 6.12),
with the owner of the mouse running the probes and confirming the battery state. The
[Wireless-Device-Battery-Monitor](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor)
project inspired the hunt and supports many other brands — this dongle id just wasn't on its
list. If you have a different mouse on a `1EA7:0066` dongle, it may work as-is; open an issue
with your `--raw` output.

## License

[MIT](LICENSE)
