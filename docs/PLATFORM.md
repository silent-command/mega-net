# The MEGA65, as these programs found it

For whoever works on mega-net, the gopher, FTP, SSH, NTP or Gemini
client next, human or model. Everything here was measured on the
machine and cost time to learn; each item names the finding that
records the evidence (`REQUIREMENTS.md` section 5 of the repository
given). Read this before touching hardware. The clients keep
`../mega-net` as a sibling checkout, so this file is reachable from all
of them.

## Read in this order

1. The repository's own notes: what it is, the rules, the commands.
2. This file: the machine, the toolchain, the tools, the traps.
3. `REQUIREMENTS.md` of the repository: section 2 decisions (do not
   reopen them), section 4 the plan, section 5 the findings. The README
   is the summary for users; the findings are the record.

## The machine

A 45GS02 at 40 MHz, a 6502 descendant with a Z register, 32-bit "Q"
operations (A, X, Y and Z as one register), a hardware multiplier at
`$D770-$D77F`, DMA (DMAgic, F018B form), 384 KB of chip RAM in banks
0 to 5, optional attic RAM at `$8000000`, a 45E100 Ethernet controller,
and an 80-column VIC-IV text mode of 25 or 50 rows. A native program
loads at `$2001` and owns the machine until it resets it. The C65
KERNAL sits at `$E000` through the MAP registers; its interrupt handler
is what runs if a program does not take the vectors.

## The family's memory map

Fixed by measurement. Do not put anything new in a row that belongs to
another program without checking that the two never run together (they
do not: one program at a time), and never in a row marked never.

