#!/usr/bin/env python3
"""Cross-platform build driver for mega-net.

The stack targets the MEGA65, but the work happens on a development host,
and that host may be Windows, Linux or macOS. Shell scripts would have made
Windows a second-class citizen, so the build lives here instead: Python 3 is
the one thing all three platforms already have, and it is already the
tooling language used elsewhere in this family of projects.

    python3 build.py test     build and run the host test suite
    python3 build.py m65      compile the portable core for the MEGA65
    python3 build.py spike    link the step 2 hardware spike (a PRG)
    python3 build.py abi      the banked stack image, trampoline, ABI spike
    python3 build.py all      both
    python3 build.py clean

Overrides, all optional:

    CC              host compiler (default: cc/gcc/clang, or cl on Windows)
    LLVM_MOS_DIR    llvm-mos SDK root (default: ~/llvm-mos, or PATH)
"""

import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
IS_WINDOWS = platform.system() == "Windows"


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def find_host_cc():
    """Pick a host compiler. MSVC is supported but not the primary path."""
    if os.environ.get("CC"):
        return os.environ["CC"]
    candidates = ["cc", "gcc", "clang"]
    if IS_WINDOWS:
        # Prefer a GNU-style compiler if one is installed; fall back to MSVC.
        candidates = ["gcc", "clang", "cc", "cl"]
    for c in candidates:
        if shutil.which(c):
            return c
    die("no host C compiler found; set CC")


def is_msvc(cc):
    return Path(cc).stem.lower() == "cl"


def sources(subdir):
    return sorted(str(p) for p in (ROOT / subdir).glob("*.c"))


def run(cmd):
    print("  " + " ".join(Path(c).name if os.sep in c else c for c in cmd))
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        die(f"command failed with exit {proc.returncode}")


def build_host():
    cc = find_host_cc()
    out = BUILD / "host"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ("mn_tests.exe" if IS_WINDOWS else "mn_tests")

    srcs = sources("src/net") + sources("test")
    if not srcs:
        die("no sources found")

    if is_msvc(cc):
        # MSVC: /W4 and warnings-as-errors, C11 (it has no C99 mode).
        cmd = [cc, "/nologo", "/std:c11", "/W4", "/WX", "/Od", "/Zi",
               f"/Fe:{exe}", f"/Fo:{out}\\"] + srcs
    else:
        cmd = [cc, "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-o", str(exe)] + srcs

    print(f"host build ({Path(cc).name}):")
    run(cmd)
    print(f"built {exe}")

    print()
    return subprocess.run([str(exe)]).returncode


def find_mos_clang():
    """Locate mos-mega65-clang across platforms and install layouts."""
    exe = "mos-mega65-clang.exe" if IS_WINDOWS else "mos-mega65-clang"

    root = os.environ.get("LLVM_MOS_DIR")
    roots = [Path(root)] if root else []
    roots += [Path.home() / "llvm-mos", Path("/opt/llvm-mos"),
              Path("/usr/local/llvm-mos")]
    if IS_WINDOWS:
        roots.append(Path("C:/llvm-mos"))

    for r in roots:
        cand = r / "bin" / exe
        if cand.is_file() and os.access(cand, os.X_OK):
            return str(cand)

    found = shutil.which(exe)
    if found:
        return found

    die("llvm-mos not found. Install the SDK and set LLVM_MOS_DIR to its "
        "root, or put mos-mega65-clang on PATH.")


