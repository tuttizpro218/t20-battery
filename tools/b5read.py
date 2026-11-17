#!/usr/bin/env python3
# Read the HXSJ T20 receiver status page on demand: [B5 05 00 00 00 00 00 00]
# -> 64 reply frames [B5 05 addr 04 d0 d1 d2 d3] covering 0x00..0xFF.
#   b5read.py single        one dump, decoded
#   b5read.py loop [secs]   dump every 2 s, print only bytes that change
# Everything is also appended to /tmp/opencode/output.txt so no manual redirect is needed.
import os, select, sys, time

NODE = "/dev/hidraw4"
DUMP = bytes([0xB5, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
LOG = "/tmp/opencode/output.txt"


def log(msg=""):
    print(msg, flush=True)
    try:
        with open(LOG, "a") as f:
            f.write(msg + "\n")
    except OSError:
        pass


def ts():
    return time.strftime("%H:%M:%S") + ".%03d" % (int(time.time() * 1000) % 1000)


def dump(fd, wait=1.5):
    mem = {}
    try:
        os.write(fd, DUMP)
    except OSError as e:
        log("write failed: %s" % e)
        return None
    end = time.time() + wait
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            d = os.read(fd, 64)
        except OSError:
            continue
        if len(d) >= 8 and d[0] == 0xB5 and d[1] == 0x05 and d[3] == 0x04:
            mem[d[2]] = d[4:8]
        elif d[0] == 0x02:
            pass  # motion
        elif d[0] == 0xB5:
            log("%s  announce: %s" % (ts(), d.hex(" ")))
    if len(mem) < 64:
        log("  incomplete dump: %d/64 chunks" % len(mem))
    return mem


def read_region(mem, start, n):
    # mem is a map of address -> 4-byte chunk; assemble n bytes starting at start
    out = bytearray()
    for a in range(start, start + n):
        chunk = mem.get(a & ~3, b"\x00")
        out.append(chunk[a & 3] if len(chunk) == 4 else 0)
    return bytes(out)


def status_line(mem):
    st = read_region(mem, 0x90, 8)
    return "status@90: %s  | state=0x%02x level=%3d(0x%02x)" % (
        st.hex(" "), st[5], st[6], st[6])


def changed(prev, cur):
    chg = []
    for a in sorted(cur):
        if prev is None or prev.get(a) != cur[a]:
            chg.append("0x%02x: %s -> %s" % (a, (prev or {}).get(a, b"??").hex(" "), cur[a].hex(" ")))
    return chg


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "single"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else (10 if mode == "loop" else 0)
    fd = os.open(NODE, os.O_RDWR | os.O_NONBLOCK)
    while True:  # drain pending
        r, _, _ = select.select([fd], [], [], 0)
        if not r:
            break
        try:
            os.read(fd, 64)
        except OSError:
            break
    log("=== b5read %s (log: %s) ===" % (mode, LOG))
    if mode == "single":
        mem = dump(fd)
        if mem:
            log(status_line(mem))
            log("region 0x80-0x9F: %s" % read_region(mem, 0x80, 32).hex(" "))
    elif mode == "loop":
        # built-in choreography: the script tells you when to plug/unplug
        total = secs if secs >= 75 else 75
        schedule = [
            (0.0,  "ON BATTERY - leave the charger UNPLUGGED"),
            (25.0, ">>> PLUG THE CHARGER IN NOW <<<"),
            (50.0, ">>> UNPLUG THE CHARGER NOW <<<"),
            (75.0, "END - keep it unplugged"),
        ]
        log("=== watching status page for %.0f s (dump every 2 s) ===" % total)
        prev = None
        start = time.time()
        end = start + total
        said = set()
        while time.time() < end:
            t = time.time() - start
            for when, msg in schedule:
                if when not in said and t >= when:
                    said.add(when)
                    log("")
                    if when == 0.0:
                        log("\a %s" % msg)
                    else:
                        log("\a ==================== %s" % msg)
                    log("")
            mem = dump(fd, 1.0)
            if mem is None:
                break
            log("%s %s" % (ts(), status_line(mem)))
            for c in changed(prev, mem):
                log("   CHANGED %s" % c)
            prev = mem
            nxt = min((w for w, _ in schedule if w not in said), default=total)
            time.sleep(max(0.0, min(2.0, start + nxt - time.time())))
    else:
        sys.exit("usage: b5read.py single | loop [secs]")


if __name__ == "__main__":
    main()
