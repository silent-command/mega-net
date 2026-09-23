# mega-net from BASIC 65: a hobbyist's guide

This is for someone who writes BASIC 65 on a MEGA65 and wants their
program to talk to the network: fetch something from a server, send a
message to another computer, or let another computer connect in. No C,
no assembly, and nothing you can't do with `POKE`, `PEEK` and `SYS`.
If those three are new to you, start with `TUTORIAL-BASIC.md`, which
builds one program slowly and explains every line.

The stack does the hard parts (Ethernet, ARP, IP, DHCP, DNS, TCP, UDP).
Your program does three things: load it, poke it a request, and keep
calling `POLL` so it can work.

## 1. What you need on the disk

Two files, on the disk your program runs from:

| File | What it is | Where it loads |
|---|---|---|
| `TRAMP` | 92 bytes of machine code your `SYS` goes through | `$1600` in bank 0 |
| `MEGANET` | the stack itself, about 32 KB | `$2000` in bank 4 |

The easiest way to get a disk with both: in the mega-net folder, run
`python3 tools/basic_d81.py`. That makes `build/basic/MNBASIC.D81`, which
also carries `MNBASIC`, the test program this guide is based on. Copy the
two files onto your own disk with the MEGA65's file tools, or just put
your program on that disk.

## 2. Loading, and the one subroutine you need

```basic
10 BANK 0
20 BLOAD "TRAMP",B0
30 BLOAD "MEGANET",B4
40 REM --- the stack is loaded; set up a place for parameter blocks ---
50 BF=$6000: REM a free area above your program text (see section 8)
```

Every call to the stack goes through this subroutine. Put it at the
end of your program and never change it:

```basic
1000 POKE $1600,E AND 255:POKE $1601,INT(E/256)
1010 POKE $1602,A:POKE $1603,X:POKE $1604,Y:POKE $1605,Z
1020 SYS $160E
1030 R=PEEK($1606):RETURN
```

`E` is the entry you want (a number from the table below), `A`, `X`, `Y`
and `Z` are the four register arguments, and `R` comes back as the
result. If an entry returns more than one byte, the others are at
`PEEK($1607)`, `PEEK($1608)` and `PEEK($1609)`.

> **Don't use RREG.** After `SYS $160E` the CPU registers hold the
> trampoline's own housekeeping, not your answer. Always `PEEK($1606)`.

Two calls you make once:

```basic
60 E=$2000:A=0:X=0:Y=0:Z=0:GOSUB 1000: REM INIT: must be first
70 E=$201E:GOSUB 1000:              REM DHCP_START: ask the router for an address
```

## 3. Polling: the rule that makes everything work

The stack does nothing on its own. It only reads the network and runs
its timers when you call `POLL`, entry `$200F`. So every wait in your
program is a loop that calls `POLL`:

```basic
100 REM wait for the address
110 T=TI
120 E=$200F:GOSUB 1000
130 E=$2021:GOSUB 1000: REM DHCP_STATE
140 IF R=3 THEN 200:    REM 3 = bound: we have an address
150 IF R=4 THEN PRINT "NO DHCP SERVER":END
160 IF TI-T<600 THEN 120
170 PRINT "DHCP TIMED OUT":END
200 PRINT "ONLINE"
```

Call `POLL` as often as you can, at least every few milliseconds while
anything is in progress. A BASIC loop that does nothing but `GOSUB 1000`
manages a few hundred polls a second, which is plenty. If your program
stops polling for more than 20 minutes, the stack's lease clock loses
track; a program that sits at an `INPUT` prompt should poll in between
(section 7 has a way).

## 4. Parameter blocks: how bigger things are passed

Four registers carry four bytes. Anything larger — an address to
connect to, text to send, a buffer to receive into — lives in a
*parameter block*: a few bytes you `POKE` into bank 0 memory, whose
address you pass in `A` (low byte) and `X` (high byte), with `Y=0`.

Reading your address back after DHCP:

