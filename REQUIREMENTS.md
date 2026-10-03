# mega-net — Requirements

> **Platform knowledge lives in [../mega-gopher/PLATFORM-NOTES.md](../mega-gopher/PLATFORM-NOTES.md)**
> until this project has enough findings of its own to justify a copy. That
> file is project-independent and is the thing to read first. This document
> keeps the mega-net decisions and, as the project goes on, the narrative of
> how each finding was reached — the same convention as gopher's
> `REQUIREMENTS.md`.

## 1. Goal

A native TCP/IP stack for the MEGA65, written in portable C with a small
assembly core, that replaces the third-party stack the gopher client depended on.
The purpose is not parity with the earlier stack: it is
to make the MEGA65 a **server or peer** rather than only a client, which is
what FTP, a proxy, and a multi-line BBS require and what the earlier
stack's single-socket design cannot provide.

## 2. Decisions already made

| Question | Decision |
|---|---|
| **Why replace, not patch** | Three reasons, all from the gopher project. (1) the earlier stack's 100 ms delay spins on a VIC-II raster register that never advances in native MEGA65 mode, so `ETH_INIT` never returns without a local patch. (2) It is single-socket; multi-socket means multiplexing connection state through every layer — a rewrite, not an increment. (3) It has no upstream license file. Patching addresses (1) only. |
| **License** | **0BSD.** Deliberate: partly so the stack is unambiguously reusable, partly because the earlier stack's licensing ambiguity is a background reason this project exists. |
| **Run mode** | **Native MEGA65 mode only.** No C64/GO64 mode. Decided 2026-09-04. |
| **Development hosts** | **Windows, Linux and macOS are all first-class.** A hard requirement, not a preference — decided 2026-09-04. The build is Python (`build.py`), never shell-only; the compiler and the MEGA65 tool are discovered, never assumed; no absolute paths in anything committed. See `LLM.md` for the checklist. |
| **Architecture: host-testable core** | The stack is portable C99 with the hardware behind one thin interface, `mn_netif`. It compiles natively on the host and is exercised by tests that inject synthetic frames. This was the highest-leverage lesson from gopher: a 60-second edit-deploy-observe loop with unreliable feedback is what made the last project painful, and a state machine you can run in milliseconds changes the character of the work. Done in `4c9a5e6`. |
| **Architecture: banked binary + jump table** | **A day-one requirement**, decided 2026-09-04. The stack ships as a banked binary with a jump-table ABI so that C, assembly and BASIC callers all work from the start — the same shape as the earlier stack's `SYS`-style table, which the gopher client already drove through its own trampoline and far-call header. Not yet built; it shapes step 2 onward. |
| **ABI shape (R-13)** | **The earlier stack's shape, with DMA parameter blocks.** The stack is a headerless image at physical `$42000` (bank 4) with a `jmp` table at its first bytes; it runs with bank 4 mapped over CPU `$2000-$7FFF`. BASIC calls it with `SYS $42xxx,a,x,y,z` / `RREG` exactly as it called the earlier stack, because BASIC 65's `SYS` above `$FFFF` performs that MAP itself. C and assembly callers go through a 71-byte trampoline at `$0334`. Arguments and results are A/X/Y/Z; anything larger travels as a **28-bit pointer in A/X/Y to a parameter block in the caller's memory, read and written back by DMA**. That is the one departure from the earlier stack and the reason for it: the stack never reads the caller's memory through the CPU — while it runs, the caller's `$2000-$7FFF` is not there — and the earlier stack's answer to that was byte-at-a-time register calls, which is what made gopher's receive path painful. DMA does not care what is mapped. Alternatives rejected: the upper-quadrant window (`$8000-$BFFF`) is 16 KB and a C stack will exceed it (the earlier stack is 22 KB of assembly); bank 0 collides with BASIC ROM at `$A000`. Decided and proven 2026-09-04, see 5.3. |
| **Interrupts and the memory map (R-19, closed)** | The trampoline restores the caller's real map after every call — `$2000-$7FFF` identity to bank 0 and the **C65 KERNAL at `$E000` from bank 3** — and hands back the caller's I flag. A C caller may therefore run with interrupts enabled while using the stack; measured by counting KERNAL IRQs through `($0314)` with the stack in use (5.4). Gopher's trampoline restored a zero map, which removes the KERNAL: that, not the controller reset, was its "cli hangs" debt. Decided and proven 2026-09-04. |
| **Z is zero whenever C runs** | llvm-mos compiles every store of zero as `STZ`, which on the 45GS02 stores the **Z register**. Any assembly that loads Z — every MAP sequence — must leave Z = 0 before C runs again, on both sides of the ABI. `src/spike/spike_stz.c` is the twelve-byte demonstrator. See 5.4 for the day this cost. |
| **No crt0 means INIT is the crt0** | The image has no runtime start-up, so `INIT` does what one would: zero `.bss`, zero `.zp.bss`, and **copy `.zp.data` from its load address in the image into zero page**. The third was missing until the compiler moved a four-byte constant there (5.7). Module state is pinned to `.bss` by named sections so zero page holds only the compiler's temporaries. |
| **The window is 40 KB** | During a call bank 4 is mapped over `$2000-$BFFF` (MAPLO `X=$E4`, MAPHI `Z=$34`), with the stack's soft stack at `$BC00-$BFFF`. A 24 KB window filled up at 13.8 KB of image plus 10 KB of `.bss` and left 79 bytes below a 512-byte soft stack (5.7). MAPHI has one offset, so the caller's KERNAL mapping is absent *during* a call and restored after — harmless with interrupts off. |
| **Stack zero page** | `$90-$FF`, **swapped** with the caller's around every call — the caller's copy goes to safety on entry and ours comes back, the reverse on exit — so the stack's own zero-page statics persist between calls. **Not** `$02-$8F`: that is every llvm-mos program's imaginary-register and `.zp` range, and the compiler puts small statics there unasked (5.3). And zero page is always bank 0, whatever the image's base: `MN_PHYS()` knows, and the DMA job list is kept out of it (5.6). |
| **Scope: DHCP and DNS** | **In scope**, decided 2026-09-04. Both were available in the earlier stack and the gopher client depends on them; parity is not reachable without them. |
| **Scope: multi-socket** | Required — it is the point of the project — but **after** the single-connection parity test in step 5. |
| **Memory** | Use what is needed, **but the stack must leave room for real applications** (FTP, a multi-line BBS) to run alongside it. Use every kind of memory the platform offers, **including attic RAM**. Decided 2026-09-04. No fixed budget; the constraint is "an application can still fit". |
| **Packet access** | **No structs overlaid on packets.** Every field goes through the big-endian byte accessors in `mn_byteorder.h`. llvm-mos and the host compiler cannot be relied on to lay out packed structs identically, and a stack that passes on the host but fails on the target would defeat the purpose of the host harness. |
| **Dependencies** | The stack core (`src/net/`) depends on `stdint.h` and nothing else — no mega65-libc, no platform headers. The target-side HAL may use whatever it needs. |
| **Toolchain** | llvm-mos (`mos-mega65-clang`) for the target; any C99 compiler plus Python 3 for the host; `64tass` for assembly; `c1541` from VICE for disk images. Same as gopher. |
| **Verification host** | The development Mac shares a LAN segment with the MEGA65, so `tcpdump` on the host is a valid witness for frames the MEGA65 transmits. Confirmed 2026-09-04. |

## 3. Requirements

Numbered so they can be referred to from commits and findings.

### Functional

- **R-1** Send and receive raw Ethernet frames on the 45E100.
- **R-2** ARP: resolve addresses, answer requests for our address.
- **R-3** IPv4 with correct header checksums; ICMP echo reply so the machine
  answers `ping`.
- **R-4** UDP.
- **R-5** DHCP client: obtain address, mask, gateway and DNS server.
- **R-6** DNS resolver (A records at minimum).
- **R-7** NTP client, as the first real application — set the MEGA65's RTC
  from the internet.
- **R-8** TCP, single connection: connect, send, receive, close, with
  correct sequence arithmetic, retransmission and FIN handling.
- **R-9** The gopher client runs on mega-net with no loss of function
  against its existing adversarial test suite. This is the parity gate.