| Where | Who | What |
|---|---|---|
| bank 0 `$0334-$03FF` | KERNAL | far-call code under BASIC 65; not free (mega-net 5.10) |
| bank 0 `$0800-$0FCF` | gopher, FTP | the 80x25 screen, where the ROM put it |
| bank 0 `$0800-$0FFF` | IRC | the client's buffers, its screen being at `$10000`: nothing prints through the KERNAL, so the page is plain RAM (irc 5.18); the bank's RTI at `$0F0F` is inside it |
| bank 0 `$1000-$10FF` | BASIC 65 | function-key table and live code; not free (gopher notes) |
| bank 0 `$1600-$16FF` | mega-net | the trampoline and its mailbox, reserved in every client |
| bank 0 `$0F0F` | IRC | an RTI its TLS bank's vectors point at while the bank is mapped (irc 5.15) |
| bank 0 `$1700-$17FF` | SSH, Gemini, IRC | the crypto bank's trampoline (one program at a time) |
| bank 0 `$1800-$19FF` | clients | CBM DOS BAM and block buffers |
| bank 0 `$1400-$15FF` | Gemini | the receive buffer, the screen translation buffer, the renderer's row, the boot crumb (gemini 5.8) |
| bank 0 `$1A00-$1F90` | Gemini | the scratch line (the request, the renderer's line, the address typed) to `$1E23`, then the small buffers, listed in its `lowram.h` (gemini 5.6, 5.8, 5.9) |
| bank 0 `$1FB0` | clients | the exit stub, outside the program's own region |
| bank 0 `$2001-$CFFF` | the program | code, data, soft stack growing down from `$D000` |
| bank 0 `$E000-$FEFF` | IRC | RAM once `meganet_own_vectors` has mapped the KERNAL out, below the vector stub at `$FF00`; a PRG cannot load there, so the IRC client puts a third image, HIGH, there by DMA (irc 5.17). Any client that owns its vectors could |
| bank 1 `$10000-$10F9F` | SSH | the screen, 25 or 50 rows (ssh 5.23) |
| bank 1 `$11000-$117FF` | all clients | the lowercase font copy with the six added glyphs (ssh 5.25) |
| bank 1 `$11800-$11FFF` | FTP, Gemini | the bookmark text (ftpc 5.10; gemini 5.8), one program at a time |
| bank 1 `$12000-$1BDFF` | SSH, Gemini, IRC | the crypto image (ssh 5.4); Gemini's TLS bank (gemini 5.4); IRC's, the CRYPTO image (irc 5.15) |
| bank 1 `$1C000-$1DFFF` | Gemini | the link table at `$1C000`, the pinned keys at `$1D000` |
| bank 1 `$12000-$19D3F` | gopher | the parsed menu items (gopher 2.51) |
| bank 1 `$1E000-$1F7FF` | SSH, Gemini, IRC | the bank's top window: SSH's terminal parser (ssh 5.7); Gemini's renderer and its bank's zero-page swap, the RENDER image (gemini 5.8); IRC's chain check, zero-page swap and static stack, the CHAIN image (irc 5.15) |
| bank 1 `$1F800-$1FFFF` | never | the color RAM mirror: code or data here is overwritten by every color write (ssh 5.7). In 80x50 the display's row 25 is cells 2000-2047, `$1FFD0-$1FFFF`, which is where a bank's vectors sit while it is mapped: they must be rewritten on every entry and the colour put back (irc 5.15) |
| banks 2 and 3 | never | the 128 KB ROM; writes do not stick (gopher notes) |
| bank 4 `$42000-$4BFFF` | mega-net | the stack image and its soft stack |
| bank 4 `$4FFF0-$4FFFF` | mega-net | the interrupt vectors in force during a call |
| bank 5 `$50000-$5E8FF` | mega-net | the socket pool: eight TCP sockets, four UDP mailboxes (5.11, 5.16) |
| bank 5 `$5E900-$5F0CF` | SSH | the known-hosts text |
| bank 5 `$5E900-$5FFFF` | Gemini, IRC | the server's certificate: Gemini the leaf, IRC the whole chain to 5,888 bytes (one program at a time) |
| banks 6 and up | never | not memory on this machine |
| attic `$8000000+` | gopher | menu cache at `$8060000`, history at `$80A0000`, downloads at `$8100000`; CPU access and one-off DMA only |
| attic `$8000000+` | Gemini | the body at `$8000000`, the rows at `$8100000`, the link targets at `$8200000`, the history at `$8300000` |
| `$FF80000` | VIC | color RAM, 32 KB, one byte per cell |

The zero page belongs to the running program. mega-net swaps `$90-$FF`
with the caller on every call; the SSH crypto bank swaps `$70-$FF`.
Both put the caller's back. The compiler puts small statics in zero
page unless they are pinned to a named section; pin anything a DMA
job or a bank must see.

## The traps, each paid for once

1. **The Ethernet controller's events reach the IRQ vector past the
   I flag.** A C program must take the vectors from the KERNAL as its
   first act (`meganet_own_vectors()`), and hold the controller in
   reset (`$D6E0 = 0`, three frames) before mega-net's INIT, or one
   boot in three dies in the KERNAL's handler with the ROM mapped
   under it. mega-net 5.16, 5.18; ssh 5.8, 5.19. A BASIC caller needs
   neither. About one boot in twenty still sticks at start-up; a reset
   and a retry is the answer, and the test driver retries.
2. **`STZ` stores the Z register.** llvm-mos compiles every store of
   zero as `STZ`. Any assembly that loads Z ends with `LDZ #0` before C
   runs again. mega-net 5.4, `spike_stz.c`. When a measurement makes no
   sense, check Z first.
3. **A conditional branch further than 127 bytes becomes a 16-bit
   relative branch that this core does not take.** The assembler emits
   it silently. Write a short branch over a `jmp`; keep loops under
   128 bytes or split them. ssh 5.3, 5.11.
4. **A Q instruction on a symbol is assembled in zero-page form**,
   which the linker cannot fit. Emit the two prefix bytes and the plain
   absolute instruction (`LDQA`/`STQA` macros in ssh's `mulacc_m65.S`).
   ssh 5.11.
5. **llvm-mos once compiled `while (n--)` to decrement the wrong
   zero-page word.** Write pointer walks and explicit counters;
   ssh's `tools/orphan_rmw.py` scans a linked ELF for the shape and the
   build refuses on a hit. ssh 5.6. The same family: `strcat` in a loop
   reloading stale pointers (gopher `gopher_config.c`), a static write
   position that never advanced. When a string built by `strcpy` or
   `strcat` is wrong and the inputs are right, write the loop yourself.
6. **No `int64_t`, and `uint32_t` only in accumulators.** Every 64-bit
   operation is expanded inline; TweetNaCl's field arithmetic was 43 KB
   that way. Build with `-Oz`, not `-Os`: the FTP client went from 2 KB
   over to 4 KB free on the flag alone. ssh 5.2, 5.22; mega-net 5.17.
7. **A zero-length DMA copies 64 KB.** `lcopy` and `lfill` pass the
   count straight through; so does mega65-libc's `cputsxy`. Guard every
   caller-controlled length, and every computed one: the clients' shared
   disk loader handed an empty file's zero to `lcopy` and wiped the font,
   the screen and the crypto image (ssh 5.30). gopher notes section 4.
8. **Attic RAM is not coherent under DMA.** A DMA read shortly after a
   DMA write nearby returns stale data at every size and cache setting.
   CPU access is fine; one-off DMA is fine. Never a buffer that DMA
   fills and drains. mega-net 5.13, 5.15, 5.16.
9. **The DMA list registers are shared with BASIC.** Save and restore
   `$D701`, `$D702` and `$D704` around every job you build, or BASIC's
   own scroll fetches its job from your bank. mega-net 5.10.
10. **The exit to BASIC is a software reset from a stub outside the
    program**, with the hot registers on first, then the font pointer
    `$D068-$D06A` back to the ROM's set, the bank byte written once more
    inside the stub. Any other order leaves BASIC typing into an
    unreadable screen. ftpc 5.9; gopher 0.3.2; ssh 5.25.
11. **Screen codes are not ASCII.** `a-z` are 1 to 26; `@ [ ] ^ _ |` are
    graphics at their ASCII values; `\ ^ ` { } ~` do not exist in the
    ROM set, which is why the clients copy it to `$11000` and draw them.
    Keep text ASCII until draw time.
12. **The console library's output is not usable.** `cputsxy` crashes
    under llvm-mos, `sprintf("%s")` crashes, and linking any of it costs
    a 765-byte escape buffer. The clients write cells directly
    (`screen.c`, `termscr.c`) and format numbers by hand. `conioinit`
    and `setscreensize` are still used for the mode.
13. **The program's data can run into its soft stack.** The link
    script's region runs to `$D000` and the stack grows down from there
    into whatever the program leaves above its `.noinit`; the linker
    reserves nothing. A client whose data ended at `$D059` booted into a
    silent crash at its first deep call. Keep 1 KB (the FTP client's
    "below `$C900`" rule); the Gemini client's `build.py` refuses less
    (gemini 5.6).
14. **The video registers**: hot registers `$D05D` bit 7 (turn off
    before setting the screen by hand, on again before the exit); rows
    `$D07B` (24 or 49); screen pointer `$D060-$D063`; font pointer
    `$D068-$D06A`; `$D031` bit 5 for the VIC-III attributes (underline,
    blink) in the color byte's high nibble; one global background
    `$D021`.
15. **The keyboard queue** is `$D610` (ASCII, write to pop), `$D619`
    (PETSCII), `$D611` (modifiers: bit 2 CTRL, bit 3 MEGA). Escape is
    `$1B`, HELP `$1F`, TAB `$09` and shift-TAB `$0F` (its own code,
    not TAB with a shift bit: gemini 5.15), the function keys `$F1-$FE`
    for F1 to F14 in label order, so the MEGA65's own F9, F11 and F13
    are `$F9 $FB $FD`. MEGA with a letter
    delivers the capital letter with bit 7 set (`$C1-$DA`), with bit 3
    of `$D611` set; mask the bit off before comparing (ssh 5.29). A held
    key auto-repeats. **There is no cursor-up code**: up is SHIFT with
    down, `$91` with bit 0 of `$D611` set, and with MEGA held the
    keyboard sends `$11` for *both* directions and marks SHIFT in
    `$D611` instead -- a MEGA-cursor binding must read bit 0 to tell
    them apart, or use other keys (mega-irc 5.26, measured with
    `tools/tap.py --probe`).
16. **Sixteen colors, one background.** A cell has a foreground; the
    background is global and belongs to the user (`$D021`, inherited or
    cycled); a host's background is drawn reversed where it differs
    (ssh 5.31).

## The toolchain

llvm-mos (`mos-mega65-clang`, `~/llvm-mos`), mega65-libc as a sibling
checkout (`../mega65-libc`, built by each project into `build/libc`),
`c1541` from VICE for disks, Python 3 for the builds, `64tass` only for
mega-net's guide listing. Builds are Python (`build.py`) so that
Windows, Linux and macOS are all first-class; the gopher client still
has `build-native.sh`. Nothing may assume a POSIX shell in a build
path.

## The tools on the Mac, and how they bite

`m65` (here `m65.osx`, elsewhere `m65` or `m65.exe`) talks to the
machine over the serial monitor; `mega65_ftp` moves files to the SD
card; `mega-net/tools/mon.py` is a monitor client with memory reads,
breakpoints and watchpoints; `mega-net/tools/device.py` finds the tool
on any host. `MEGA65_PORT=/dev/cu.usbserial-23201` pins the port when
autodiscovery picks the wrong FTDI channel.

- **Typing.** `m65 -T text` types a line and RETURN; `-t` types without,
  with escapes `~M` RETURN, `~D ~U ~L ~R` cursor, `~H` HOME, `~T`
  INST/DEL, `~C` RUN/STOP, `~z ~Z` pause 1 or 2 s. Type lowercase only:
  capitals are dropped and can hang the tool. Never a backslash or a
  non-ASCII byte: the tool hangs, and killing it leaves the monitor
  waiting mid-command until the machine is power-cycled. One field per
  `-T` call, never typing ahead: the virtual keyboard holds keys and a
  held RETURN submits several prompts. Long lines are unreliable.
  **What `-t` cannot press, `tools/tap.py` can**: it takes keyboard
  matrix codes and writes them to the same `$D615-$D617` the driver
  uses, so `tap.py 44` is F9, `tap.py '3d 15'` is MEGA held with F
  (staged modifier-first, as a hand does), and `tap.py --probe ...`
  halts the CPU and prints what `$D610`/`$D611` received for each. The
  driver's own table stops at F1/F3/F5/F7 and has no modifiers, and a
  byte it does not know is dropped *silently* -- an F9 test through
  `-t` that shows nothing has shown nothing (mega-irc 5.26).
- **Screenshots.** `m65 -S0` prints the screen as text; strip the ANSI
  color codes before matching; screen row N is output line N+2. It
  decides upper or lower case from the VIC's font pointer, so with a
  client's RAM font every screen renders as the uppercase set and
  capitals show as `?`: match case-insensitively and avoid capitals in
  patterns (`ost:`, not `Host:`). `-S file.png` renders with the real
  font and is the truth. **Never take any screenshot while an SSH
  session is running**: it hangs the tool and wedges the monitor until
  a power cycle. Polling a client's own screens between sessions is
  fine.
- **Files.** `mega65_ftp` refuses to run while a program is in memory:
  reset with `m65 -F` first. Replace a file with `del X` then `put`,
  never `put` alone. It needs `-l <port>`. The Mac filesystem is
  case-insensitive: `rm PATTERN.BIN` deletes `pattern.bin`.
- **Memory.** `mon.py 'm 001a73a'` reads sixteen bytes at a 28-bit
  address without halting; a bank's CPU address `$A73A` is physical
  `$1A73A`. `m65 --memsave=aaaa:bbbb=file` dumps a range, one byte short
  of the end. Halting the CPU (breakpoints, `-S`, even an extra DMA)
  hides timing faults; read state after the fact instead.
- **Servers.** Public gopher servers rate-limit repeated hits; use the
  fixture servers under each client's `tools/`. An asyncssh server
  needs `line_editor=False` or every full-screen program looks broken
  (ssh 5.21).

## Driving the machine from a script

`mega-net/tools/m65lib.sh` is the driver the projects' hardware tests
use: source it, then `boot_prg DISK PRG PATTERN` resets, mounts and
runs a program and waits for its first screen (retrying the start-up
stick), `wait_for PATTERN` polls the screen until a word appears,
`type_line` and `type_keys` type, `row N` and `scr` read the screen,
`put_d81 FILE NAME` replaces a disk on the card. Poll for the change
you expect rather than sleeping fixed seconds: a test that waits ten
minutes to be sure is a test nobody runs. Poll no faster than once a
second: every screen dump stalls the CPU, so a loop that dumps
without pause made each phase of a TLS handshake five times longer
and timed the server out, and even the one-second polling adds about
a half to what it times; a timing that matters is taken with one
look after a fixed sleep (gemini 5.11). Each client's own notes give
the recipe for its own screens.

## The method, which every repository follows

- **The host first.** Protocols against synthetic packets, crypto
  against the RFC vectors, the terminal against captured streams, in
  milliseconds. Nothing goes to the machine that has not passed.
- **Every wait is bounded**, paced on the `$D7FA` frame counter, and
  mega-net is polled from every loop, including inside long
  computations. The peer closing is not the end of the data: drain,
  then close.
- **Make a fault deterministic before debugging it**, and read state
  after the fact (`mon.py`, crumbs, counters) rather than stopping the
  CPU.
- **Record every hardware finding**, numbered, in `REQUIREMENTS.md`,
  with what was seen and what it cost. A claim without a finding
  behind it is not made.
- **A screenshot proves what is in screen memory.** For the picture,
  the PNG; for the video state, diff the registers against a clean
  boot.
- **Commits stay local until the user asks for a push or a release.**
