# HXSJ T20 on the reused Sharkoon `1EA7:0066` receiver — protocol notes

## Why nothing could read the mouse

The HXSJ T20 is a budget rechargeable 2.4 GHz mouse. Its dongle does not use an HXSJ vendor id:
it identifies as `1EA7:0066` — registered to Sharkoon for the "Mediatrack Edge Mini Keyboard" —
and calls itself **"2.4G Mouse"** with two interfaces. This id pair is widely reused by cheap
generic receivers (the same `VID:PID` shows up in forum posts as the receiver of unrelated mice),
so there is no vendor name to look for and no published protocol.

The descriptors have no standard HID *Battery Strength* usage, so Windows' generic battery
fallback and Linux' kernel `hid-input` battery see nothing. The receiver is not Bluetooth.
There is no vendor software to capture. The battery is only reachable through a vendor
collection that nobody had documented — this page does, from live captures.

## Hardware and HID layout

Tested with an HXSJ T20, dongle firmware `bcdDevice 2.00`, on Linux 6.12 (Debian 13), 2026-11-17:

| Interface | Usage | Reports |
|---|---|---|
| 0 | keyboard (boot 6KRO) | 8-byte input |
| 1 | mouse | report `02`: buttons, 12-bit X/Y, wheel; consumer `01`; system `03` |
| 1 | **vendor `FFB5:0001`** | report `B5`: 8-byte input **and** 8-byte output |
| 1 | vendor `FFBC:0088` | report `04`: 1-byte input (never observed in captures) |

Everything battery-related lives in the `FFB5:0001` collection.

## The status channel (report id `B5`)

### Wake announcement (input, push)

When the mouse wakes up, re-links, or runs its ~10 s light-sleep heartbeat, the receiver pushes:

```text
B5 67 78 46 79 98 02 60
```

| Byte(s) | Meaning | Example |
|---|---|---|
| 0 | Report id | `B5` |
| 1 | Frame type | `67` |
| 2–5 | Device id (stable across power cycles and dumps) | `78 46 79 98` |
| 6 | State | `02` |
| 7 | Battery level, percent 0–100 | `60` = 96% |

The frame is byte-identical across every wake while the level is unchanged. After ~20–30 s idle
the mouse goes into deep sleep and the announcements stop.

### Status page query (output + 64 input frames)

The receiver answers a single read-only query with its full 256-byte status page:

```text
request (output report B5, 8 bytes):
B5 05 00 00 00 00 00 00

reply stream (input report B5, 64 frames, ~130 ms):
B5 05 addr 04 d0 d1 d2 d3      with addr = 0x00, 0x04, ... 0xFC
```

Each frame carries 4 bytes of the page at `addr`. The mouse's last reported status sits at
`0x91..0x96` — exactly the payload of the wake announcement:

```text
addr 0x90: 00 78 46 79      -> 0x90 unused, 0x91..0x93 = id0..id2
addr 0x94: 98 02 60 15      -> 0x94 id3, 0x95 state, 0x96 level, 0x97 unknown (0x15 in captures)
```

The page answers even while the mouse is deep asleep (the receiver replies by itself), but it
holds the **last value the mouse reported over the radio**. That is the right behaviour for a
status widget — but it means the value can be stale while the mouse charges or is switched off.

### Other commands (observed, not used)

A one-frame probe sweep produced:

| Output payload | Reply |
|---|---|
| `B5 01 00 ...` | `B5 01 01 38 00 ...` (version-like `0x38`) |
| `B5 02 00 ...` | 64-frame dump of a second page, all zeros |
| `B5 04 00 ...` | `B5 04 01 00 ...` |
| `B5 03/06/10/50 ...` | nothing |

Write commands were deliberately not probed. Only the exact query above is ever sent by this
tool; both bytes of the `B5` pair are defined in the report descriptor, so nothing is guessed.

## Charging is not reported

Verified by watching the status page live while plugging and unplugging a charger: not one byte
in `0x90..0x9F` changed, and no announcement was pushed. A charge-only cable keeps the mouse
silent on the 2.4 GHz link while charging. So the level is reported without a charging state,
and the level simply holds while the mouse is on the cable.

## Battery level semantics

The level byte is a plain percent (96% reported for a mouse the owner confirmed as nearly full;
the wake frame and the page always agree). Long-run discharge tracking is pending — if the byte
turns out to be quantised or non-linear on this firmware, this page will be updated. The state
byte stayed `02` in every capture, including while charging; its full semantics (low-battery
value, charging value, offline value) are unknown.

## What is not in the page

* **DPI and other mouse settings.** Pressing the mouse's DPI button — which visibly cycles
  stages and changes the LED — changes nothing in the 256-byte page. The config lives in the
  mouse and never crosses the radio. The `2c 04 2c 04 2c 04` triplet at `0x30` and the table at
  `0x80..0x8F` stayed constant through every capture, including a session dedicated to pressing
  the DPI button.
* The page does mirror the **last command it received**: after our query, `B5 05 00 00` sits at
  `0x08` (echoed again at `0x36`).
* Radio housekeeping ticks regardless of the mouse state: an uptime-like counter at `0x18`, and
  byte `0xC8` (`ff 00 xx 00`) plus `0x50`/`0xD8..0xE4` flip constantly while the mouse is active
  and settle back to `ff` when it idles — most likely last-packet RSSI/noise metrics, not
  settings.

## Implementation

`src/t20-battery.cpp` enumerates `/sys/class/hidraw`, matches `HID_ID=0003:00001EA7:00000066`,
opens each candidate node and sends the page query, accepting the first node whose replies match
the `[B5 05 addr 04 ...]` shape (the keyboard interface never answers, so no report descriptor
parsing is needed). It reports `state` and the percent from chunk `0x94`.

`tools/b5read.py` is the probe utility used during reverse engineering: it dumps the full page,
diffs it between reads (`loop` mode) and decodes the status area (`single` mode).

## Credits

Protocol reverse-engineered on 2026-11-17 by the **GLM AI assistant** (Z.ai, via the
[OpenCode](https://opencode.ai) coding agent), working live with the mouse's owner,
**@tuttizpro218**, who ran the probes (power cycles, charger plug/unplug) and validated the
readings against the real battery state.
