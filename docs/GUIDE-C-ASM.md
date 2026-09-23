# mega-net from C and assembly

For someone comfortable with llvm-mos or 45GS02 assembly who wants a
network stack they can call and forget about. This is the practical
side; `docs/ABI.md` is the contract, and `REQUIREMENTS.md` the record of
why each thing is the way it is.

## 1. The shape of it

mega-net is a headerless image of about 32 KB, loaded at physical
`$42000` (bank 4). Its first bytes are a jump table; entry *N* is at
`$42000 + 3N` and entries are only ever appended. A call maps bank 4
over CPU `$2000-$BFFF` for its duration and puts the caller's map and
interrupt flag back afterwards, so the caller's program can live
anywhere in that range and never notices. Every call returns promptly;
nothing blocks on the network, and nothing happens between calls.
**`POLL` is the engine**: it pulls one frame and runs every timer, and
you call it from your main loop as often as you can.

Calls go through a 92-byte trampoline at `$1600`, the one page of bank
0 the stack reserves. It has a mailbox: entry address, A/X/Y/Z in,
A/X/Y/Z out, and the map to restore. Arguments larger than four bytes
travel in a *parameter block* in your memory whose 28-bit address goes in
A/X/Y; the stack reads and writes blocks by DMA and never touches your
memory with the CPU.

## 2. Getting the two binaries into place

`python3 build.py abi` in the mega-net tree produces:

| File | Use |
|---|---|
| `build/m65/meganet.bin` | the image, for a disk |
| `build/m65/meganet_call.bin` | the trampoline |
| `build/gen/meganet_payload.{c,h}` | both as C arrays, for embedding |
| `build/gen/meganet_tramp.{c,h}` | the trampoline alone plus `MEGANET_BIN_SIZE`, for a program that loads the image from disk |

Embedding is simplest and costs 32 KB of your program's file:

```c
#include "meganet.h"           /* src/abi */
#include "meganet_payload.h"   /* build/gen */
#include "mn_dma.h"            /* src/hal: the 28-bit DMA copy; or use lcopy() from mega65-libc */

mn_m65_io_enable();
mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
meganet_call(MEGANET_INIT, 0, 0, 0, 0);
```

Loading from disk is what the gopher client does (`src/gopher_boot.c`
there): the image travels as a second file on the `.d81`, read into bank
4 at startup; only the trampoline is embedded, from `meganet_tramp.c`.

`MN_PHYS(p)` turns a pointer in your bank-0 program into the 28-bit
address DMA needs. If you build with `-DMN_PHYS_BASE=...` for a program
that itself lives in another bank, it accounts for that.

## 3. The loop

Everything is a state machine you poll. The whole of a client that gets
an address, resolves a name, fetches a document and hangs up:

```c
static uint8_t last_frame; static uint16_t frames;
static void pump(void) {                     /* poll, and count frames for timeouts */
    meganet_poll();
    if (MN_FRAMECOUNT != last_frame) { last_frame = MN_FRAMECOUNT; frames++; }
}

meganet_dhcp_start();
while (meganet_dhcp_state() != MEGANET_DHCP_BOUND) pump();     /* add a frame limit */

meganet_dns_start("gopherpedia.com");
while (meganet_dns_state() == MEGANET_DNS_WAITING) pump();
if (meganet_dns_state() != MEGANET_DNS_DONE) fail();
meganet_dns_result(ip);

meganet_tcp_connect(ip, 70);
for (;;) {
    pump();
    st = meganet_tcp_state(&flags, &avail);
    if (st == MEGANET_TCP_ESTABLISHED) break;
    if (st == MEGANET_TCP_CLOSED) fail();                       /* flags say why */
}
meganet_tcp_send("/\r\n", 3);
for (;;) {
    pump();
    n = meganet_tcp_recv(buf, sizeof buf);
    if (n) consume(buf, n);
    st = meganet_tcp_state(&flags, &avail);
    if ((flags & MEGANET_TCP_F_EOF) && !avail) break;           /* drained after the peer's close */
    if (st == MEGANET_TCP_CLOSED) break;
}
meganet_tcp_close();
```

`src/spike/spike_tcp.c` is this, complete, with the timeouts.

Three rules the gopher client learned the hard way, which the stack now
enforces for you but which still shape a good client:

- **The peer closing is not the end of the data.** Keep reading until
  `avail` is zero *and* the EOF flag is set, then close.
