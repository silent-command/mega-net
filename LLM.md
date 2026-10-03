# mega-net

A native TCP/IP stack for the MEGA65: Ethernet, ARP, IPv4, ICMP, UDP,
DHCP with lease renewal, DNS, NTP, and TCP with eight sockets, in C99
with a small assembly core, callable from C, assembly and BASIC 65
through one jump table. 0BSD.

**Status: complete for its first applications and in use.** Three
clients run on it, `../mega-gopher`, `../mega-ftp` and `../mega-ssh`. ABI 0.2,
35 entries, appended only. The host suite is 367 checks. The image is
32 KB in a 40 KB window. No known defects.

## Read first

1. This file.
2. `docs/PLATFORM.md`: the machine, the family's memory map, the traps,
   the tools, the test driver. Every client points here too.
3. `REQUIREMENTS.md`: section 2 decisions (settled), section 4 the
   plan, section 5 the findings, 5.1 to 5.18. The README and the guides
   under `docs/` are for users.

## Commands

```
python3 build.py test     host suite; must be 367 checks, 0 failed before anything else
python3 build.py abi      the image (build/m65/meganet.bin), the trampoline as C (build/gen/), the spikes
python3 build.py m65      the portable core alone, for the target
python3 tools/basic_d81.py            a disk for BASIC callers: TRAMP, MEGANET, the guide's and the tutorial's programs
python3 tools/server_test.py IP       drive spike_server on the machine: two echo lines, a refusal, UDP
python3 tools/tcp_test_server.py LOG  a known TCP peer on :7070 for spike_tcp
python3 tools/device.py --check       does the machine answer
```

Clients build against `src/abi/meganet.h` and the two `abi` outputs,
and run `build.py abi` here when they are missing. After any ABI
change, rebuild all three clients.

## Layout

| Path | What |
|---|---|
| `src/net/` | The portable core, one file per protocol; `mn_net.c` ties them. C99 and `stdint.h`, nothing else. |
| `src/net/mn_netif.h` | The entire hardware boundary. |
| `src/hal/` | The MEGA65 side: the 45E100 driver, DMA, extended memory, the primitives. |
| `src/abi/` | The image's entry (`api.c`), the jump table, the trampoline, the linker scripts, `meganet.h`, `meganet_vectors.c` (the client's vector kit). |
| `src/spike/` | Hardware proofs, one PRG each: frame, ping, DHCP, time, TCP, a server, attic RAM, the STZ hazard. Regression probes. |
| `test/` | The host harness: `stub_link.c` backs `mn_netif` with memory; one test file per layer. |
| `tools/` | `device.py`, `mon.py` (the serial monitor), `basic_d81.py`, `m65lib.sh` (the script driver), the test servers. |
| `docs/` | `PLATFORM.md`, `ABI.md`, `TUTORIAL-BASIC.md` (the beginner's walk-through; its program ships as `TUTORIAL`), `GUIDE-BASIC.md`, `GUIDE-C-ASM.md`, `parity/`. |

## The ABI, in one paragraph

A headerless image at physical `$42000` whose first bytes are a `jmp`
table. During a call bank 4 is mapped over `$2000-$BFFF` and
`$E000-$FFFF`, the latter for the stack's own interrupt vectors at
`$4FFF0`. Arguments and results are A/X/Y/Z; anything larger is a
28-bit pointer in A/X/Y to a block in the caller's memory, moved by
DMA. The stack's zero page is `$90-$FF`, swapped with the caller's on
every call, never merely restored (5.3, 5.6). `INIT` is the crt0. Every
address derives from `MEGANET_TR` (`$1600`). Entries are appended,
never reordered. `src/spike/spike_abi.c` is the C example,
`tools/basic/mnbasic.bas` the BASIC one. Build every new feature
behind the table from the start.

## Rules

1. **Z is 0 whenever C runs.** Every routine that loads Z ends with
   `LDZ #0`. `spike_stz.c` shows the failure (5.4).
2. **Restore the caller's map, never a zero map**; the mailbox holds
   it. Zeroing MAPHI swaps in the C64 KERNAL and the next interrupt
   hangs the machine.
3. **`INIT` zeroes `.bss` and `.zp.bss` and copies `.zp.data`**, and
   installs the window's vectors before anything else, because the
   controller's events arrive during it (5.18, commit 354c6dc). Module
   state is pinned to `.bss` by `MN_BSS`; zero page is for temporaries.
4. **Diagnostic buffers get their own page** (5.7).
5. **Save and restore the DMA list registers** `$D701/$D702/$D704`
   around every job (5.10).
6. **The trampoline is at `$1600`, one address for every caller**,
   never a build-time choice (5.10).
7. **The window brings its own vectors**; never map it without the RTI
   stub and the three vectors at `$4FFF0` (5.11, 5.16).
8. **The socket pool is bank 5, never attic RAM** (5.13, 5.15, 5.16).
9. **Find the bytes before adding code.** The window has about 3 KB.
   State into the pool, transient buffers onto `mn_net_scratch()`,
   counters 16-bit, no `uint32_t` arithmetic outside accumulators.
10. **A generic stack takes no special case for one application.** An
    SSH client, a TLS layer, an FTP server all layer over sockets, as
    the three clients do.
11. **Every wait is bounded on `$D7FA`; nothing blocks on the network;
    the caller polls.** The sender keeps a small congestion window with
    go-back-one retransmission (5.17).
12. **A C caller owns its vectors and quiets the controller before the
    first call**; it is the caller's job and `meganet_own_vectors()` is
    the kit (5.18). Document it wherever a client is written.
13. **Windows, Linux and macOS are all first-class hosts.** No shell in
    the build path; no platform headers in `src/net/`; the compiler and
    the `m65` tool discovered, never assumed (`tools/device.py`,
    `MEGA65_M65`, `MEGA65_PORT`, `LLVM_MOS_DIR`).

## Working method

- The host harness first: add the case to `test/` before touching
  hardware. Turn an intermittent fault deterministic before debugging
  it; halting the CPU hides timing faults, so read state after the
  fact with `tools/mon.py` and `MN_CRUMB` (`-DMN_CRUMBS`).
- Test BASIC callers from a tokenized program on disk
  (`tools/basic_d81.py`), never by typing through `m65 -T`.
- Record every hardware finding in `REQUIREMENTS.md`, numbered, with
  what was seen and what it cost. Nothing in this file is a claim
  without a section there.
- Commits are local until the user asks for a push or a release. A
  release is the BASIC callers' disk, `build/basic/MNBASIC.D81`,
  rebuilt from `build.py abi` first.
