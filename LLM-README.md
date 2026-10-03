# mega-net

A TCP/IP stack for the [MEGA65](https://mega65.org), written for the
machine: native mode, the 45E100 Ethernet controller, one jump table
that C, assembly and BASIC 65 all call the same way.

It gives a program Ethernet, ARP, IPv4, ICMP, UDP, DHCP with lease
renewal, DNS, NTP and TCP with eight sockets, including listening ones,
so the MEGA65 can be a client or a server. Three applications run on it
today: a gopher browser, an FTP client and an SSH client.

## What it gives you

- **The whole stack, in native mode.** Your program stays a native
  MEGA65 program at 40 MHz. The stack gets its address by DHCP and
  keeps it, resolves names, sets the clock from the internet, and
  carries TCP in both directions. Eight TCP sockets and four UDP
  mailboxes, all usable at once.
- **One way in, from three languages.** A jump table behind a
  trampoline. C includes `meganet.h`; assembly loads the registers and
  jumps; BASIC 65 pokes a mailbox and does one `SYS`. The same entries,
  the same numbers, whichever you write in.
- **It stays out of your program.** The stack lives in bank 4, its
  buffers in bank 5, its trampoline in one page of bank 0. It never
  touches your memory through the CPU, never touches your zero page,
  and gives back your memory map and interrupt flag exactly as it found
  them after every call. What you pass it and what it returns move by
  DMA.
- **Polled, so it is predictable.** Nothing runs behind your back.
  You call `POLL` from your loop; the stack does its work inside that
  call and returns. Every wait in your program stays yours to bound.
- **Proven twice over.** The core is portable C99 with the hardware
  behind one interface, so the protocols are tested on the development
  machine in milliseconds: 367 checks, including adversarial sequence
  numbers, retransmission and half-closes that are impractical to stage
  on hardware. Then the hardware findings, numbered in
  `REQUIREMENTS.md`: fifty pings at a fifty-millisecond interval, a
  300 KB FTP transfer each way, ten adversarial servers, a lease renewed
  against a real router, a two-line echo server taking callers from a
  Mac.
- **Builds anywhere.** Windows, Linux and macOS are all first-class
  development hosts: the build is Python, the core has no platform
  headers, and the compiler and MEGA65 tools are found rather than
  assumed.
- **0BSD licensed.**

**Status: complete for its first applications and in use.** ABI 0.2,
35 entries. The image is 32 KB in a 40 KB window. No known defects.

## Using it from BASIC 65

Two files go on the disk your program runs from: `TRAMP`, the
trampoline, and `MEGANET`, the stack. `python3 tools/basic_d81.py`
makes a disk with both. Then, in your program:

```basic
10 BANK 0
20 BLOAD "TRAMP",B0
30 BLOAD "MEGANET",B4
40 E=$2000:A=0:X=0:Y=0:Z=0:GOSUB 1000    : REM INIT, once, first
50 E=$201E:GOSUB 1000                     : REM DHCP_START
60 E=$200F:GOSUB 1000                     : REM POLL: the stack works inside this call
70 E=$2021:GOSUB 1000:IF R<>3 THEN 60     : REM DHCP_STATE, until BOUND
80 PRINT "ONLINE"
90 END
1000 POKE $1600,E AND 255:POKE $1601,INT(E/256)
1010 POKE $1602,A:POKE $1603,X:POKE $1604,Y:POKE $1605,Z
1020 SYS $160E
1030 R=PEEK($1606):RETURN
```

Lines 1000 to 1030 are the one subroutine every call goes through: `E`
is the entry, `A`, `X`, `Y` and `Z` the arguments, `R` the result, with
further result bytes at `$1607` to `$1609`. Read results with `PEEK`,
never `RREG`, which sees the trampoline's registers rather than the
stack's. Load the program from disk rather than typing it in over the
serial monitor, which drops lines.

[`docs/TUTORIAL-BASIC.md`](docs/TUTORIAL-BASIC.md) starts from the
beginning, for someone who has not used PEEK, POKE or SYS before, and
builds a program that fetches a page from the internet a line at a
time. [`docs/GUIDE-BASIC.md`](docs/GUIDE-BASIC.md) is the shorter
reference: the entry table, a DNS lookup, a gopher fetch, UDP, and a
server, all verified on the machine as one program.

## Using it from C

Build the stack once with `python3 build.py abi`. That produces the
image, the trampoline, and `build/gen/meganet_tramp.c`, the trampoline
as a C array for your program to embed. The image is best shipped as a
file on your disk and loaded into bank 4 at start; the three clients
all do that with the same loader.

A program comes up like this:

```c
#include "meganet.h"

POKE(0xD6E0, 0);                 /* hold the ethernet controller in reset for three frames: */
wait_frames(3);                  /* a stray event from an earlier run must not land during INIT */
meganet_own_vectors();           /* take the interrupt vectors from the KERNAL, before the first call */
load_image_and_trampoline();     /* MEGANET to $42000, TRAMP to $1600, by DMA or from disk */
meganet_set_restore_map(0x00, 0xE0, 0x00, 0x00);   /* the map the trampoline puts back: yours */
meganet_call(MEGANET_INIT, 0, 0, 0, 0);

meganet_dhcp_start();            /* or meganet_set_ip(&conf) for a static address */
while (meganet_dhcp_state() != MEGANET_DHCP_BOUND) meganet_poll();
```

The two lines before `meganet_own_vectors()` matter: this core delivers
the controller's events to the IRQ vector even with interrupts masked,
so a C program must own its vectors before the stack's first call and
must quiet the controller first (`REQUIREMENTS.md` 5.16, 5.18). A BASIC
program needs neither.

A name, then a connection, a request, and the reply until the peer
closes:

```c
uint8_t ip[4], flags, st; uint16_t avail, n;

meganet_dns_start("gopher.floodgap.com");
while (meganet_dns_state() == MEGANET_DNS_WAITING) meganet_poll();
if (meganet_dns_state() != MEGANET_DNS_DONE) fail();
meganet_dns_result(ip);

meganet_tcp_connect(ip, 70);
while ((st = meganet_tcp_state(&flags, &avail)) != MEGANET_TCP_ESTABLISHED) {
    if (st == MEGANET_TCP_CLOSED) fail();          /* refused, reset or timed out: the flags say which */
    meganet_poll();
}
meganet_tcp_send("/\r\n", 3);
for (;;) {
    meganet_poll();
    meganet_tcp_state(&flags, &avail);
    if (avail) { n = meganet_tcp_recv(buf, sizeof buf); show(buf, n); }
    else if (flags & MEGANET_TCP_F_EOF) break;     /* drained, and the peer is done */
}
meganet_tcp_close();
```

Those are socket 0; the `_s` forms (`meganet_tcp_connect_s`,
`meganet_tcp_recv_s`, and so on) take a socket number. A server is as
short. This one listens on two sockets and echoes:

```c
meganet_tcp_listen(0, 6400); meganet_tcp_listen(1, 6400);
for (;;) {
    meganet_poll();
    for (s = 0; s < 2; s++) {
        st = meganet_tcp_state_s(s, &flags, &avail);
        if (st == MEGANET_TCP_ESTABLISHED && avail) {
            n = meganet_tcp_recv_s(s, buf, sizeof buf);
            meganet_tcp_send_s(s, buf, n);
        } else if (st == MEGANET_TCP_ESTABLISHED && (flags & MEGANET_TCP_F_EOF)) {
            meganet_tcp_close_s(s);
        } else if (st == MEGANET_TCP_CLOSED) {
            meganet_tcp_listen(s, 6400);           /* the socket is free again: listen again */
        }
    }
}
```

A third caller while both are busy is refused with a reset, which is
what `tools/server_test.py` checks from the development machine.

Arguments and results travel in A, X, Y and Z. Anything larger is a
28-bit pointer to a block in your memory, which the stack reads and
writes back by DMA. Keep polling and the lease renews itself: at half
the lease the stack asks its server, at seven eighths it asks everyone,
and only if the lease runs out does it drop the address and start over.
`meganet_dhcp_status()` reports the phase and the minutes left.

If you write assembly of your own on this machine: llvm-mos compiles
"store zero" as `STZ`, which the 45GS02 executes as "store Z". Leave Z
at zero before returning to C. `src/spike/spike_stz.c` shows what
happens otherwise.

[`docs/GUIDE-C-ASM.md`](docs/GUIDE-C-ASM.md) has the full treatment:
the binaries, the poll loop, sockets, the memory promises, the mailbox
from assembly, and how to extend the ABI.
[`docs/ABI.md`](docs/ABI.md) is the contract, entry by entry.

## What the stack uses, and what it leaves you

| Where | What |
|---|---|
| bank 0 `$1600-$16FF` | the trampoline: mailbox and call entry |
| bank 4 `$42000-$4BFFF` | the image and its own stack |
| bank 4 `$4FFF0-$4FFFF` | the interrupt vectors in force during a call |
| bank 5 `$50000-$5E8FF` | socket buffers: eight TCP sockets, four UDP mailboxes |

Nothing else. Bank 1, the rest of bank 4, all of attic RAM and every
byte of bank 0 but that one page are yours. The socket buffers are in
chip RAM because attic RAM is not coherent under DMA (`REQUIREMENTS.md`
5.13, 5.16).

## Building and testing

Host tests need a C99 compiler (`cc`, `gcc`, `clang` or MSVC) and
Python 3. The target build needs the [llvm-mos](https://llvm-mos.org)
SDK.

```
python3 build.py test     the host suite: the protocols against synthetic packets
python3 build.py abi      the stack image, its trampoline, and the hardware spikes
python3 build.py m65      compile the portable core for the MEGA65 alone
python3 tools/basic_d81.py       a disk for BASIC callers, with the guide's test program
```

| Variable | Overrides |
|---|---|
| `CC` | host compiler |
| `LLVM_MOS_DIR` | llvm-mos SDK root (default `~/llvm-mos`, or PATH) |
| `MEGA65_M65` | the `m65` tool, whatever it is called locally |
| `MEGA65_PORT` | serial port, if autodiscovery picks the wrong one |

Hardware goes through `tools/device.py`, which knows the tool is called
`m65` on Linux, `m65.osx` on macOS and `m65.exe` on Windows:

```
python3 tools/device.py --check          does the machine answer?
python3 tools/device.py --shot out.png   capture the screen
python3 tools/server_test.py 192.168.1.252      drive spike_server: two echo lines, a refusal, UDP
python3 tools/tcp_test_server.py log.txt        a known peer on :7070 for the TCP spike
```

## Layout

| Path | What |
|---|---|
| `src/net/` | The portable core, one file per protocol; `mn_net.c` ties them together. C99 and `stdint.h`, nothing else. |
| `src/net/mn_netif.h` | The entire hardware boundary. |
| `src/hal/` | The MEGA65 side: the 45E100 driver, DMA, extended memory. |
| `src/abi/` | The banked image's entry, the jump table, the trampoline, the linker scripts, and `meganet.h`, the one header a client needs. |
| `src/spike/` | Hardware proofs, each a self-contained PRG: a frame, ping, DHCP, time, TCP, a server, attic RAM, the STZ hazard. Kept as regression probes. |
| `test/` | The host harness: an `mn_netif` backed by memory, and the suite. |
| `tools/` | The device driver, the serial monitor, the BASIC disk builder, the test servers. |
| `docs/` | The BASIC guide, the C and assembly guide, the ABI. |
| `REQUIREMENTS.md` | The decisions, the plan, and every hardware finding, numbered. |

Packets are never overlaid with structs. Every field goes through the
big-endian accessors in `mn_byteorder.h`, because llvm-mos and a host
compiler cannot be relied on to lay out packed structs identically.

## License

0BSD. See `LICENSE`.