- **Close (or abort) before you connect again on the same socket.**
- **Send a request in one call.** The stack sends what is queued as one
  segment; byte-at-a-time sends get you reset by real servers.

Time everything with the frame counter. `MN_FRAMECOUNT` is `$D7FA`, 50
or 60 per second; no other wait in the stack, and none in a good client,
spins on anything else.

## 4. Sockets

Eight TCP sockets, `0` to `7`. Every TCP helper has a `_s` form that
takes the socket first; the plain form is socket 0. A server:

```c
meganet_tcp_listen(0, 6400);
meganet_tcp_listen(1, 6400);              /* two lines on one port */
for (;;) {
    meganet_poll();
    for (s = 0; s < 2; s++) {
        st = meganet_tcp_state_s(s, &flags, &avail);
        if (st == MEGANET_TCP_ESTABLISHED || st == MEGANET_TCP_CLOSE_WAIT) {
            if (avail) { n = meganet_tcp_recv_s(s, buf, sizeof buf); meganet_tcp_send_s(s, buf, n); }
            else if (flags & MEGANET_TCP_F_EOF) meganet_tcp_close_s(s);
        } else if (st == MEGANET_TCP_CLOSED && was_live[s]) {
            meganet_tcp_listen(s, 6400);  /* the line is free: next caller */
        }
    }
}
```

A listening socket becomes ESTABLISHED when the handshake completes and
CLOSED when that connection ends; you listen again. A SYN for a port with
no free listener is refused with RST at once. `meganet_tcp_peer()` tells
you who connected. `src/spike/spike_server.c` is the full version, and
`tools/server_test.py` on a Mac or PC drives it.

Send rings are 3 KB and the sender keeps up to four segments in flight,
so `meganet_tcp_send_s()` may accept fewer bytes than offered when the
ring is full; check the return and send the rest after a poll. Receive
rings are 4 KB; the window is advertised by its right edge, so a slow
consumer never stalls the peer at a size boundary.

UDP: `meganet_udp_open(port)` gives a socket (or `$FF`; there are four,
and DHCP, DNS and NTP borrow one each while they run),
`meganet_udp_send()` and `meganet_udp_recv()` move datagrams; a send may
return `MEGANET_SEND_PENDING` while the next hop's MAC is resolved,
which means "poll and send again".

## 5. Memory, and what the stack promises about yours

| Where | What |
|---|---|
| bank 0 `$1600-$16FF` | the trampoline |
| bank 4 `$42000-$4BFFF` | the image and its own soft stack |
| bank 4 `$4FFF0-$4FFFF` | the interrupt vectors in force during a call |
| bank 5 `$50000-$5E8FF` | socket buffers: eight TCP rings, four UDP mailboxes |

Nothing else. In particular:

- **Your zero page is untouched.** The stack's own zero page is
  `$90-$FF`, which it swaps out and back around every call; `$02-$8F`
  it never reads or writes. The one thing not to do is place a
  *parameter block* in `$90-$FF`: the DMA would land on the stack's
  copy, not yours.
- **Your memory map comes back.** The trampoline restores the map at
  `$160A-$160D` after each call; the default is the SYS map, which is
  what a running llvm-mos program and BASIC 65 both have. A program
  that has remapped writes its own four MAP bytes there
  (`meganet_set_restore_map()`).
- **Interrupts are enabled between calls and masked during them.**
  During a call the KERNAL is unmapped, so the stack maps its own
  vectors at the top of bank 4 and an RTI stub takes anything that
  fires. One thing does fire: the 45E100 raises the CPU's IRQ vector
  after transmits on this core even with its enables clear and I set.
  Between calls that reaches *your* handler, so your IRQ handler must
  tolerate an interrupt with no source it recognizes. The KERNAL's does.
- **The DMA list registers** `$D701/$D702/$D704` are saved and restored
  around every job. The MEGA65 I/O personality is asserted on every
  entry and not restored, because it can't be read.

Attic RAM is yours entirely; the stack doesn't use it (DMA to it is
unreliable on this core, which is documented and measured, 5.16).

### 5.1 Your interrupt vectors

On this core the ethernet controller's events reach the CPU's IRQ
vector regardless of the controller's enables and of the I flag
(REQUIREMENTS.md 5.16). During a call the stack's own stub absorbs
them. Between calls your vectors are in force, and if they are the
KERNAL's, its handler runs on your zero page and stack and returns with
the ROM mapped under your program: the SSH client lost one boot in two
to this at DHCP start (5.18). So, first thing in `main`:

