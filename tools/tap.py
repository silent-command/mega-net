#!/usr/bin/env python3
"""Press keys on the MEGA65 by keyboard-matrix code, which reaches what
`m65 -t` cannot: F9-F14, and MEGA or CTRL held with another key.

    tap.py 44                 F9
    tap.py 44 44 45           F9, F9, F11 in a row
    tap.py '3d 15'            MEGA held with F (up to three codes a chord)
    tap.py --probe 44 '3d 07' halt the CPU, press each, and print what the
                              keyboard queued in $D610 and showed in $D611

Each press is 60 ms through $D615-$D617, the registers the m65 tool
itself types with, so nothing auto-repeats. --probe reads $D611 while
the keys are still held, since it is live state and not queued, then
drains the queue so one row never bleeds into the next.

Matrix codes (row*8+column): the C64's 0-63 -- 0 DEL, 1 RETURN, 2 right,
3 F7, 4 F1, 5 F3, 6 F5, 7 down, 15 left SHIFT, 61 MEGA, 63 RUN/STOP,
58 CTRL, 'A' 10, 'B' 28, 'F' 21, 'Z' 12 -- and the MEGA65's column 8:
64 NO SCROLL, 65 TAB, 66 ALT, 67 HELP, 68 F9, 69 F11, 70 F13, 71 ESC.
There is no cursor-up code: up is SHIFT with down, and with MEGA held
the keyboard sends down ($11) for both, marking SHIFT in $D611 instead
(mega-irc 5.26). Needs MEGA65_PORT, as mon.py does."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mon

def chord(keys):
    return " ".join((keys.split() + ["7f", "7f", "7f"])[:3])

def send(fd, line):
    """The whole line, or the machine is left holding a key: a short
    write lost a release once and the client looked dead, every key
    after it a MEGA chord (mega-irc 5.26)."""
    data = (line + "\r").encode()
    while data:
        try: n = os.write(fd, data)
        except BlockingIOError: n = 0
        data = data[n:]
        if data: time.sleep(0.01)

def release(fd):
    """Let go of everything, and wait for the monitor to say so."""
    mon.command(fd, "s ffd3615 7f 7f 7f", wait=0.15)

MODIFIERS = {"3d", "0f", "3a"}          # MEGA, left SHIFT, CTRL

def tap(fd, keys, hold=0.06):
    """Press a chord the way a hand does: the modifier down first, then
    the key, then all released. Landing three codes in one instant lets
    the key sometimes register before the modifier -- MEGA+B arrives as
    a bare 'B' -- which staging avoids (mega-irc 5.26)."""
    codes = keys.split()
    if len(codes) > 1 and codes[0] in MODIFIERS:
        send(fd, "s ffd3615 " + chord(codes[0])); time.sleep(0.03)
    send(fd, "s ffd3615 " + chord(keys)); time.sleep(hold)
    send(fd, "s ffd3615 7f 7f 7f"); time.sleep(0.25)

def head(fd):
    for l in mon.command(fd, "m ffd3610", wait=0.3).replace("\r", "\n").split("\n"):
        if ":0FFD3610:" in l.upper():
            h = l.split(":")[-1].strip()
            return h[0:2], h[2:4]
    return "??", "??"

def drain(fd):
    out = []
    for _ in range(24):
        k, m = head(fd)
        if k in ("00", "??"): break
        out.append(k)
        mon.command(fd, "s ffd3610 00", wait=0.12)
    return out

def probe(fd, chords):
    mon.command(fd, "t1"); time.sleep(0.2); drain(fd)
    print("%-16s %-16s %s" % ("held", "D610 D611", "queued"))
    for keys in chords:
        send(fd, "s ffd3615 " + chord(keys)); time.sleep(0.05)
        k, m = head(fd)
        send(fd, "s ffd3615 7f 7f 7f"); time.sleep(0.25)
        q = drain(fd)
        print("%-16s %s   %s        %s" % (keys, k, m, " ".join(q) or "(nothing)"))
    release(fd)
    mon.command(fd, "t0")

if __name__ == "__main__":
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        sys.exit(__doc__)
    fd = mon.open_port()
    if args[0] == "--probe":
        probe(fd, args[1:])
    else:
        for keys in args:
            tap(fd, keys)
        release(fd)                     # also drains the monitor's echoes
