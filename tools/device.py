#!/usr/bin/env python3
"""Locate and drive the MEGA65 from any development host.

The MEGA65 cross-development tool ships under a different name on each
platform -- `m65` on Linux, `m65.osx` on macOS, `m65.exe` on Windows -- and
is often not on PATH at all. Every part of this project that talks to
hardware goes through here, so that a build or test step never hardcodes one
platform's spelling.

    python3 tools/device.py --check        confirm the machine answers
    python3 tools/device.py --shot out.png capture the screen
    python3 tools/device.py --run x.prg    reset, then load and run a PRG
    python3 tools/device.py --mem 1400:1480 out.bin
                                           save a memory range (hex)
    python3 tools/device.py -- <args>      pass arguments through to m65

Override discovery with MEGA65_M65 (path to the binary) and MEGA65_PORT
(serial port; otherwise m65 autodiscovers).
"""

import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

SYSTEM = platform.system()

# Ordered by how likely each is to be the right one on the current platform.
BINARY_NAMES = {
    "Darwin":  ["m65.osx", "m65"],
    "Linux":   ["m65", "m65.linux"],
    "Windows": ["m65.exe", "m65"],
}.get(SYSTEM, ["m65"])

SEARCH_DIRS = [
    Path("/usr/local/bin"),
    Path("/opt/homebrew/bin"),
    Path("/usr/bin"),
    Path.home() / "bin",
    Path.home() / "mega65",
]
if SYSTEM == "Windows":
    SEARCH_DIRS += [
        Path("C:/Program Files/MEGA65"),
        Path("C:/mega65"),
    ]


def find_m65():
    """Returns the path to the m65 binary, or None."""
    override = os.environ.get("MEGA65_M65")
    if override:
        p = Path(override)
        return str(p) if p.is_file() else None

    for name in BINARY_NAMES:
        found = shutil.which(name)
        if found:
            return found

    for d in SEARCH_DIRS:
        for name in BINARY_NAMES:
            cand = d / name
            if cand.is_file() and os.access(cand, os.X_OK):
                return str(cand)
    return None


def m65(*args, timeout=60):
    """Runs m65 with the given arguments. Returns a CompletedProcess."""
    binary = find_m65()
    if not binary:
        names = ", ".join(BINARY_NAMES)
        raise SystemExit(
            f"MEGA65 tool not found (looked for {names}).\n"
            "Install mega65-tools, or set MEGA65_M65 to the binary."
        )

    cmd = [binary]
    port = os.environ.get("MEGA65_PORT")
    if port:
        cmd += ["-l", port]
    cmd += list(args)

    try:
        return subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
    except subprocess.TimeoutExpired:
        raise SystemExit(
            f"m65 did not answer within {timeout}s. The machine may be "
            "wedged; try a reset (-F) before power-cycling."
        )


def check():
    """A screenshot is the cheapest proof that the serial monitor is alive."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        shot = Path(td) / "check.png"
        # -S takes its filename attached, not as a separate argument.
        proc = m65(f"-S{shot}", timeout=90)
        if shot.is_file() and shot.stat().st_size > 0:
            print(f"MEGA65 responded ({find_m65()})")
            return 0
    print("no response from the MEGA65", file=sys.stderr)
    sys.stderr.write(proc.stderr[-500:] if proc.stderr else "")
    return 1


def main():
    args = sys.argv[1:]

    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0

    if args[0] == "--check":
        return check()

    if args[0] == "--shot":
        if len(args) < 2:
            raise SystemExit("--shot needs an output path")
        out = Path(args[1]).resolve()
        proc = m65(f"-S{out}", timeout=90)
        if out.is_file():
            print(f"wrote {out}")
            return 0
        sys.stderr.write(proc.stderr or "")
        return 1

    if args[0] == "--run":
        if len(args) < 2:
            raise SystemExit("--run needs a PRG path")
        prg = Path(args[1]).resolve()
        if not prg.is_file():
            raise SystemExit(f"no such file: {prg}")
        # -F resets first. That is correct here: there is no mounted disk
        # image for the reset to drop.
        proc = m65("-F", "-r", str(prg), timeout=120)
        sys.stderr.write(proc.stderr[-800:] if proc.stderr else "")
        return proc.returncode

    if args[0] == "--mem":
        if len(args) < 3:
            raise SystemExit("--mem needs a hex range and an output path")
        rng, out = args[1], Path(args[2]).resolve()
        proc = m65(f"--memsave={rng}={out}", timeout=90)
        if out.is_file():
            print(f"wrote {out} ({out.stat().st_size} bytes)")
            return 0
        sys.stderr.write(proc.stderr or "")
        return 1

    if args[0] == "--":
        args = args[1:]
    proc = m65(*args)
    sys.stdout.write(proc.stdout or "")
    sys.stderr.write(proc.stderr or "")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