```basic
210 E=$2015:A=BF AND 255:X=INT(BF/256):Y=0:Z=0:GOSUB 1000: REM GET_IP
220 PRINT "IP";PEEK(BF);".";PEEK(BF+1);".";PEEK(BF+2);".";PEEK(BF+3)
230 PRINT "GATEWAY";PEEK(BF+8);".";PEEK(BF+9);".";PEEK(BF+10);".";PEEK(BF+11)
```

`GET_IP` writes twelve bytes at the block: the address, the netmask and
the gateway, four bytes each. The stack writes and reads these blocks by
DMA, so they work anywhere in bank 0.

## 5. The entries you will use

All entries are `$2000 + 3 × number`. States and results are in `R`.

| E | Name | In | Out |
|---|---|---|---|
| `$2000` | INIT | | 0 |
| `$200F` | POLL | | 1 if a packet was handled |
| `$201E` | DHCP_START | | |
| `$2021` | DHCP_STATE | | 0 idle, 1 asking, 2 asking, **3 bound**, 4 failed |
| `$2015` | GET_IP | A/X = block | 12 bytes: ip, mask, gateway |
| `$201B` | SET_LOCAL_IP4 | A,X,Y,Z = address | for a fixed address instead of DHCP |
| `$202A` | DNS_START | A/X = block with the name | 0 started |
| `$202D` | DNS_STATE | | 0 idle, 1 waiting, **2 done**, 3 failed |
| `$2030` | DNS_RESULT | | the four bytes at `$1606-$1609` |
| `$2042` | TCP_CONNECT | A/X = block {ip, port lo, port hi}, Z = socket | 1 started |
| `$2045` | TCP_STATE | Z = socket | state; flags at `$1607`; bytes waiting at `$1608` + 256 × `$1609` |
| `$2048` | TCP_SEND | A/X = block {addr lo, hi, 0, len lo, len hi}, Z = socket | bytes accepted (`$1606` + 256 × `$1607`) |
| `$204B` | TCP_RECV | A/X = block {addr lo, hi, 0, cap lo, cap hi, len lo, len hi}, Z = socket | 1 if any; length written into the block |
| `$204E` | TCP_CLOSE | Z = socket | |
| `$2051` | TCP_ABORT | Z = socket | |
| `$2054` | TCP_LISTEN | A/X = port, Z = socket | 1 listening |
| `$2057` | TCP_PEER | A/X = block, Z = socket | 6 bytes: who connected |
| `$205D` | UDP_OPEN | A/X = port | socket number, or 255 |
| `$2063` | UDP_SEND | A/X = block {ip, port lo, hi, addr lo, hi, 0, len lo, hi}, Z = socket | 1 sent, 2 try again |
| `$2066` | UDP_RECV | A/X = block {addr lo, hi, 0, cap lo, hi, then 8 bytes out}, Z = socket | 1 if a datagram was waiting |

TCP states: 0 closed, 1 connecting, **2 connected**, 5 the other side
has finished sending (you can still read what arrived, then close), 9
listening. Flags at `$1607` after TCP_STATE: 1 = the other side is done
sending, 2 = it reset the connection, 4 = it stopped answering, 8 = it
refused the connection.

The socket number goes in `Z`. You have eight (0 to 7). If you only ever
use one, leave `Z=0` and forget about it.

## 6. Recipes

### Look up a name

Domain names are case-insensitive, so PETSCII's upper/lower muddle
doesn't matter here. The name must end with a zero byte.

```basic
300 N$="GOPHER.FLOODGAP.COM"
310 FOR I=1 TO LEN(N$):POKE BF+I-1,ASC(MID$(N$,I,1)):NEXT:POKE BF+LEN(N$),0
320 E=$202A:A=BF AND 255:X=INT(BF/256):Y=0:Z=0:GOSUB 1000
330 E=$200F:GOSUB 1000:E=$202D:GOSUB 1000:IF R=1 THEN 330
340 IF R<>2 THEN PRINT "NOT FOUND":END
350 E=$2030:GOSUB 1000:H0=PEEK($1606):H1=PEEK($1607):H2=PEEK($1608):H3=PEEK($1609)
360 PRINT "ADDRESS";H0;H1;H2;H3
```

### Fetch a gopher menu and print it

