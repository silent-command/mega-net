# mega-net ABI reference

The stack is a headerless image loaded at physical `$42000` (bank 4).
Its first bytes are a jump table: entry *N* is at `$42000 + 3N`, forever
— entries are appended, never renumbered. A call maps bank 4 over CPU
`$2000-$BFFF` (and `$E000-$FFFF`, for the stack's own interrupt vectors),
runs the entry, and restores the caller's map and interrupt flag. Every
call is synchronous and returns quickly; the stack does nothing between
calls. **Poll it**: `POLL` (entry 5) pulls one frame from the link and
runs every timer. Call it as often as you can; nothing happens otherwise.

## Calling

Arguments go in A, X, Y and Z; results come back in A, X, Y and Z.
Anything larger travels in a *parameter block* in the caller's memory:
A/X/Y hold its 28-bit address (low, middle, high), and the stack reads
and writes the block by DMA. It never touches the caller's memory with
the CPU, never touches the caller's zero page, and leaves the DMA list
registers as it found them.

Calls go through the **trampoline**, 92 bytes at `$1600`, a page the
stack reserves in every program:

| Address | Bytes | Meaning |
|---|---|---|
| `$1600-$1601` | 2 | entry address as mapped: `$2000 + 3N`, low byte first |
| `$1602-$1605` | 4 | A, X, Y, Z in |
| `$1606-$1609` | 4 | A, X, Y, Z out |
| `$160A-$160D` | 4 | the map restored after the call, as MAP's A, X, Y, Z. Default `00 E0 00 83`: the SYS map, which is also a running llvm-mos program's. Leave it unless you have remapped. |
| `$160E` | | the call: `JSR` from assembly or C, `SYS $160E` from BASIC |

From C, include `src/abi/meganet.h`: `meganet_call(entry, a, x, y, z)`
and the typed helpers wrap the mailbox. From BASIC 65, `BLOAD "TRAMP",B0`
and `BLOAD "MEGANET",B4` from a disk made by `tools/basic_d81.py`, then
`POKE $1600,lo : POKE $1601,hi : SYS $160E` and `PEEK($1606)` for the
result — `RREG` reports the trampoline's own registers, not the stack's.

## What the stack uses

| Where | What |
|---|---|
| bank 0 `$1600-$16FF` | the trampoline |
| bank 4 `$42000-$4BFFF` | the image and its own stack |
| bank 4 `$4FFF0-$4FFFF` | interrupt vectors in force during a call |
| bank 5 `$50000-$5E8FF` | socket buffers: TCP rings then UDP mailboxes (`TCP_INFO` reports kind 2) |

## The table

Addresses are physical; the mapped entry address is the low 16 bits
(`$2000 + 3N`). "Block" is the parameter block at the 28-bit address in
A/X/Y. Multi-byte block fields are little-endian.

| N | Physical | Name | In | Out | Block |
|---|---|---|---|---|---|
| 0 | `$42000` | INIT | — | A=0 | Resets everything. Call once, first. |
| 1 | `$42003` | VERSION | — | A=major X=minor Y='M' Z='N' | 0.2 as of this table: entries 28-34 and the extra results of entries 8 and 11 are 0.2 |
| 2 | `$42006` | GET_MAC | block | — | out: 6 bytes |
| 3 | `$42009` | LINK_TX | block | A=1 sent | ptr28[3], len[2]: a raw Ethernet frame |
| 4 | `$4200C` | LINK_RX | block | A=1 got | ptr28[3], cap[2]; out len[2] |
| 5 | `$4200F` | POLL | — | A=1 if a frame was handled | The pump. |
| 6 | `$42012` | SET_IP | block | — | ip[4], mask[4], gw[4]: a static address |
| 7 | `$42015` | GET_IP | block | — | out: ip[4], mask[4], gw[4] |
| 8 | `$42018` | GET_STATS | block | — | out, 36 bytes: rx frames, ARP replies, echo replies, tx failures, ARP learned, UDP rx, UDP dropped, DHCP tries, DHCP rx, DHCP last length (u16 each); DHCP last op, type, last xid[4], xid[4], last chaddr[0], [5]; interrupts caught during calls u16; TCP RSTs sent u16 |
| 9 | `$4201B` | SET_LOCAL_IP4 | A,X,Y,Z = ip | — | Static address, no block (BASIC: `SYS addr,a,b,c,d` style pokes) |
| 10 | `$4201E` | DHCP_START | — | A=0 | Begins acquisition; the address is 0.0.0.0 until bound. |
| 11 | `$42021` | DHCP_STATE | — | A=state, X=phase, Y/Z=minutes of lease left | state: 0 idle, 1 selecting, 2 requesting, 3 bound, 4 failed. phase while bound: 0 bound, 1 renewing, 2 rebinding. Minutes 65535 = no expiry. |
| 12 | `$42024` | GET_DNS | block | — | out: 4 bytes, the DNS server DHCP gave |
| 13 | `$42027` | SET_DNS | A,X,Y,Z = server | — | for static setups |
| 14 | `$4202A` | DNS_START | block | A=0 started | NUL-terminated name, up to 63 bytes |
| 15 | `$4202D` | DNS_STATE | — | A: 0 idle, 1 waiting, 2 done, 3 failed | |
| 16 | `$42030` | DNS_RESULT | — | A,X,Y,Z = the address | |
| 17 | `$42033` | NTP_START | A,X,Y,Z = server | — | |
| 18 | `$42036` | NTP_STATE | — | A: 0 idle, 1 waiting, 2 done, 3 failed | |
| 19 | `$42039` | NTP_RESULT | block | — | in: offset_min i16; out: seconds u32 (NTP, since 1900), year u16, month, day, hour, minute, second, weekday (0 = Sunday) |
| 20 | `$4203C` | SET_RTC | block | A=0 | year u16, month, day, hour, minute, second, weekday (0 = Sunday, written to the clock as given) |
| 21 | `$4203F` | DHCP_LAST_MSG | block | — | diagnostic: 300 bytes of the network layer's receive buffer |
| 22 | `$42042` | TCP_CONNECT | block, Z=socket | A=1 started | ip[4], port[2] |
| 23 | `$42045` | TCP_STATE | Z=socket | A=state, X=flags, Y/Z=bytes available | see below |
| 24 | `$42048` | TCP_SEND | block, Z=socket | A/X = bytes accepted | ptr28[3], len[2] |
| 25 | `$4204B` | TCP_RECV | block, Z=socket | A=1 if any | ptr28[3], cap[2]; out len[2] |
| 26 | `$4204E` | TCP_CLOSE | Z=socket | — | graceful: FIN after the queued data |
| 27 | `$42051` | TCP_ABORT | Z=socket | — | RST and drop |
| 28 | `$42054` | TCP_LISTEN | A/X = port, Z=socket | A=1 listening | |
| 29 | `$42057` | TCP_PEER | block, Z=socket | — | out: ip[4], port[2] of the peer |
| 30 | `$4205A` | TCP_INFO | — | A=sockets, X=1 attic RAM / 2 bank 5, Y/Z=pool base >> 12 | |
| 31 | `$4205D` | UDP_OPEN | A/X = port | A=socket, or $FF | |
| 32 | `$42060` | UDP_CLOSE | Z=socket | — | |
| 33 | `$42063` | UDP_SEND | block, Z=socket | A: 0 failed, 1 sent, 2 resolving (send again) | ip[4], port[2], ptr28[3], len[2] |
| 34 | `$42066` | UDP_RECV | block, Z=socket | A=1 if a datagram was waiting | ptr28[3], cap[2]; out len[2], src ip[4], src port[2] |

A socket index past the end answers A=$FF. Every TCP entry takes the
index in Z; a caller that never sets Z has socket 0.

### TCP states and flags

| State | | Flags (X) | |
|---|---|---|---|
| 0 | CLOSED | `$01` | EOF: the peer has finished sending; what arrived is still readable |
| 1 | SYN_SENT | `$02` | RESET: the peer reset the connection |
| 2 | ESTABLISHED | `$04` | TIMEOUT: retransmissions exhausted |
| 3 | FIN_WAIT_1 | `$08` | REFUSED: the SYN was answered with RST |
| 4 | FIN_WAIT_2 | | |
| 5 | CLOSE_WAIT | | |
| 6 | CLOSING | | |
| 7 | LAST_ACK | | |
| 8 | TIME_WAIT | | |
| 9 | LISTENING | | |
| 10 | SYN_RCVD | | |

A listening socket goes ESTABLISHED when a caller completes the
handshake and CLOSED when that connection ends; listen again for the
next caller. Several sockets may listen on one port. A SYN for a port
with no free listener is refused with RST at once.

Receive rings are 4 KB and stay readable after the peer closes: drain,
*then* close. Send rings are 3 KB and the sender keeps up to four
segments in flight; `TCP_SEND` reports how much it took. The window is advertised by its right edge, so a slow reader
never stalls the sender.

## Rules the stack keeps

- Entries are appended, never renumbered or changed in meaning.
- Register results are also in the trampoline's mailbox at `$1606`.
- The stack leaves shared hardware state as it found it: the DMA list
  address registers, the caller's zero page and memory map, the
  interrupt flag. It asserts the MEGA65 I/O personality on every entry
  and does not restore it, because it cannot be read.
- Every wait inside the stack is bounded; no call blocks on the network.