- **R-10** Multiple simultaneous sockets, including listening sockets, so the
  MEGA65 can act as a server.

### Architectural

- **R-11** The stack core is portable C99 that compiles and runs natively on
  the development host, with the hardware behind `mn_netif` and nothing
  else.
- **R-12** Every protocol layer has host tests that inject synthetic frames.
  New behaviour comes with a test that exercises it on the host before it is
  tried on hardware.
- **R-13** The stack ships as a banked binary with a jump-table ABI callable
  from C, assembly and BASIC.
  The stack reserves the page `$1600-$16FF` of bank 0 in every client
  for its trampoline (5.10): one address, the same for C, assembly and
  BASIC.
  It also reserves `$4FFF0-$4FFFF` for its own interrupt vectors, which
  are in force while its window is mapped (5.11), and the first 40 KB
  of attic RAM (bank 5 on a machine without it) for socket buffers.
- **R-14** Every wait is bounded. Timing uses the `$D7FA` frame-counter
  idiom, never a VIC-II raster register.
- **R-15** The stack leaves enough memory for an application such as FTP or
  a multi-line BBS to run beside it, using attic RAM and other platform
  memory as appropriate.
- **R-16** Native MEGA65 mode only.

### Process

- **R-17** Windows, Linux and macOS are all supported development hosts.
- **R-18** 0BSD license, present from the first commit.
- **R-19** Intermittent faults are made deterministic before they are
  debugged — the working habit from gopher, kept on purpose.
- **R-20** The stack leaves shared hardware state as it found it. Registers
  another program relies on across its own operations — the DMA list
  address registers first of all — are saved and restored around every
  use (5.10). The I/O personality is the one exception: the stack asserts
  the MEGA65 personality on every entry and does not restore it, because
  it cannot be read back.

## 4. Staged plan

Ordered by what kills the project earliest if it does not work.