Gopher is the simplest protocol there is: connect to port 70, send a
selector and a line ending, read until the server closes. This reads
the root menu of the site resolved above.

```basic
400 REM the address block: ip, then the port (70) low byte first
410 POKE BF,H0:POKE BF+1,H1:POKE BF+2,H2:POKE BF+3,H3:POKE BF+4,70:POKE BF+5,0
420 E=$2042:A=BF AND 255:X=INT(BF/256):Y=0:Z=0:GOSUB 1000
430 T=TI
440 E=$200F:GOSUB 1000:E=$2045:GOSUB 1000
450 IF R=2 THEN 500
460 IF R=0 THEN PRINT "CONNECTION FAILED, FLAGS";PEEK($1607):END
470 IF TI-T<750 THEN 440
480 PRINT "NO ANSWER":END
500 REM send the request: a bare line ending asks for the root menu
510 SB=BF+$100:POKE SB,13:POKE SB+1,10
520 POKE BF+$10,SB AND 255:POKE BF+$11,INT(SB/256):POKE BF+$12,0:POKE BF+$13,2:POKE BF+$14,0
530 E=$2048:A=(BF+$10) AND 255:X=INT((BF+$10)/256):GOSUB 1000
540 REM receive into RB, 200 bytes at a time, until the server is done
550 RB=BF+$200
560 POKE BF+$20,RB AND 255:POKE BF+$21,INT(RB/256):POKE BF+$22,0:POKE BF+$23,200:POKE BF+$24,0
570 E=$200F:GOSUB 1000
580 E=$204B:A=(BF+$20) AND 255:X=INT((BF+$20)/256):GOSUB 1000
590 L=PEEK(BF+$25)+256*PEEK(BF+$26)
600 IF L=0 THEN 640
610 FOR I=0 TO L-1:C=PEEK(RB+I):GOSUB 2000:NEXT
620 GOTO 570
640 E=$2045:GOSUB 1000:IF (PEEK($1607) AND 1)=0 AND R=2 THEN 570
650 E=$204E:GOSUB 1000:PRINT:PRINT "DONE":END
2000 REM print one ASCII byte: swap letter cases for PETSCII, drop CR
2010 IF C=13 THEN RETURN
2020 IF C=10 THEN PRINT:RETURN
2030 IF C>=65 AND C<=90 THEN C=C+32:GOTO 2050
2040 IF C>=97 AND C<=122 THEN C=C-32
2050 PRINT CHR$(C);:RETURN
```

The loop at 570 keeps polling and reading until the server has finished
(flag 1) and nothing is left. Notice the order: read everything first,
*then* close. Closing early throws away whatever was still in the
stack's buffer.

Line 2000 exists because the network speaks ASCII and the MEGA65 speaks
PETSCII, where the letter cases are the other way round. Do `PRINT
CHR$(14)` once at the start of your program to switch to the
upper/lower-case character set, and the swap above prints text as sent.

### Send a message to another computer with UDP

UDP is one datagram, no connection. On the other computer, this Python
one-liner prints whatever arrives on port 5000:

```
python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('',5000));print(s.recvfrom(1024))"
```