def build_m65():
    clang = find_mos_clang()
    out = BUILD / "m65"
    out.mkdir(parents=True, exist_ok=True)

    # There is no MEGA65 program to link yet -- step 2 is the 45E100 spike.
    # Compiling the core now catches portability breaks as they are made,
    # rather than weeks later.
    print(f"target build ({Path(clang).name}):")
    for src in sources("src/net"):
        obj = out / (Path(src).stem + ".o")
        run([clang, "-std=c99", "-Os", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-c", src, "-o", str(obj)])
    print("core compiles for mega65")
    return 0


def build_spike():
    """Links the step 2 spike into a native $2001 PRG. HAL + spike only;
    src/net/ is compiled too so the netif contract is checked, but the
    spike calls none of it beyond the struct."""
    clang = find_mos_clang()
    out = BUILD / "m65"
    out.mkdir(parents=True, exist_ok=True)
    prg = out / "spike_frame.prg"

    srcs = sources("src/hal") + [str(ROOT / "src/spike/spike_frame.c")]
    print(f"spike build ({Path(clang).name}):")
    run([clang, "-std=c99", "-Os", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-o", str(prg)] + srcs)
    size = prg.stat().st_size
    with open(prg, "rb") as f:
        lo, hi = f.read(2)
    print(f"built {prg} ({size} bytes, load ${hi:02x}{lo:02x})")
    return 0


def _pad_to_bss(image, mapfile):
    """TRIM(ram) drops trailing zero bytes, which can eat the tail of
    .data. Pad the image back out to __bss_start so the loaded bytes are
    exactly the link's idea of initialised memory."""
    import re
    # Map line looks like: "    26ca     26ca        0     1         __bss_start = ."
    m = re.search(r"^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+\d+\s+__bss_start\b",
                  mapfile.read_text(), re.M)
    if not m:
        print("  (no __bss_start in map; image not padded)")
        return
    want = int(m.group(1), 16) - 0x2000
    have = image.stat().st_size
    if have < want:
        with open(image, "ab") as f:
            f.write(b"\0" * (want - have))
        print(f"  padded {have} -> {want} bytes (to __bss_start)")
    else:
        print(f"  image ends at __bss_start (${want + 0x2000:04x}); no padding needed")


def _emit_payload(gen_dir, blobs, name="meganet_payload"):
    """Write <name>.{c,h}: each blob as a C array plus its size."""
    gen_dir.mkdir(parents=True, exist_ok=True)
    guard = name.upper() + "_H"
    h = ["/* GENERATED by build.py -- do not edit. */",
         f"#ifndef {guard}", f"#define {guard}"]
    c = ["/* GENERATED by build.py -- do not edit. */",
         f'#include "{name}.h"']
    for sym, path in blobs:
        data = path.read_bytes()
        macro = sym.upper() + "_SIZE"
        h.append(f"#define {macro} {len(data)}u")
        h.append(f"extern const unsigned char {sym}[{macro}];")
        c.append(f"const unsigned char {sym}[{macro}] = {{")
        for i in range(0, len(data), 12):
            c.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",")
        c.append("};")
    h.append("#endif")
    (gen_dir / f"{name}.h").write_text("\n".join(h) + "\n")
    (gen_dir / f"{name}.c").write_text("\n".join(c) + "\n")


def _check_trampoline_symbols(clang, tramp, header, base):
    """meganet.h fixes where tr_call sits relative to the base. Make the
    linked address and the header agree, or fail the build: a client calling into the middle of a routine is how one run
    ended in jsr $0000."""
    import re
    nm = Path(clang).parent / "llvm-nm"
    out = subprocess.run([str(nm), str(tramp) + ".elf"], capture_output=True,
                         text=True).stdout
    syms = {m.group(2): int(m.group(1), 16)
            for m in re.finditer(r"^([0-9a-fA-F]+)\s+\w\s+(meganet_tr_\w+)$", out, re.M)}
    hdr = header.read_text()
    for name, macro in (("meganet_tr_call", "MEGANET_TR_CALL_OFF"),):
        m = re.search(r"#define " + macro + r"\s+0x([0-9a-fA-F]+)", hdr)
        if not m or name not in syms:
            die(f"cannot check {name}: header={bool(m)} elf={name in syms}")
        want, have = base + int(m.group(1), 16), syms[name]
        if want != have:
            die(f"{name}: meganet.h puts it at ${want:04x} but it linked at ${have:04x}")
    print(f"  tr_call ${syms['meganet_tr_call']:04x}: header agrees")


def build_trampoline(clang, out, gen, image=None):
    """The trampoline at $1600 (REQUIREMENTS.md 5.10): the raw binary, and
    meganet_tramp.{c,h} for a client to embed -- with MEGANET_BIN_SIZE
    when the image is known, for clients that load it from disk."""
    out.mkdir(parents=True, exist_ok=True)
    abi = ROOT / "src" / "abi"
    tramp = out / "meganet_call.bin"
    print(f"trampoline ({Path(clang).name}):")
    run([clang, "-nostdlib", "-T", str(abi / "trampoline.ld"),
         str(abi / "trampoline.S"), "-o", str(tramp)])
    print(f"  {tramp.name}: {tramp.stat().st_size} bytes")
    _check_trampoline_symbols(clang, tramp, abi / "meganet.h", TRAMP_BASE)
    _emit_payload(gen, [("meganet_call_bin", tramp)], name="meganet_tramp")
    if image is not None:
        h = gen / "meganet_tramp.h"
        h.write_text(h.read_text().replace(
            "#endif", f"#define MEGANET_BIN_SIZE {image.stat().st_size}u  /* size of meganet.bin on disk */\n#endif"))
    return tramp


TRAMP_BASE = 0x1600


def build_abi():
    """The banked stack image, its trampoline, and the ABI spike client."""
    clang = find_mos_clang()
    out = BUILD / "m65"
    gen = BUILD / "gen"
    out.mkdir(parents=True, exist_ok=True)
    abi = ROOT / "src" / "abi"
    warn = ["-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]

    # 1. Trampoline at the reference base, $1600 (BASIC-safe, 5.10).
    tramp = build_trampoline(clang, out, gen)

    # 2. Stack image: raw binary linked at $2000, physical $42000.
    image = out / "meganet.bin"
    mapfile = out / "meganet.map"
    srcs = ([str(abi / "jumptable.S"), str(abi / "api.c")] + sources("src/hal")
            + sources("src/net"))
    print("stack image:")
    # -Oz, not -Os: 3.5 KB of the 40 KB window, at no cost the 16 KB echo
    # can measure (5.17). It wants more zero page than -Os; the module
    # state that would have gone there is pinned to .bss (MN_BSS).
    run([clang, "-std=c99", "-Oz", "-nostartfiles", "-DMN_PHYS_BASE=0x40000UL",
         "-T", str(abi / "meganet.ld"), f"-Wl,-Map={mapfile}"] + warn +
        srcs + ["-o", str(image)])
    _pad_to_bss(image, mapfile)
    with open(image, "rb") as f:
        first = f.read(1)[0]
    print(f"  {image.name}: {image.stat().st_size} bytes, first byte "
          f"${first:02x} ({'jmp: ok' if first == 0x4c else 'NOT a jmp!'})")
    if first != 0x4c:
        die("jump table is not at the start of the image")

    # 3. Both blobs as C arrays for clients to embed -- and the trampoline
    #    alone, for clients that load the image from disk (gopher).
    _emit_payload(gen, [("meganet_bin", image), ("meganet_call_bin", tramp)])
    # The DHCP state's mapped address, for the renewal spike, which nudges
    # the lease clock through DMA (5.14). Diagnostic only; not part of the ABI.
    import re as _re
    m = _re.search(r"^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+\d+\s+\S+:\(\.bss\.dhcp\)",
                   mapfile.read_text(), _re.M)
    if m:
        h = gen / "meganet_payload.h"
        h.write_text(h.read_text().replace("#endif", f"#define MEGANET_DHCP_STATE_ADDR 0x{int(m.group(1), 16):04X}u  /* mapped; diagnostic (5.14) */\n#endif"))
    build_trampoline(clang, out, gen, image)

    # 4. The clients: the ABI spike, and the ping client.
    for name, extra in (("spike_abi", ["getp.S", "irqhook.S"]), ("spike_ping", []),
                        ("spike_dhcp", []), ("spike_time", []), ("spike_dhcpdiag", []),
                        ("spike_tcp", []), ("spike_server", []), ("spike_dhcprenew", []), ("spike_attic", [])):
        prg = out / f"{name}.prg"
        print(f"{name}:")
        run([clang, "-std=c99", "-Os", "-I", str(gen)] + warn +
            [str(ROOT / f"src/spike/{name}.c")] +
            [str(ROOT / "src/spike" / e) for e in extra] +
            [str(gen / "meganet_payload.c"), str(ROOT / "src/hal/mn_dma.c"),
             "-o", str(prg)])
        print(f"  {prg.name}: {prg.stat().st_size} bytes")
    return 0


def build_stz():
    """The STZ-hazard demonstrator (REQUIREMENTS.md 5.4)."""
    clang = find_mos_clang()
    out = BUILD / "m65"; out.mkdir(parents=True, exist_ok=True)
    prg = out / "spike_stz.prg"
    run([clang, "-std=c99", "-Os", str(ROOT / "src/spike/spike_stz.c"),
         str(ROOT / "src/spike/stz.S"), "-o", str(prg)])
    print(f"  {prg.name}: {prg.stat().st_size} bytes")
    return 0


def clean():
    if BUILD.exists():
        shutil.rmtree(BUILD)
        print(f"removed {BUILD}")
    else:
        print("nothing to clean")
    return 0


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "test"

    if target == "test":
        return build_host()
    if target == "m65":
        return build_m65()
    if target == "spike":
        return build_spike()
    if target == "abi":
        return build_abi()
    if target == "stz":
        return build_stz()
    if target == "all":
        rc = build_m65()
        return build_host() or rc
    if target == "clean":
        return clean()

    print(__doc__)
    die(f"unknown target '{target}'")


if __name__ == "__main__":
    sys.exit(main())