| Step | Delivers | Status |
|---|---|---|
| 1 | Host test harness, stub link layer, checksum and Ethernet layers | **Done** `4c9a5e6` |
| 2 | 45E100 spike: one raw frame out and one in, witnessed on the host | **Done** — see 5.2 |
| 2b | Banked image + jump-table ABI (R-13), proven by re-running the spike through it | **Done** — see 5.3 |
| 2c | Interrupts enabled for callers (the R-19 debt), proven with the stack in use | **Done** — see 5.4 |
| 3 | ARP + ICMP until the MEGA65 answers a ping | **Done** — `ping` answered, 50/50 at 50 ms, min 7 ms; see 5.5 |
| 4 | UDP, DHCP, DNS, then NTP as the first application | **Done** — the MEGA65 sets its clock from the internet (5.7) |
| 5 | Single-connection TCP; gopher client ported as the parity test | **Done** — TCP on hardware (5.8); the gopher client ported and run against the internet, the windowing server and the adversarial cases (5.9) |
| 5b | BASIC caller (R-13's third language), proven on hardware | **Done** — DHCP, GET_IP and ping from a BASIC 65 program (5.10) |
| 6 | Multi-socket, listening sockets | **Done** — eight sockets, listeners, RST for orphans, UDP for applications; the MEGA65 as a server, driven from the Mac (5.11) |

DHCP and DNS were added to step 4 on 2026-09-04 when they were confirmed
in scope; they were not in the original five-step sequencing.

## 5. Findings

Narrative of what was learned, in order. Each entry should say what was
tried, what happened, and what it means — the point is that the next person
does not have to rediscover it.

### 5.1 45E100 driver sequence, from the earlier stack's driver (2026-09-04)

Read from the earlier stack's driver source rather than guessed. The register map is
in `mega65.asm`: `CTRL1 $D6E0`, `CTRL2 $D6E1`, `TXSIZE $D6E2/$D6E3`,
`COMMAND $D6E4`, `CTRL3 $D6E5`, `MAC $D6E9-$D6EE`.

**Initialisation.** Configure the RX filter through `CTRL3`: clear bit 5
(no multicast), set bit 4 (broadcast on — needed for ARP) and bit 0
(`NOPROM`, promiscuous off). Read the MAC from `$D6E9`. Then reset: `$00`
to `CTRL1`, wait, `$03` to `CTRL1`, wait; pulse the TX state machine with
`$03` then `$00` to `CTRL2`; then wait four seconds for the PHY. the earlier stack's
waits are the raster-spinning `ETH_WAIT_100MS` — this is the bug from
decision 1, and mega-net uses the frame counter instead (R-14).

**Transmit.** Pad the frame to 60 bytes. Write the length to `TXSIZE`. DMA
the frame to the controller buffer at `$FFDE800`. Make sure `CTRL1` is
`$03`. Wait, bounded, for `CTRL1` bit 7 (TX idle). Write `$01` to
`COMMAND`.

**Receive.** A frame is waiting when `CTRL2` bit 5 is set. The buffer at
`$FFDE800` begins with two metadata bytes: `+0` is the length low byte;
`+1` has the length high nibble in bits 0–3, and flags in bits 4–7 —
multicast, broadcast, unicast-to-me, and **CRC error** in bit 7. The frame
itself starts at `$FFDE802`. After handling it, rotate the buffer by writing
`$01` then `$03` to `CTRL2`.

**Interrupts.** `CTRL2` bits 7 and 6 (`RXQEN`, `TXQEN`) enable the
controller's RX/TX interrupts. Per `PLATFORM-NOTES.md`, leave them clear
unless a handler is installed, and clear them read-modify-write — the low
bits of `CTRL2` are the queue-advance controls and a blind store there
loses frames. mega-net polls; it does not use these interrupts.

**Why this matters.** The buffer at `$FFDE800` is in the 28-bit address
space and is not reachable from the 6502 without DMA or a memory map. That
makes the DMA helper part of the HAL, not an optimisation, and it is why
the step 2 spike is a real test and not a formality.

### 5.2 Step 2 spike: raw frames both ways on real hardware (2026-09-04)

`src/spike/spike_frame.c`, 1,649 bytes, load `$2001`, run with
`tools/device.py --run`. It brings up the 45E100 through
`src/hal/mn_eth45e100.c`, sends one hand-built frame every ~2 s, and
records everything it sees in a status block at `$1400`, read back with
`--mem 1400:1480`. No screen code: nothing else had to work for the spike
to report.

**The witness is the host's ARP table, not tcpdump.** `tcpdump` needs
`/dev/bpf`, which is root-only on macOS, and the project should not need
root to verify itself. So the spike's raw frame is shaped as an ARP request
for the host's own address (`192.168.1.232`), from a spare address on the
subnet (`192.168.1.199`, confirmed unused first). A kernel that receives a
well-formed request for itself learns the sender: `arp -an` went from
`(incomplete)` to `ee:2b:df:69:8f:f5` four seconds after the program
started — and that is exactly the MAC the spike read out of `$D6E9`. The
frame was not just sent; it was parsed by a real stack. The host's reply
then arrives as a unicast frame, exercising the receive side with no
further arrangement. Pinging the spare address in a loop doubles as the
wait and as a source of broadcast ARP requests for the MEGA65 to receive.

**Results.** TX count 1, TX failures 0. RX count 9 within a few seconds of
start (the LAN is chatty); state `GOT_RX`. `CTRL1` read `$91` — TX idle
set — and `CTRL2` `$16`. The most recent frame captured was a genuine
broadcast ARP request from the router (`3c:bd:c5:24:6a:81`, `192.168.1.1`)
asking for `.246`.

**Finding: the reported RX length includes the FCS.** A 42-byte ARP
request, padded to the 60-byte minimum, was reported as **64**, and the
four bytes after the zero padding were `d2 37 0d 10` — the CRC. the earlier stack's
own comment ("frame length, excluding FCS") turns out to describe what it
*stores*, after subtracting. The driver now strips 4 bytes and treats
anything of 4 bytes or fewer as a bad frame. R-1 is satisfied only with
this fix; without it every upper layer would see four trailing bytes of
garbage.

**Timings that held.** 5 frames between reset and release; 200 frames
(~4 s) for the PHY, per the earlier stack, and the first successful transmit came
right after it. The frame-counter waits (R-14) worked first time; nothing
spun.

**What the spike proved, and what it did not.** Proved: I/O enable at
`$D02F`; the DMA job format and trigger at `$D701`/`$D705` from C, with no
mega65-libc; DMA to and from `$FFDE800`; the TX handshake; the RX queue and
its rotation; that a `$2001` PRG linked by `mos-mega65-clang` with no libc
runs natively. Not proved: anything about the banked binary and jump table
(R-13), which the spike does not use — that is the next piece of target
work, and it comes before ARP so that ARP is built in the final shape.

### 5.3 R-13: the banked ABI, proven end to end — and the zero-page bug that shaped it (2026-09-04)

`src/abi/` is the ABI: `meganet.ld` links a raw image at `$2000`;
`jumptable.S` is the table (six entries at `$2000+3N`, append-only) and
the prologue/epilogue every entry passes through; `api.c` is the C behind
it; `trampoline.S` is the 71-byte far-call at `$0334` for C and assembly
callers; `meganet.h` is what a client includes. `build.py abi` produces
`meganet.bin` (1,738 bytes), `meganet_call.bin`, both as C arrays in
`build/gen/meganet_payload.c`, and the acceptance client
`src/spike/spike_abi.c` — the step 2 spike again, but every frame now goes
through the banked stack.

**Layout, verified from the ELF before running.** Table at `$2000`
exactly; `.noinit` (register mailboxes and the zero-page save area) placed
immediately *after* `.bss`, so init's zeroing of `.bss` cannot touch it;
the 45GS02 `stz`/`ldz`/`map`/`eom` encode as `$9C`/`$AB`/`$5C`/`$EA` under
llvm-mos, which assembles them natively — no `64tass` needed. The image
ends exactly at `__bss_start`, so `TRIM(ram)` cost nothing; `build.py`
pads to `__bss_start` regardless, against the day `.data` grows a
zero-valued tail. One oddity: llvm-mos pulls a five-byte `.init.250`
(`lda #$0e / jsr $ffd2`, the charset shift) into the image at `$2012`.
Nothing calls it — there is no crt0 — and it is harmless dead bytes.

**Results, first run.** Image landed (`$4C` at `$42000`), trampoline
landed (`$78` at `$033E`), `VERSION` returned `0,1,'M','N'` through the
whole chain, `INIT` returned 0, `LINK_RX` delivered correct frames into
the caller's buffer with the length written back into the parameter
block. But `GET_MAC` returned `00:00:00:00:00:00`, and so every ARP request
the client sent had a zero sender MAC, and the host learned nothing.

**The cause was the protection, not the path.** Over the monitor, the
stack's own `nif.mac` in bank 4 (`$426CA`) read `ee:2b:df:69:8f:f5`,
matching the hardware registers at `$D6E9` — the stack was right. The
client's `mac[6]` had been placed by llvm-mos in **zero page, at `$2A`**
(`.zp.bss`: the compiler does this for small statics, unasked). `GET_MAC`
DMA'd the six bytes there correctly. Then the stack's epilogue restored
the caller's `$02-$8F` from the copy taken on entry, overwriting the
result with the zeros that were there before the call. `peek1` and
`frame`, in ordinary `.bss`, were untouched — which is why the load
checks and receive path passed while the MAC did not.

**The fix is structural.** Any scheme that saves and restores a caller's
zero page will revert results delivered into it, and the caller cannot be
asked to keep buffers out of zero page because the placement is the
compiler's, not theirs. So the stack's zero page is now **`$90-$BF`**,
disjoint from the `$02-$8F` every llvm-mos program owns; only that range
is saved and restored, for the benefit of BASIC, whose workspace it is
and which is not running during a `SYS`. After the change: `GET_MAC`
correct, the host's ARP table learned `.198` → `ee:2b:df:69:8f:f5` four
seconds after start, and the last frame received was a 98-byte unicast
IPv4 ICMP echo from the host — unpadded, so the FCS strip from 5.2 is
confirmed on a second frame shape.

**Two things carried, not solved.** The trampoline disables interrupts
and leaves them disabled on return, as gopher's did, because re-enabling
them after the controller reset hung that project's machine in a way
never made deterministic. That is an R-19 debt, not a design. And the
witness address had to move from `.199` to `.198`: a cached ARP entry on
the host would have made the transmit witness a false positive, and
clearing one needs root. A fresh address is a fresh test.

**What R-13 now guarantees.** A BASIC program can `BLOAD "meganet.bin",
P($42000)` and `SYS $42000`. A C program can embed the two arrays, DMA
them into place, and call `meganet_link_tx()`. Assembly can `jsr $033E`
with the mailboxes filled. The table is the ABI, and entries are appended,
never reordered. ARP is next, and it will be built behind this table
rather than moved behind it later.

### 5.4 R-19: interrupts, the memory map, and the STZ instruction (2026-09-04)

The debt was "the trampoline leaves interrupts disabled after a call,
because re-enabling them after the controller reset hung gopher's
machine, for reasons never made deterministic." It is now deterministic,
and the controller had nothing to do with it. Two separate facts, one of
which hid the other for most of a day.

**Fact one: the zero map removes the KERNAL.** A running llvm-mos program
has the map `A=$00 X=$E0 Y=$00 Z=$83` (read with the hypervisor's
`hyppo_get_mapping` trap, which reports six bytes in the order MAPLO
high, low, MAPHI high, low, then two megabyte bytes): `$2000-$7FFF`
identity-mapped to bank 0, and **`$E000-$FFFF` mapped to `$3E000`, the
C65 KERNAL in bank 3**. Gopher's trampoline, and this project's first,
"unmapped" on return with all four registers zero. That drops the C65
KERNAL from `$E000`; C64-style `$01` banking then shows the *C64* KERNAL
from bank 2 in its place (vectors `43 fe b8 e4 48 ff` instead of
`16 fa 4f fa 23 fa`, both measured). The next IRQ runs a C64 handler in
a C65 world and the machine hangs. That is the whole of gopher's finding.
Nothing else can put the C65 KERNAL back: `$D030` ROME and `$01` both
select bank 2, so only MAP reaches bank 3. Restoring the caller's map
verbatim is therefore required, not optional, if the caller is ever to
enable interrupts.

**Fact two, which made fact one look impossible.** Every attempt to
restore that map from the trampoline produced garbage on the *next* call,
and a long series of experiments produced a consistent but baffling
rule: "after a MAP that latches a non-zero MAPHI offset, the next MAP
misbehaves if C code ran in between." Reads of freshly written memory
came back as `83 e0 83 83` where `00 e0 00 00` had been written. The
monitor showed the *stores* had landed wrong, and the disassembly showed
why: llvm-mos emits **`STZ abs` (`$9C`) for every store of zero**. On the
65C02 that means "store zero". On the 45GS02 it means "store the Z
register" — and after `LDZ #$83; MAP`, Z is `$83`. Every zero the
compiler wrote afterwards was `$83`: the MAP arguments (hence the crash
on the next MAP), the trampoline's mailbox (hence `INIT rc 131` — `$83`
is 131), loop counters (hence "reads of zero" that were loops that never
ran), DMA job lists (hence DMA that "returned zero"). The only
configuration that ever worked was the one whose last `LDZ` was `#0`.
There is no CPU bug. There is a toolchain that targets a 65C02 running
on a CPU where one opcode means something else, and it only shows when
Z is non-zero — which no C program ever makes it, and every MAP sequence
does. `src/spike/spike_stz.c` reproduces it in twelve bytes of assembly.