On the MEGA65 (with that computer's address in P0-P3):

```basic
700 E=$205D:A=5000 AND 255:X=INT(5000/256):Y=0:Z=0:GOSUB 1000:U=R
710 IF U=255 THEN PRINT "NO FREE SOCKET":END
720 M$="HELLO FROM THE MEGA65":MB=BF+$300
730 FOR I=1 TO LEN(M$):POKE MB+I-1,ASC(MID$(M$,I,1)):NEXT
740 POKE BF,P0:POKE BF+1,P1:POKE BF+2,P2:POKE BF+3,P3:POKE BF+4,5000 AND 255:POKE BF+5,INT(5000/256)
750 POKE BF+6,MB AND 255:POKE BF+7,INT(MB/256):POKE BF+8,0:POKE BF+9,LEN(M$):POKE BF+10,0
760 E=$2063:A=BF AND 255:X=INT(BF/256):Y=0:Z=U:GOSUB 1000
770 IF R=2 THEN E=$200F:GOSUB 1000:GOTO 760: REM the stack is finding the other machine; send again
780 PRINT "SENT"
```

The first send to a machine may return 2: the stack has asked the
network who owns that address and will know by the next poll. Just
send again.

The text arrives as `HELLO FROM THE MEGA65`: a string you type in BASIC
is PETSCII, whose letters `POKE` out as ASCII capitals. To send lower
case, add 32 to each letter between 65 and 90 as you poke it.

### Let another computer connect to you

A listening socket waits for a caller. This one echoes back whatever
arrives, and takes the next caller when the first hangs up. Test it
from any computer with `nc 192.168.1.xx 6400` (or `telnet`).

```basic
800 E=$2054:A=6400 AND 255:X=INT(6400/256):Y=0:Z=0:GOSUB 1000
810 PRINT "LISTENING ON PORT 6400"
820 RB=BF+$200
830 E=$200F:GOSUB 1000
840 E=$2045:GOSUB 1000:S=R:F=PEEK($1607)
850 IF S=9 THEN 830:                         REM still waiting
860 IF S=0 THEN PRINT "CALLER GONE":GOTO 800: REM hung up: listen again
870 POKE BF+$20,RB AND 255:POKE BF+$21,INT(RB/256):POKE BF+$22,0:POKE BF+$23,200:POKE BF+$24,0
880 E=$204B:A=(BF+$20) AND 255:X=INT((BF+$20)/256):GOSUB 1000
890 L=PEEK(BF+$25)+256*PEEK(BF+$26)
900 IF L>0 THEN POKE BF+$10,RB AND 255:POKE BF+$11,INT(RB/256):POKE BF+$12,0:POKE BF+$13,L:POKE BF+$14,0:E=$2048:A=(BF+$10) AND 255:X=INT((BF+$10)/256):GOSUB 1000
910 IF (F AND 1) AND L=0 THEN E=$204E:GOSUB 1000: REM they finished: close our side
920 GOTO 830
```

For a two-line service, listen on sockets 0 and 1 with the same port and
run this loop for each. A third caller while both are busy is told "no"
immediately, which is much friendlier than a silence.

## 7. Things that will bite you once

- **`RREG` is wrong here.** Use `PEEK($1606)`.
- **Keep polling.** A `GET` loop that waits for a key should call
  `POLL` each time round: `830 GET K$:E=$200F:GOSUB 1000:IF K$="" THEN 830`.
- **Case.** ASCII and PETSCII disagree about letters. Use line 2000
  above, or `CHR$(14)` and live with it.
- **`petcat`** (VICE's tokenizer, if you keep programs as text) mangles
  a hex number that ends in `E`, like `$160E`. Write `5646` instead.
- **DHCP takes a moment**, and needs a router. On a cable straight to a
  PC with no DHCP server, use `SET_LOCAL_IP4` (`E=$201B` with the four
  bytes in A, X, Y, Z) and skip the DHCP wait.
- **State 4 (DHCP failed) or DNS state 3** after a long wait usually
  means the cable, not the program.

## 8. What the stack uses, so your program doesn't

| Where | What |
|---|---|
| bank 0, `$1600-$16FF` | the trampoline. Never `POKE` here except through the subroutine |
| bank 4, `$42000-$4BFFF` | the stack itself |
| bank 4, `$4FFF0-$4FFFF` | the stack's interrupt vectors during a call |
| bank 5, `$50000-$5E8FF` | the stack's network buffers. **BASIC's bitmap graphics commands also draw in bank 5**, so a program that uses both will fight over it |

Your parameter blocks go anywhere else in bank 0 above your program
text. `$6000` is safe for a program under about 14 KB; a bigger program
should use somewhere higher, up to `$F600`.

## 9. Where to go next

- `docs/ABI.md` has every entry and every byte of every block.
- `tools/basic/guide.bas` is every recipe above in one program, run on
  the MEGA65 exactly as printed here; `tools/basic_d81.py` puts it on
  the disk as `GUIDE` next to `MNBASIC`, the stack's own test program.
  `RUN "GUIDE"` and read along.
- The C guide (`docs/GUIDE-C-ASM.md`) is the same stack from the other
  side, if BASIC starts to feel slow.