```c
meganet_own_vectors();                /* a stub in bank 0 under $E000; the KERNAL out of the map */
/* ... copy the trampoline into place, then: */
meganet_set_restore_map(0x00, 0xE0, 0x00, 0x00);   /* the copy carried the KERNAL map */
```

`src/abi/meganet_vectors.c`, which uses `lpoke` and `lcopy` from
mega65-libc. The stub records the first interrupted PC and flags at
`$FF80-$FF82`, so a hang leaves the address of the first event. Your
exit to BASIC must map the ROM back itself before jumping through the
reset vector; the SSH client's `m65_exit.c` does.

## 6. From assembly

The mailbox is the whole interface (the listing is 64tass syntax and assembles as printed; ca65 differs
only in `.byte` for the string; llvm-mos's assembler spells `<name` as
`mos16lo(name)`). Set the entry
and the registers, `JSR $160E`, read the results from the mailbox — **not** from the
registers, which hold the trampoline's housekeeping on return (A/X/Y are
the restored map bytes and Z is 0).

```
; resolve "gopherpedia.com": DNS_START takes a 28-bit pointer to the name
        lda #<name          ; A/X/Y = pointer, low/mid/high
        sta $1602
        lda #>name
        sta $1603
        lda #0              ; bank 0
        sta $1604
        sta $1605           ; Z: unused here
        lda #$2a            ; entry $202A
        sta $1600
        lda #$20
        sta $1601
        jsr $160e
        lda $1606           ; 0 = started
        bne fail
poll:   lda #$0f            ; POLL, $200F
        sta $1600
        lda #$20
        sta $1601
        jsr $160e
        lda #$2d            ; DNS_STATE, $202D
        sta $1600
        jsr $160e           ; $1601 still $20
        lda $1606
        cmp #1              ; 1 = waiting
        beq poll
        cmp #2              ; 2 = done: DNS_RESULT ($2030) leaves the address in $1606-$1609
        bne fail            ; 3 = not found
        lda #$30
        sta $1600
        jsr $160e           ; the four bytes of the address are now at $1606-$1609
        rts
fail:   rts
name:   .text "gopherpedia.com", 0     ; 64tass; ca65 spells this .byte
```

The trampoline itself is position-dependent (it's linked for `$1600`) but
otherwise ordinary code: `PHP`/`SEI`, a MAP to bank 4, `JSR` to the entry,
results into the mailbox, the restore MAP, `PLP`, `RTS`. `src/abi/trampoline.S`
is 60 lines and worth reading once.

## 7. Seeing what it is doing

- `meganet_get_stats()` fills a block: frames received, ARP and echo
  replies, transmit failures, UDP counts, the DHCP transaction's
  details, interrupts caught during calls, and TCP RSTs sent for
  segments nobody owned. `meganet_tcp_info()` says how many sockets
  and where their buffers are. `meganet_dhcp_status()` gives the lease
  phase and minutes left.
- `tools/mon.py` talks to the MEGA65's serial monitor from the
  development machine: registers including the memory map, memory by
  28-bit address, breakpoints. `tools/device.py` wraps `m65` for screen
  captures and memory dumps.
- `tools/tcp_test_server.py` sends 5,000 known bytes; `tools/server_test.py`
  exercises a server on the MEGA65; the gopher project's `tools/` has
  adversarial servers (slow, silent, late).
- Protocol logic lives in `src/net/`, portable C99 with a host test suite
  (`python3 build.py test`, 367 checks). If you extend the stack, extend
  the tests; the stub link in `test/` injects frames without hardware.

## 8. Extending the ABI

Append an entry: an `ENTRY` line in `src/abi/jumptable.S`, a
`mn_api_*` function in `src/abi/api.c` that reads `mn_api_a..z` and
calls `ret()`, a constant and a helper in `src/abi/meganet.h`. Never
renumber. Large data goes through pointer blocks by DMA. The image's
window has about 3 KB to spare; state that scales with sockets belongs
in the pool (`mn_xmem.h`), transient buffers on `mn_net_scratch()`, and
`uint32_t` arithmetic is worth avoiding on this CPU. The project notes in
the tree list the rules that were each learned from a failure.