**What was real and what was not.** Real: the six-byte order of the
hypervisor's map report; BASIC's default map; that MAPHI works from user
code exactly as the book describes (each of `Z=$14/$24/$44/$84` put its
block onto a pattern DMA'd into bank 4); that the megabyte form of MAP
(`X=$0F`/`Z=$0F`) works (`$80` mapped attic RAM, measured). Not real,
and now withdrawn: that the megabyte state was "dirty" (the zero reads
were STZ-corrupted loops); that the hypervisor trap "poisons" megabyte
state; that DMA could not read bank 3 under a user map (the job list was
STZ-corrupted); that stack accesses were translated (measured clean);
that a KERNAL copy in RAM cannot service interrupts (never fairly tested
— Z was `$83` at the time). Each of those was believed for an hour or
two on the strength of a measurement that STZ had falsified.

**The design that results.** The trampoline: `PHP; SEI`; map bank 4 over
`$2000-$7FFF` with MAPHI *unmapped* (the stack image's soft stack is at
`$8000`, and the caller's `$E000` offset would push it into ROM); call;
restore the caller's map from the mailbox, KERNAL included; **`LDZ #0`**;
`PLP; RTS`. The stack's entry: after capturing the caller's Z into its
mailbox, **`LDZ #0`** before its own C runs; `PHP; SEI` because BASIC's
`SYS` leaves the KERNAL IRQ running and it uses `$90-$BF`. The
demonstrator is kept as a regression check; the rule is in `LLM.md`.

**The measurement that closes it.** `src/spike/spike_abi.c` now hooks the
KERNAL's IRQ chain at `($0314)` with a counter, records `$D01A` (`$E1`:
raster IRQ enabled) and the vectors (`16 fa`: C65 KERNAL present), then
executes a bare `cli` and runs its loop. Over one read interval: IRQ
count **74 → 185**, loops +42,193, receive 15 → 34, transmit 5 → 12,
`I=0` in the loop, MAC correct, and the host's ARP table learned the
spare address. The KERNAL IRQ runs, the stack runs, both keep running.
(`$A0-$A2` did not advance; that is the C64 jiffy location, not the
C65's, and it was the wrong witness from the start.)

**Two things carried.** BASIC callers were reasoned about, not measured:
`SYS` keeps the KERNAL mapped and manages its own return, and the
stack's entry protects its zero page against BASIC's live IRQ, but no
BASIC program has yet called the table. And the `$C000-$CFFF`
interface-ROM question — whether any KERNAL path a caller might use
needs `$D030` ROMC — is unexamined; nothing in this project needs it.

### 5.5 Step 3: the MEGA65 answers ping (2026-09-04)

`src/net/mn_net.c` is the network layer: one interface, one IPv4
address, and `mn_net_poll()`, which pulls at most one frame from the
link and answers what it can — an ARP request for our address with a
reply to the asker, an ICMP echo request for our address with an echo
reply. Everything else is dropped after being counted. It is tested on
the host with the stub link (`test/test_net.c`: request for us, request
for another address, ping, ping for another address, junk, a frame
shorter than a header, and a link that refuses to transmit), 67 checks
green, before it went anywhere near hardware.

Behind the table it is four appended entries — `SET_IP` (`$42012`, a
12-byte block), `GET_IP` (`$42015`), `GET_STATS` (`$42018`, four
16-bit counters) and `SET_LOCAL_IP4` (`$4201B`, `SYS addr,a,b,c,d` for
BASIC) — and `POLL` (`$4200F`) now does the work; it returns 1 when a
frame was handled. `src/spike/spike_ping.c` is the whole client: load,
`INIT`, `SET_IP`, `cli`, then `POLL` forever. Satisfies R-2 and R-3.

**The witness is `ping` from the development host**, which needs no
root and cannot be faked by the stack: the host resolves the address
by ARP (the stack answered), then the echo requests come back. First
run: 5 of 5 answered, 2 ARP replies, 5 echo replies — and a round-trip
of **1,011 ms**, "4 packets out of wait time", each reply arriving
exactly when the next request did.

**Finding: rotate the receive ring before reading it.** The driver's
`rx` checked the "frame waiting" bit, read the visible buffer, then
rotated. That serves the *previous* frame: the CPU-visible buffer is the
one already handled, and the flag means a newer one is waiting behind
it. Every ping was answered one ping late, which at a one-second
interval is a one-second round trip — a symptom that would have been
invisible in the earlier spikes, where "the last frame received" was
always plausible. Rotating first (`$01` then `$03` to `CTRL2`, then
read) fixed it: **min 7.2 ms, 50 of 50 at a 50 ms interval, 0% loss,
59 echo replies, 0 transmit failures**. the earlier stack reads before rotating
but pulls frames in a burst while the flag stays set, which hides the
lag for it. The step 2 finding about the FCS stands unchanged.

**Carried.** Round-trip jitter is real: avg 27 ms, max 160 ms at the
50 ms interval, and a 577 ms outlier in the first batch after the PHY
came up. The polling loop is a full trampoline call per poll (map, zero
page save and restore, two DMAs) whether or not a frame is waiting, and
the echo reply recomputes two checksums in C. Worth measuring once
there is a UDP path to compare against; not worth optimising blind.

### 5.6 Step 4, first half: UDP and DHCP on hardware (2026-09-04)

Host first, as the rule says. UDP with the pseudo-header checksum; a
four-entry ARP cache learned from replies and from requests for our
address; a send path that resolves the next hop (the destination on our
subnet or a broadcast, else the gateway), sends an ARP request and
reports `MN_SEND_PENDING` when it does not know the MAC, and never
blocks; four UDP sockets, each a port with a one-datagram mailbox. Then
the DHCP client as a state machine driven from poll: DISCOVER, OFFER,
REQUEST, ACK, NAK back to the start, retransmit after four seconds up
to five times, and give up. All against synthetic frames on the stub
link, 151 checks green, before hardware. One network-layer change came
with DHCP: while our address is 0.0.0.0, the IPv4 layer accepts anything
that reached us by MAC, because a server that ignores the broadcast flag
unicasts the offer to the address it is about to give us.

**On hardware, after two fixes:** `DHCP_START` from a twenty-line client,
BOUND after 112 polls, lease `192.168.1.252/24`, gateway and DNS
`192.168.1.1` — the same values the development Mac holds for this LAN
— and the leased address answers ping, 5 of 5. Satisfies R-4 and R-5.

**Fix one: the zero page must be swapped, not restored.** The bigger
core needed 73 bytes of `.zp`, so the stack's zero page grew from
`$90-$BF` to `$90-$FF` — and every call after the first crashed. The
prologue saved the caller's zero page on entry and the epilogue put it
back on exit; that *wiped the stack's own zero-page statics* between
calls, and with 73 bytes the compiler had put persistent state there:
the netif's function pointers, the DHCP machine, the tick counter. The
second call jumped through garbage. With seven bytes of `.zp` this had
held only temporaries, which is why the ABI ever worked. Now there are
two save areas: the caller's copy goes to safety on entry and ours comes
back; the reverse on exit. `.zp.bss` is zeroed at `INIT` like `.bss`.

**Fix two: zero page is always bank 0.** The image believes its memory
starts at `$40000`, and `MN_PHYS()` added that to every address it
handed to DMA. Zero page is never remapped: a two-byte static the
compiler placed at `$B4` is physically at `$000B4`, not `$400B4`, and a
DMA told the latter reads and writes the wrong bank silently. `MN_PHYS()`
now leaves addresses below `$100` alone, and the DMA job list itself —
whose bank goes to the controller as the image's — is pinned out of
zero page with a named section.

**Carried.** Lease renewal is not implemented: the lease time is
recorded and nothing happens when it runs out. Fine for a day of
development; not fine for a BBS. The zero-page swap costs about 450
cycles per call on top of what was there; it is the price of letting
the compiler use zero page, and it was not measured against the
alternative (forbidding it).

### 5.7 Step 4, second half: DNS, NTP, and the clock set from the internet (2026-09-04)

The host side first: the DNS resolver (A records, CNAMEs and compression
pointers walked, retries) and the SNTP client (mode-3 request, transmit
timestamp, calendar arithmetic checked at leap day 2024, the 400-year
rule at 2000, and the second that turns 1999 into 2000) — 184 checks
green. Behind the table: `SET_DNS`, `DNS_START`/`STATE`/`RESULT` (the
address comes back in A/X/Y/Z, so BASIC can `RREG` it), `NTP_START`/
`STATE`/`RESULT` (a block with the caller's UTC offset in and the date
out), and `SET_RTC`, which writes BCD to `$FFD7110-$FFD7116` with the
24-hour bit set. `src/spike/spike_time.c` chains them: DHCP, resolve
`pool.ntp.org`, SNTP, set the clock.

**Result.** Bound at loop 136; `pool.ntp.org` → `23.150.40.242`, the
same first answer `dig` returned on the host; NTP seconds 3997530584
(2026-09-04 17:09:44 UTC); local time with the host's offset
13:09:44 EDT; RTC read back `2026-09-04 13:09:42`, 24-hour mode. The
MEGA65 set its own clock from the internet. Satisfies R-6 and R-7 and
completes step 4. (The RTC reports day-of-week 4 for that Friday: its
convention is Monday = 0, which `SET_RTC` does not yet translate.)

*Corrected 2026-09-14.* The Monday = 0 conclusion was wrong. The clock
holds whatever weekday is written: `../mega-ntp` REQUIREMENTS.md 5.1
wrote 1 for a Monday through mega65-libc's `setrtc()` on an R6 and read
1 back a minute later. `NTP_RESULT` gives 0 = Sunday (`mn_ntp.c`;
`test_dnsntp.c` expects 5 for a Friday), which is also mega65-libc's
convention, so there is nothing to translate. The 4 read here is most
likely the clock before the write: `spike_time.c` reads `$FFD7110` right
after `SET_RTC`, but those registers mirror the I2C chip, and that same
read was two seconds behind (13:09:42 against 13:09:44). This was not
re-measured with `SET_RTC` itself.

**Three findings on the way, each an afternoon.** The first two are the
image outgrowing its assumptions; the third is the network.

*The soft stack ran out of room.* At 13.8 KB of image and 10 KB of
`.bss` the 24 KB window was full: `.noinit` — the mailboxes and the
zero-page swap buffers — sat 79 bytes below the 512-byte soft stack, and
DHCP's call chain overflowed into it. The known-good DHCP client failed
against the new image while the ping client, with a shallower call
chain, still worked, which is what pointed at the stack. The window is
now 40 KB (`$2000-$BFFF`) with a 1 KB soft stack; the decisions table
has the mapping.

*A constant moved into zero page and vanished.* With the larger image
the compiler placed the DHCP magic cookie — a four-byte `static const`
— in `.zp.data`, initialised zero-page data whose bytes live in the
image and are copied into zero page by crt0. This image has no crt0, and
`INIT` copied `.data` (in place) and zeroed `.zp.bss` but knew nothing of
`.zp.data`. The cookie read as zero, every DISCOVER went out without it,
and the router — correctly — answered as if to a BOOTP client: a reply
with the lease address but no DHCP message type, which the state
machine cannot accept. `INIT` now copies `.zp.data`. The general lesson
is the decision-table entry: an image without a runtime must do all
three of the runtime's jobs, not the two it happened to need so far.

*The router overloads its options.* Dumping the reply also showed option
52 (overload) with the vendor-specific option 125 continued in the
`file` field. The parser now honours overload for both `file` and
`sname`, with a host test that puts the message type there. This was
not the cause of the failure — the cookie was — but it would have been
the next one.

**A lesson about diagnostics.** The first dump of the router's reply
looked scrambled in its header and `file` field, and an hour went into
that. The per-second samples the diagnostic wrote at `$1420 + 34n` had
grown straight through the dump at `$1500`. The scramble was mine.
Diagnostic buffers get their own page.

**ABI note.** `DHCP_LAST_MSG` (`$4203F`) copies the DHCP module's
message buffer to the caller; it is a diagnostic and is documented as
one, but the table is append-only, so it stays.

**Carried.** ~~`SET_RTC` should translate weekday to the RTC's Monday = 0.~~
Withdrawn 2026-09-14: the weekday is 0 = Sunday throughout (see the
correction above). NTP's 2036 rollover is ignored. The stack's image is now 14.3 KB; a
client that embeds it as an array is itself 15 KB, which is fine today
and will not be once TCP is in — gopher ended up streaming its stack
from disk for the same reason.

### 5.8 Step 5: TCP, one connection, on hardware (2026-09-04)

`src/net/mn_tcp.c`: a 4 KB receive ring the application drains at its
own pace, a 1 KB send buffer with one segment in flight (stop-and-wait),
and the state machine driven from poll — SYN_SENT, ESTABLISHED, both
close orders through FIN_WAIT/CLOSE_WAIT/LAST_ACK/CLOSING to a two-second
TIME_WAIT, RST and refusal, retransmission with an exponential RTO from
one second and a limit of five, abort with RST. The network layer gained
a two-step raw IPv4 transmit (resolve the next hop, then build the
segment in place) and an input hook. The synthetic peer in
`test/test_tcp.c` exercises every path, including the one gopher found
the hard way: the stub link's transmit queue had to grow past eight
frames to hold a conversation. 252 checks green.

**The three gopher lessons are design, not comments.** The ring stays
readable after the peer's FIN, and the flags report EOF separately from
the state, so a client drains and *then* closes. A request goes out as
one segment because the sender takes whatever is queued up to the
peer's MSS. And the window is advertised at a 2 KB cap (the 45E100's
receive ring is four frames deep) with an update sent when its **right
edge** has moved 256 bytes or the window had closed — the size-based
test that stalled the earlier stack at exactly 1024 bytes cannot recur.

**Behind the table:** `TCP_CONNECT` (a 6-byte block), `TCP_STATE` (state
in A, flags in X, bytes available in Y/Z), `TCP_SEND` and `TCP_RECV`
(28-bit pointer blocks, moved by DMA in frame-sized chunks), `TCP_CLOSE`,
`TCP_ABORT`. `src/spike/spike_tcp.c` is the client;
`tools/tcp_test_server.py` the LAN peer.

**Results, first run.** LAN: connected from `192.168.1.252:49403`, sent
`/\r\n`, received **5,000 bytes, sum16 47580 — exactly the expected
value** — in 26 frames (~0.5 s), across a 4 KB ring, in 7 receive calls;
EOF flagged, closed cleanly, the server saw its close complete in
0.07 s. Internet: `gopherpedia.com` resolved to `178.128.182.33`, and
its root menu arrived as **3,513 bytes, the same count `nc` gets from the
Mac**, in ~0.5 s. Satisfies R-8.

**Carried.** Stop-and-wait is fine for requests and adequate for gopher
responses (the peer fills our window regardless); it will matter for
uploads and for the BBS. No delayed ACK: every data segment is
acknowledged, which is more frames than necessary and also more robust
on a polled stack. The image is 21.8 KB and a client embedding it is
24 KB; the gopher port will need to load `meganet.bin` from its `.d81`
as it loads `ETHBIN` today — same bank, same address.

### 5.9 The parity test: the gopher client on mega-net (2026-09-04)

The port lives in `../mega-gopher` on the branch `mega-net`; `main` there is
untouched until it has been seen to work. `gopher_net.c` was rewritten
against `meganet.h` behind the same `gopher_net.h` the rest of the client
already used — error strings, the dotted-quad parser and the DNS cache
kept, the waits paced on `$D7FA` as before. `gopher_boot.c` loads
`MEGANET` (21,885 bytes) from the `.d81` into `$42000` where `ETHBIN`
went, and the generated trampoline to `$0334`; `build-native.sh` takes
the header, the trampoline source and the image from `../mega-net`
(building it if need be), so the client carries no copy of the stack.
The gopher client's old far-call header, trampoline, payload and generator are
gone.

**The finding: the I/O personality.** The first run stalled at "Bringing
up network...". Eight diagnostic bytes in the driver (`mn_eth_dbg`, in
`.bss`) showed the stack in DHCP SELECTING with zero tries and one
transmit failure, seeing no frame waiting — while `--memsave` read the
controller directly and found one. The stack's driver and the host were
reading different registers at the same address: the client's screen and
disk code select their own I/O personality, and the 45E100 is only at
`$D6E0` under the MEGA65 one. The spikes never left that personality, so
the stack never had to assert it; the earlier stack did, on every call. Now
`mn_prologue` writes `$47`/`$53` to `$D02F` on every entry, before it
touches anything. The same stall exposed a second defect: a send the
link refused did not count as a try in DHCP, DNS or NTP, so a link that
kept refusing would wait forever instead of reaching FAILED.

