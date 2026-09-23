#!/usr/bin/env python3
"""Build a .d81 for BASIC callers: the stack image and the trampoline as
PRG files (two-byte load address first) so BASIC 65's BLOAD puts them
where the ABI expects -- MEGANET at $2000 in bank 4, TRAMP at $1600.

    python3 tools/basic_d81.py [out.d81]      (after: python3 build.py abi)

The BASIC program tools/basic/mnbasic.bas goes on the disk too, tokenized
by petcat as MNBASIC: RUN "MNBASIC". See REQUIREMENTS.md 5.10."""
import os, shutil, subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
m65 = root / "build" / "m65"
out = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build" / "basic" / "MNBASIC.D81"
out.parent.mkdir(parents=True, exist_ok=True)

def prg(name, blob, addr):
    p = out.parent / name
    p.write_bytes(bytes((addr & 0xff, addr >> 8)) + blob)
    return p

meganet = prg("meganet.prg", (m65 / "meganet.bin").read_bytes(), 0x2000)
tramp = prg("tramp.prg", (m65 / "meganet_call.bin").read_bytes(), 0x1600)

# The BASIC program itself, tokenized: typing it in over the serial monitor
# drops lines (5.10), so it travels on the disk too, as MNBASIC.
petcat = shutil.which("petcat") or os.environ.get("PETCAT")
if not petcat and Path("/opt/homebrew/bin/petcat").exists():
    petcat = "/opt/homebrew/bin/petcat"
if not petcat:
    sys.exit("petcat (VICE) not found; set PETCAT=/path/to/petcat")
basprg = out.parent / "mnbasic.prg"
subprocess.run([petcat, "-w65", "-o", str(basprg), "--",
                str(root / "tools" / "basic" / "mnbasic.bas")], check=True)
guideprg = out.parent / "guide.prg"                    # the recipes from docs/GUIDE-BASIC.md
subprocess.run([petcat, "-w65", "-o", str(guideprg), "--",
                str(root / "tools" / "basic" / "guide.bas")], check=True)
tutorprg = out.parent / "tutorial.prg"                 # the program docs/TUTORIAL-BASIC.md builds
subprocess.run([petcat, "-w65", "-o", str(tutorprg), "--",
                str(root / "tools" / "basic" / "tutorial.bas")], check=True)

c1541 = shutil.which("c1541") or os.environ.get("C1541")
if not c1541:
    for cand in ("/opt/homebrew/bin/c1541", "/usr/local/bin/c1541"):
        if Path(cand).exists():
            c1541 = cand
if not c1541:
    sys.exit("c1541 (VICE) not found; set C1541=/path/to/c1541")

if out.exists():
    out.unlink()
subprocess.run([c1541, "-format", "mnbasic,mn", "d81", str(out),
                "-write", str(meganet), "meganet",
                "-write", str(tramp), "tramp",
                "-write", str(basprg), "mnbasic",
                "-write", str(guideprg), "guide",
                "-write", str(tutorprg), "tutorial"], check=True)
subprocess.run([c1541, "-attach", str(out), "-dir"], check=True)
print(f"{out} ({out.stat().st_size} bytes)")