**Results.** `gopherpedia.com`: the root menu renders, 64 items.
`sdf.org`: 1,338 bytes, complete — the R-8b case, where the earlier stack stopped at
exactly 1,024. `test_gopher_server.py` from gopher's `tools/`, whose 5,635
bytes stalled the earlier stack: **"Item 88 of 88"**, and the server's log shows
every byte handed to its kernel with no `SEND STALLED` — the stack kept
acknowledging while the client drained one byte at a time. The
network-edge cases from `adversarial_server.py`: `/latemenu` (ten
seconds of silence, then a menu) rendered; `/slowdrip` (one byte every
half second for thirty seconds) arrived whole; `/stall` (accepts, sends
nothing) produced "Connected, but no reply to the request", the intended
message. Screens in `docs/parity/`. R-8 is satisfied in full; step 5 is
done.

One note for whoever repeats this: a selector with a leading slash is
typed as `host:port//latemenu` — `host:port/latemenu` is the selector
`latemenu` by the client's URL grammar, and the server rightly answers
"Unknown selector". That is the client, not the stack.

**Carried.** Unchanged from 5.8, plus the two the user has deferred:
BASIC callers and the Windows/Linux build paths are still untested.
`mn_eth_dbg` stays in the driver; it costs eight bytes and paid for
itself on the first day.

### 5.10 BASIC callers: the third language, proven (2026-09-04)

`tools/basic/mnbasic.bas`, on a disk built by `tools/basic_d81.py`:
`BLOAD` the trampoline and the image, capture the map, then VERSION,
INIT, DHCP_START, a POLL/DHCP_STATE loop, GET_IP into a bank-0 block,
and thirty seconds more of polling for the Mac to ping. **It works**:
`VERSION 0.1 MN`, `INIT 0`, bound in under a second, `IP 192.168.1.252`,
`GW 192.168.1.1`, and 5 of 5 pings answered (40-155 ms: the poll loop
is interpreted BASIC). `docs/parity/basic.png`. Four things had to be
found first, two of them defects in the stack.

**$0334 is the KERNAL's far-call code.** A dump of the $0300 page after
a fresh boot, before anything was loaded, shows $0334-$03DF full of
machine code: the KERNAL's own map-and-JSR helpers, which live in block
0 for the same reason our trampoline does. `BLOAD`ing the trampoline
over them put the machine in an endless BREAK loop at the next KERNAL
call. C clients never saw it because they own the machine. The MEGA65
Book's memory map gives `$1600-$1EFF` as "free for program use" under
BASIC 65 and `$0002-$15FF` to the KERNAL, so the reference base moved
to `$1600` — and the capture report's page, `$1500` until now, to the
page after the trampoline's.

**One page, reserved.** The gopher client's F011 sector buffer was at
`$1600`, and for an afternoon the base was made a build-time choice so
that client could keep `$0334`. That was the wrong direction for a
dependency to bend: a generic stack publishes one address and its
callers adapt, and "the base depends on your build" is not something a
BASIC programmer should ever read. It was reverted the same day. **The
stack reserves `$1600-$16FF` of bank 0 in every client.** `meganet.h`
derives every mailbox address from `MEGANET_TR`, which is not a knob.
The capture entry — a diagnostic that wrote six bytes into the next
page — went with it, so the reservation has no footnote and the call
entry is at `$160E`. The gopher client's buffer moved into its own
`.bss`, where there has been room since the image was streamed from
disk. Re-verified with the reserved page: the BASIC program as above,
and the ported client booting, bringing up the network and rendering
the gopherpedia.com root menu.

**The SYS map is the default restore map.** With the program loaded
from disk (see below), the capture entry, while it existed, reported
`E0 00 83 00 00 00`:
`A=$00 X=$E0 Y=$00 Z=$83`, exactly the map measured for a running
llvm-mos program in 5.4. A BASIC caller need not capture or poke
anything; and the KERNAL's SYS wrapper re-establishes BASIC's own map
on return in any case. `$D030` reads `$64`, its documented power-on
value, and the chipset reference settles the other worry of 5.9's
follow-up list: "The MAP register overrides all other banking
mechanisms", so the ROM bits cannot hide the window (CROM9 is "not
implemented").

**The DMA list registers belong to the caller.** The first call that
runs a DMA hung BASIC — not the call itself, which returned, but
BASIC's next screen scroll. BASIC 65 sets `$D701/$D702` once and
triggers each of its own jobs by writing `$D700` alone (as the chipset
reference suggests); `mn_dma_copy` had left the stack's list address,
bank 4, behind, so BASIC's scroll fetched a job list from bank 4.
`mn_dma_copy` now saves `$D701`, `$D702` and `$D704` and restores them
in that order (writing `$D702` clears `$D704`). Also seen: `$D703` is
`$01` under BASIC 65 — the controller in F018B mode — which the driver's
F018B-form job happens to suit either way; pinning the format with the
`$0B` option byte is carried. The trampoline also now zeroes both
megabyte registers with the `$0F` form before its MAP. That was added
chasing a false lead (see below) and kept: it is four instructions, and
a plain MAP inherits whatever megabyte a caller last latched.

**Method: do not type programs in.** `m65 -T` drops lines — reliably
the line after any line that wraps past 80 columns — and a program with
lines 50, 70, 90, 120, 160, 230 and 1010 missing produced a day's worth
of convincing wrong answers: a `GOSUB 1000` into a missing 1010 ends a
program silently, and a captured map from a run whose capture line was
dropped is whatever was in memory. The program now travels on the disk,
tokenized by `petcat -w65`, and is started with `RUN "MNBASIC"`. One
petcat trap: a hex literal ending in `E` (`$160E`) is tokenized as
`$160` followed by the `VAL` token; write it in decimal. And `RREG`
after `SYS` returns the trampoline's own final registers (the map
bytes), not the stack's results: `PEEK` the mailbox, `$1606-$1609`. One more petcat trap, found twice: `$160E` ends in `E`
too; the program writes the call address as `5646`.

**What R-13 means now.** The trampoline layout, in the reserved page:

| | | |
|---|---|---|
| `$1600-$1601` | entry address, low/high (`$2000+3N`) | poke |
| `$1602-$1605` | A, X, Y, Z in | poke |
| `$1606-$1609` | A, X, Y, Z out | peek |
| `$160A-$160D` | map restored after the call (default: the SYS map) | leave |
| `$160E` | the call | `SYS` |

**Carried.** TCP from BASIC (pointer blocks with bank-0 buffers, the
same path GET_IP took here) is untested. The `$0B` option byte. R-20 is
the rule that came out of this; the reservation is now part of R-13.

### 5.11 Step 6: socket sets, listening sockets, and the MEGA65 as a server (2026-09-04)

**The design.** `mn_tcp` is now a set of eight sockets, each an active
or passive connection, dispatched by four-tuple; a SYN that matches no
connection goes to a socket in LISTEN on that port, several of which may
listen on one port (a multi-line BBS is eight `TCP_LISTEN`s); a segment
nobody owns is answered with RST, so a caller to a port with no listener
learns it at once. A listening socket goes ESTABLISHED when the
handshake completes and CLOSED when that connection ends; listen again
to take the next caller. Half-open peers time out back to LISTEN. The
rings — 4 KB receive, 1 KB send, now a ring too — live outside the
window in a pool reached by DMA through `mn_xmem.h`, so a socket costs
the image 75 bytes of state instead of five kilobytes; the host tests
back the pool with an array. Every TCP entry takes the socket index in
Z, which every existing caller already passes as zero, so the gopher
client did not change; `TCP_LISTEN`, `TCP_PEER`, `TCP_INFO`, and
`UDP_OPEN/CLOSE/SEND/RECV` for applications are appended. Host suite:
293 checks.

**Where the pool goes, and the memory map.** Bank 5 was the obvious
place and the wrong one: BASIC 65 uses banks 4 and 5 for its bitmap
graphics, and a C program wants them as plain RAM. INIT probes attic RAM
with two patterns through the same DMA path the pool uses and puts the
pool in its first 40 KB; a machine without attic RAM gets bank 5, and
`TCP_INFO` says which. The whole footprint, for anyone planning around
the stack:

| Where | What | 
|---|---|
| bank 0 `$1600-$16FF` | the trampoline (5.10) |
| bank 4 `$42000-$4BFFF` | the image and its soft stack |
| bank 4 `$4FFF0-$4FFFF` | the interrupt vectors in force during a call (below) |
| attic `$8000000-$8009FFF` (else bank 5 `$50000-$59FFF`) | the socket pool |
| `$D701/$D702/$D704` | saved and restored around every DMA (R-20) |

Everything else — bank 1, the rest of bank 4, bank 5 when attic RAM
exists, all of bank 0 but one page — is the application's.

**The image had to go on a diet.** The new code is 8 KB of text, and
the window overflowed by 3.8 KB. In order of yield: `emit`, 1.4 KB, had
been inlined into every caller — the large TCP helpers are now
`MN_NOINLINE`; the ABI's private staging frame went, the entries that
need one borrow the network layer's receive buffer, idle between polls;
the driver's 1.5 KB staging frame became a 60-byte pad, long frames go
to the controller straight from the network layer's buffer; pool
addresses are a 16-bit segment and offset so ring arithmetic stays
16-bit; UDP sockets are four again. The image is 30.6 KB and `.bss` ends
at `$B553`, leaving about 1.4 KB below the soft stack. The next feature
that needs more moves something into the pool.

**Acceptance.** `src/spike/spike_server.c` listens on two lines of port
6400 and echoes, with a UDP echo on 6401; `tools/server_test.py` drives
it from the Mac: two callers connected and echoed (14, 768 and 9
bytes, intact), a third caller **refused by RST in one second** while
both lines were busy, a line closed and re-listened and took a fourth
caller, and a UDP datagram came back from the MEGA65. All of it while
the client side of the stack was untouched: gopherpedia still renders.

**The finding: an NMI during a call.** The BASIC program from 5.10
stalled in DHCP_START about half the time on the new image, and only
there — the C clients never did. It took the afternoon. The state was
always a BRK loop at `$FA23` with the window map still in force, and
the stack page showing nothing but the loop's own pushes. Breakpoints
could not catch it because a halt anywhere near the call, even in
BASIC's own IRQ handler before it, made it go away; so did an extra
DMA. Breadcrumbs (`mn_crumb.h`: a byte per stage, a status byte with
it) placed the crash after the transmit trigger *and* later in the
return path — two places, so not a bad jump. The proof came from
giving the window its own vectors: an RTI stub at the top of bank 4
that counts entries and records the pushed frame. It caught exactly
one event per crashing run, with the frame's status byte showing **B
clear and I set** — a hardware interrupt taken with interrupts
masked, which is an NMI — at ordinary instructions of the image (the
byte at the recorded address is the ELF's). Where an NMI comes from
under BASIC 65 is not yet known (the C clients count none); the stack
cannot prevent it, so it owns the vectors instead: the trampoline maps
`$E000-$FFFF` into the window as well (MAPHI `$B4`), INIT writes the
stub and the vectors at `$4FFF0`, and `GET_STATS` reports the count.
Nine runs since, nine leases, with one interrupt caught in about every
other run. The lesson, for LLM.md: the caller's vectors are not
valid while its KERNAL is unmapped; a banked image must bring its own.

**Tools that paid for themselves.** `tools/mon.py` talks to the serial
monitor directly, without pyserial (termios plus the macOS baud ioctl):
`r` for the registers including MAPL/MAPH, `m`/`M` for memory by 28-bit
address, `b<addr>` for a breakpoint (the reported PC is *after* the
instruction there), `t1`/`t0` to halt and resume, a blank line to
step. `m65 -B` and `-H` report nothing; this does. And the crumb macro.

**Carried.** The `$0B` DMA option. TCP from BASIC: closed by the guide's
recipe program, which fetched a gopher menu and served an echo line from
BASIC 65 (docs/GUIDE-BASIC.md, tools/basic/guide.bas). The interrupt's
source: found, it is the ethernet controller's (5.16). Stop-and-wait:
gone (5.17). The pool's alignment: checked (mn_tcp_init). Lease renewal:
verified against the router (5.14). The ABI reports 0.2.

### 5.12 Background work while the machine was busy: lease renewal, the NTP era, and the window (2026-09-04)

Done without hardware, in the host harness; the parts that touch the
machine are marked unverified until it is free.

**DHCP lease renewal** (`mn_dhcp.c`). A server that runs for hours needs
it: a lease that silently expires leaves the machine with an address the
router has given away. While BOUND the module now keeps a clock and, at
half the lease (T1), sends a unicast REQUEST to the server that granted
it — ciaddr filled, no requested-address or server-id options, a
unicast reply asked for, as RFC 2131 4.3.2 has it — retrying once a
minute; at seven eighths (T2) it rebinds by broadcast; when the lease
runs out, or a NAK arrives, it drops the address to 0.0.0.0 and starts
over with DISCOVER. The application keeps seeing BOUND until the
address is actually gone; `DHCP_STATE` now also returns the phase in X
and the minutes of lease left in Y/Z, and `meganet_dhcp_status()` reads
them. The clock runs in **minutes, 16 bits**: the first version counted
32-bit seconds and cost 1.3 KB of image on a 6502; a lease of 45 days
or more is treated as one without expiry, and no router hands those out
anyway. The tick feeds seconds, the seconds feed minutes, so the module
never loses time if polled at least once every 21 minutes. BOOTP (no
lease) and infinite leases are never renewed. Forty-two new checks in
`test_dhcp.c` walk a one-hour lease through renewal, silence to
rebinding and expiry, a NAK while renewing, and an infinite lease,
across the 16-bit tick's wrap. **Unverified on hardware**: a renewal
against the real router takes half a lease.

**The NTP era.** NTP's 32-bit seconds wrap on 2036-02-07. A value below
the 1968 pivot is now read as era 1, as RFC 4330 suggests: 2^32 seconds
added, which is 49710 days and 23296 seconds. Tested against dates
computed independently.

**The window, again.** The renewal code and the era arithmetic did not
fit: 1.3 KB over. The minute clock took 800 bytes back; the DHCP and DNS
message buffers — 576 bytes each, used only inside the poll that builds
or parses a message — now use the network layer's receive buffer, idle
between polls. `DHCP_LAST_MSG` therefore shows the last frame received
rather than the last DHCP message; it was a diagnostic from 5.7 and is
kept as one. The image is 32.8 KB, `.bss` ends at `$B983`, and about
400 bytes remain below the soft stack. Measured but not adopted: `-Oz`
saves 1.65 KB of window and needs 13 zero-page bytes the swapped region
does not have; widening the region past `$90` would touch what BASIC 65
keeps below it. The next feature that needs room takes one of: a
socket's worth of state into the pool, the diagnostic entries out, or
the zero page question answered.

**R-17, reviewed.** `build.py` was written for all three hosts and reads
that way: tool discovery by `LLVM_MOS_DIR`, home, `/opt` and PATH,
MSVC as a fallback with its own flags, `.exe` names on Windows.
`tools/basic_d81.py` finds VICE's tools on PATH or by `C1541`/`PETCAT`;
`tools/device.py` knows each platform's spelling of `m65`;
`tools/mon.py` now runs on Windows through pyserial when termios is
absent, with `MEGA65_PORT=COMn`. The gopher client's own build is a
shell script and stays macOS/Linux, which is that project's choice.
None of this has been *run* on Windows or Linux; that item stays carried
until someone has such a machine on the same LAN as a MEGA65.

### 5.13 Attic RAM is not coherent under DMA; the pool is bank 5 (2026-09-04)

Reported from use, on the gopher 0.2 disk: a 4,077-byte text document
from gopher.floodgap.com rendered with whole stretches replaced by
*earlier* bytes of the same document ("forced to crack down. We g" ran
on into "Y 1, 2024:" from 170 bytes before), 56 lines shown of 145.
The stack's own checksum spike, 5,000 bytes through the same ring in
1 KB reads, passed. The client reads 256 bytes at a time while segments
keep arriving, so the ring in attic RAM is written by DMA and read by
DMA a few hundred bytes apart, over and over. With the pool forced into
bank 5 and nothing else changed the document arrived whole, 143 lines.
So DMA reads of attic RAM can return stale data shortly after DMA
writes nearby; the MEGA65 developer guide's own unit-test example is an
"attic ram cache test" that enables the cache at `$BFFFFF2` and checks
for exactly this kind of stale read, so the platform knows the
mechanism. The stack cannot ship on it: **the socket pool is bank 5,
`$50000-$59FFF`**, and attic RAM is a compile-time option
(`-DMN_POOL_ATTIC=1`) until the cache's rules are understood. The
memory tables in the README and `docs/ABI.md` say bank 5. Bank 5 is
where BASIC 65 draws its bitmap graphics, which is the one program
class that now has to know about the stack's pool.

**Carried.** The attic RAM cache: whether `$BFFFFF2` can be set so DMA
sees its own writes, or whether reads must be retried. Until then, an
application that wants attic RAM has all of it.

### 5.14 Lease renewal against the router (2026-09-04)

`src/spike/spike_dhcprenew.c` binds, then sets the module's lease clock
to a minute before T1, T2 and expiry in turn by DMA into the DHCP state
(`build.py` puts the state's mapped address in the generated header;
the field offsets come from `mn_dhcp.h` under the same compiler), and
records what happens. Three minutes instead of ten hours. The router
gives a 1,226-minute lease. **Renewal**: the unicast REQUEST went out
when the minute ticked over and the router's ACK had the module bound
again within a frame, lease full. **Rebinding**: the broadcast REQUEST
was also ACKed, in 18 frames. **Expiry**: the address was dropped
(GET_IP read 0.0.0.0) and a fresh DISCOVER/OFFER/REQUEST/ACK re-bound
192.168.1.252 in 18 frames. Six messages received in all, one try per
transmission. 5.12's carried item is closed.

One thing seen on the way: this router renews a lease to the *time
left*, not to a fresh term, so T1 and T2 move earlier with every
renewal; the module recomputes them from each ACK, which is why the
spike's second and third nudges took effect at once.

### 5.15 Attic RAM, two more tries (2026-09-04)

The developer guide's unit-test example writes `$E0` to `$BFFFFF2` to
"enable attic ram cache"; the register reads `$60` after boot. Two
images with the pool in attic RAM, one clearing the register at INIT
and one writing `$E0`, both against the floodgap document that exposed
the splice (5.13). With `$00`: the same splice, 54 lines. With `$E0`: the
client never got a lease at all — "Bringing up network..." and nothing
after it, so whatever bit 7 does, it reaches further than the attic
cache. Neither is the answer, and the pool stays in bank 5. The hook remains (`-DMN_POOL_ATTIC=1 -DMN_ATTIC_CACHE_CTRL=..`)
for whoever learns what the register's bits mean. Attic RAM is the
application's.

### 5.16 The interrupt during calls is the ethernet controller's; attic RAM is not DMA memory (2026-09-04)

Two of the carried items, measured.

**The interrupt.** A diagnostic build gives the window two separate
vector stubs, one for NMI and one for IRQ/BRK, each recording CIA2,
CIA1 and the 45E100's status byte when it is entered. Fourteen runs of
the BASIC program: every event, five of fourteen, came through the
**IRQ vector**, never the NMI's, with the I flag set, both CIAs' IR
bits clear, and `$D6E1` showing TXQ — the ethernet transmit-done
status — set while TXQEN was clear. So on this core the controller's
transmit (and presumably receive) events reach the CPU's IRQ vector
past both the enable bits and the I flag, some of the time, tens of
microseconds after a transmit; a spike with a six-second pause before
its first call ruled the serial tool's keystroke injection out. The
stack cannot switch that off, so owning the vectors during a call
(5.11) is the mitigation and stays; between calls the caller's own
handler takes the spurious interrupt, which the KERNAL's and a C
program's both tolerate. `GET_STATS` counts them. Item closed as far as
software can close it; the observation belongs upstream.

**Attic RAM.** `src/spike/spike_attic.c` writes and reads attic RAM by
DMA in three patterns — 4 KB in one go, the ring's interleaved 256-byte
chunks, and single blocks of 1 to 1024 bytes — for seven values of the
cache register `$BFFFFF2`. Every pattern fails at every value: half the
bytes wrong in the plain 4 KB case, all sizes down to a single byte in
the block case. The same spike against bank 5 is clean everywhere. So
DMA to attic RAM is not usable on this machine and core; CPU access,
which the gopher client's downloads use, is unaffected. The pool stays
in bank 5, and the memory tables say so without qualification.
Closed by measurement.

### 5.17 Hardening: the window diet, and segments in flight (2026-09-04)

**The diet.** The four UDP mailboxes, 576 bytes each, moved into the
pool behind the TCP rings; `mn_net_set_pool` places them, the host
default sits after eight 7 KB sockets. The NTP message buffer and the
ABI's tick statics are pinned to `.bss` (`MN_BSS`), which is what let
the image build with **`-Oz`**: at `-Os` it had wanted 13 more bytes
of zero page than the swapped region holds, and those four objects were
the difference. The image is 32.2 KB, `.bss` ends at `$AE4D`, and
**3.3 KB** remain below the soft stack — up from 400 bytes. `-Oz` cost
nothing measurable: the 16 KB echo runs at 212 KB/s against 190 at
`-Os`.

**Segments in flight.** The send ring is 3 KB, and the sender keeps a
congestion window of segments in flight: two at connection, one more
per acknowledgement up to four, back to one on a retransmission, and
never more than the peer's window. Retransmission resends the oldest
segment only; the timer covers the oldest byte in flight and restarts
on every new acknowledgement. Twenty new checks in `test_tcp.c`: two
segments out at once and a third held until an ACK, the window growing,
the oldest segment alone resent after the RTO, and the peer's window as
the cap. On hardware the 16 KB echo, fed 4 KB at a time so the MEGA65
has several segments to send back per round trip, came back intact at
212 KB/s both ways; the 5,000-byte checksum download, the gopher
document and the BASIC program are unchanged.

**The pool now.** Bank 5: eight TCP sockets of 4 KB receive and 3 KB
send at `$50000-$5DFFF`, four UDP mailboxes at `$5E000-$5E8FF`. 

**Carried.** Congestion control is the minimum that is safe on a LAN
and polite on the internet; there is no slow start beyond the two
segment opening, and no fast retransmit.

### 5.18 A C caller must own its vectors (2026-09-06)

The SSH client (ssh REQUIREMENTS.md 5.8) stopped on half of all boots
at DHCP start, the CPU found in this stack's stray-event stub with the
decimal flag set, or in the KERNAL's handler with the stack filling,
or at address zero in this stack's map, or in the client's own error
path after a disk load came up short. One fault: 5.16's event, tens
of microseconds after a transmit, arriving between calls, when the
client's vectors were the C65 KERNAL's (`$E000` from bank 3 by the
MAP, and again by HIRAM in `$01`). The KERNAL's handler, entered from
the client's context, runs on the client's zero page and stack, and
its own MAP brings the ROM in under the program, so the fetch after
its return comes from ROM. Which transmit did it was chance.

The fix is the caller's: `meganet_own_vectors()` in the kit
(`src/abi/meganet_vectors.c`, declared in `meganet.h`), a stub in bank
0 under `$E000` recording the first interrupted PC, the vectors on it,
nothing mapped above `$8000`, HIRAM off, the trampoline told the map.
Called first thing, before the trampoline copy (which carries the
KERNAL map, so the map is told again after), and before INIT, which
resets the controller and raised an event of its own. The SSH client
booted twenty-two times running, then thirty more, with it. A BASIC
caller is unaffected: the KERNAL's handler is right in BASIC's
context, which is why the 5.16 runs saw events and no hangs.

After the fix, no event was recorded anywhere across a boot, a DHCP
exchange, a handshake and a session: neither in the client's stub nor
in this stack's counter (`window_irqs` read 0). The events are rare in
normal running; the storms were the KERNAL's handler's doing.

**Carried.** Whether the event can be consumed at the source: a call
that transmitted could wait for transmit-done and read the status
before returning, so the event lands inside the window the stub
already covers. 5.16 measured only that the enable bit does not stop
it. Not measurable in a session now that events are this rare; the
5.16 diagnostic build over many runs would be the way.
