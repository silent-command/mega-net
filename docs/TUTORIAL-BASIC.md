# Your first internet program, in BASIC 65

This is for someone who can write a BASIC program with `PRINT`, `IF`,
`GOTO`, `GOSUB` and variables, and has never used `PEEK`, `POKE` or
`SYS`. By the end you will have a program of about sixty lines that
connects your MEGA65 to the internet, asks a server on the other side
of the world for a page of text, and prints it on the screen. Every
line is explained. The finished program is `tools/basic/tutorial.bas`,
and the disk that `python3 tools/basic_d81.py` builds carries it as
`TUTORIAL`, so you can run it before you type a single line.

If you already know your way around, `GUIDE-BASIC.md` is shorter and
has more recipes. This one goes slowly on purpose.

## 1. The four ideas

**A helper program does the hard work.** Talking to the internet means
speaking several protocols at once, with exact timing, and BASIC is far
too slow to do it. So a separate program, the mega-net stack, is loaded
into memory next to yours. It knows how to talk to the network. Your
BASIC program does not talk to the network at all: it talks to the
helper, and the helper talks to the network.

**Memory is a row of numbered boxes.** Every box holds a number from 0
to 255, and every box has an address. `PEEK(address)` reads a box.
`POKE address, value` writes one. That is all they do. The helper and
your program share the same memory, so you can leave a number in a box
and the helper can read it, and the helper can leave an answer in a box
for you.

**`$` means a hexadecimal number.** Programmers write addresses in
base sixteen because memory is laid out in powers of two. BASIC 65
accepts them directly: `$1600` is the same number as 5632. You never
need to convert; just use the `$` form where this tutorial does.

**`SYS` runs machine code.** `SYS $160E` jumps to the helper's code at
that address, the helper does one job, and comes back to your program.
Which job it does, and with what, is decided by the numbers you put in
a few boxes first. Those boxes are called the mailbox.

That is the whole mechanism: `POKE` a request into the mailbox, `SYS`
into the helper, `PEEK` the answer out. One subroutine, written once,
does it every time.

## 2. Loading the helper

Two files must be on the disk your program runs from. `TRAMP` is a
tiny piece of machine code that your `SYS` goes to; it is the
doorway. `MEGANET` is the helper itself, about 32 KB. They load with
`BLOAD`, which puts a file into memory at the address stored in it.

```basic
10 bank 0
20 bload "tramp",b0
30 bload "meganet",b4
```

Line 10 tells BASIC that the addresses you will `PEEK` and `POKE` are
in bank 0, the ordinary memory your program lives in. The MEGA65 has
several banks of 64 KB; the doorway goes in bank 0 (`b0`) where you can
reach it, and the helper goes in bank 4 (`b4`) where it is out of your
way.

```basic
40 bf=$6000:print chr$(14)
```

`BF` is a place in memory we will use as a scratch area: a few hundred
boxes starting at `$6000`, above your program, that nothing else is
using. Some requests need more than four numbers, and they go there.
`PRINT CHR$(14)` switches the screen to the character set that has both
capital and small letters, so the text we fetch reads properly.

## 3. The one subroutine

Put this at the end of the program and never change it. Every request
to the helper goes through it.

```basic
1000 poke $1600,e and 255:poke $1601,int(e/256)
1010 poke $1602,a:poke $1603,x:poke $1604,y:poke $1605,z
1020 sys $160e
1030 r=peek($1606):return
```

Before you `GOSUB 1000` you set five variables:

- `E` is which job you want: a number from the table in section 9.
- `A`, `X`, `Y` and `Z` are up to four numbers to go with it. Jobs that
  need fewer ignore the rest; set them to 0.

Line 1000 puts the job number into two boxes, `$1600` and `$1601`. A
box holds only 0 to 255 and a job number is bigger than that, so it is
split: `E AND 255` is the low part, `INT(E/256)` the high part. You will
see this split whenever an address or a count goes into boxes.

Line 1010 puts your four numbers in the next four boxes. Line 1020
knocks on the door. Line 1030 reads the answer from box `$1606` into
`R`. Some jobs leave more than one answer, in `$1607`, `$1608` and
`$1609`; the tutorial reads those when it needs them.

One warning that saves an afternoon: BASIC 65 has a command `RREG` that
reads the processor's registers after a `SYS`. Do not use it here. The
doorway's own housekeeping is in the registers by then; the answer is
in the boxes.

## 4. Getting online

Two jobs, done once at the start:

```basic
50 print "starting the network"
60 e=$2000:a=0:x=0:y=0:z=0:gosub 1000
70 e=$201e:gosub 1000
```

Job `$2000` is INIT: the helper sets itself up. It must be the first
job you ask for. Job `$201E` is DHCP_START. DHCP is how a computer gets
an address on a network: it shouts "who can give me an address?", and
the router answers with one. Every device on your home network got its
address this way. The helper sends the shout; the answer takes a
moment to arrive.

Which brings the most important idea in this tutorial.

## 5. Polling: the helper only works when you call it

The helper does nothing on its own. It cannot: while your BASIC program
is running, the processor is running BASIC. The helper reads the
network and keeps its clocks only when you ask it to, with job `$200F`,
POLL. So every wait in a network program is a loop that polls:

```basic
100 t=ti
110 e=$200f:gosub 1000
120 e=$2021:gosub 1000
130 if r=3 then 200
140 if r=4 then print "no dhcp server: is the cable in?":end
150 if ti-t<600 then 110
160 print "no answer from the router":end
```

Line 110 polls. Line 120 asks job `$2021`, DHCP_STATE: how is the
address request going? The answer in `R` is 3 when an address has been
granted, 4 when the request has failed, and 1 or 2 while it is still
in progress. Line 130 moves on when we have one. Line 150 goes round
again, but not forever: `TI` is BASIC's clock, counting sixtieths of a
second, so `TI-T<600` means "for ten seconds". A network program never
waits without a limit, because the other side may simply never answer.

A loop like this polls a few hundred times a second, which is plenty.
The rule to remember: whenever your program is waiting for anything
from the network, it must be polling while it waits.

## 6. Reading your own address

```basic
200 e=$2015:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
210 print "my address is";peek(bf);".";peek(bf+1);".";peek(bf+2);".";peek(bf+3)
```

Job `$2015` is GET_IP, and it is the first job whose answer is bigger
than four numbers: an internet address is four numbers, and the helper
also reports the netmask and the gateway, twelve in all. So instead of
answering in the mailbox, it writes them into memory at a place you
choose, and you tell it where in `A` and `X`: the address of `BF`,
split into its low and high parts the same way as before. This is
called a parameter block: a form in memory that you fill in, or the
helper fills in, when four numbers are not enough.

Line 210 prints the four boxes at `BF`, which now hold your address,
something like `192.168.1.57`. Everything on the internet is reached by
a number like that.

## 7. Turning a name into a number

Nobody remembers numbers; we type names. Turning `gopher.floodgap.com`
into its number is the job of DNS, and the helper does it for you.

```basic
300 n$="gopher.floodgap.com"
310 for i=1 to len(n$):poke bf+i-1,asc(mid$(n$,i,1)):next:poke bf+len(n$),0
320 e=$202a:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
330 e=$200f:gosub 1000:e=$202d:gosub 1000:if r=1 then 330
340 if r<>2 then print "no such name":end
350 e=$2030:gosub 1000:h0=peek($1606):h1=peek($1607):h2=peek($1608):h3=peek($1609)
360 print n$;" is";h0;".";h1;".";h2;".";h3
```

Line 310 copies the name into the scratch area one letter at a time:
`ASC` turns a letter into its number, `POKE` puts it in the next box.
The `POKE ... ,0` at the end puts a zero after the last letter, which
is how the helper knows where the name stops. (Names are not case
sensitive, so it does not matter that BASIC's letters come out as
capitals.)

Line 320, job `$202A`, DNS_START, tells the helper where the name is.
The answer has to come from a server on the internet, so line 330 polls
and asks job `$202D`, DNS_STATE, until it stops saying 1, "still
waiting". Then 2 means found, and line 350, job `$2030`, DNS_RESULT,
hands over the four numbers in the mailbox boxes `$1606` to `$1609`.
We keep them in `H0` to `H3`.

## 8. Connecting, asking, and reading the answer

Now the real thing. The server we are talking to speaks gopher, the
simplest protocol on the internet: you connect to it on port 70, send
a line, and it sends back a page of text and hangs up. That is the
whole protocol, and it is why it makes a good first program.

A port is a number that says which program on the server you want to
talk to; web servers listen on 80 and 443, gopher servers on 70.

### Connecting

```basic
400 poke bf,h0:poke bf+1,h1:poke bf+2,h2:poke bf+3,h3:poke bf+4,70:poke bf+5,0
410 e=$2042:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
420 t=ti
430 e=$200f:gosub 1000:e=$2045:gosub 1000
440 if r=2 then 500
450 if r=0 then print "could not connect, reason";peek($1607):end
460 if ti-t<750 then 430
470 print "the server did not answer":end
```

Line 400 fills in a parameter block: the four numbers of the address,
then the port, low part first (`70` then `0`, because 70 is less than
256). Line 410, job `$2042`, TCP_CONNECT, starts the connection. TCP is
the way two computers hold a conversation on the internet: a
connection is opened, bytes flow in both directions in order, and it is
closed. Opening one takes a round trip across the network, so lines
420 to 470 poll and ask job `$2045`, TCP_STATE, until the state is 2,
"connected". A state of 0 means it failed, and box `$1607` says why:
8 if the server refused, 4 if it never answered, 2 if it hung up.

`Z` was 0 in every call: the helper can hold eight conversations at
once, numbered 0 to 7, and `Z` says which. We only ever use one.

### Asking

```basic
500 print "connected, asking for the front page":print
510 sb=bf+$100:poke sb,13:poke sb+1,10
520 poke bf+$10,sb and 255:poke bf+$11,int(sb/256):poke bf+$12,0:poke bf+$13,2:poke bf+$14,0
530 e=$2048:a=(bf+$10) and 255:x=int((bf+$10)/256):gosub 1000
```

A gopher request is a line: the name of the page you want, then a line
ending. An empty name means the front page, so the whole request is
the two bytes of a line ending, 13 and 10, which line 510 puts in
memory at `SB`. Line 520 fills in the parameter block for sending: the
address of the bytes (low, high, then a 0 that means bank 0), and how
many there are (2, low and high). Line 530, job `$2048`, TCP_SEND,
sends them.

### Reading the answer

```basic
600 rb=bf+$200
610 poke bf+$20,rb and 255:poke bf+$21,int(rb/256):poke bf+$22,0:poke bf+$23,200:poke bf+$24,0
620 e=$200f:gosub 1000
630 e=$204b:a=(bf+$20) and 255:x=int((bf+$20)/256):gosub 1000
640 l=peek(bf+$25)+256*peek(bf+$26)
650 if l=0 then 700
660 for i=0 to l-1:c=peek(rb+i):gosub 2000:next
670 goto 620
700 e=$2045:gosub 1000:if (peek($1607) and 1)=0 and r=2 then 620
710 e=$204e:gosub 1000
720 print:print "done":end
```

The page comes back in pieces, and we read it 200 bytes at a time into
`RB`. Line 610 fills in the receiving block: where to put the bytes,
and at most how many (200). Line 620 polls, so the helper can take in
whatever has arrived. Line 630, job `$204B`, TCP_RECV, copies up to 200
bytes into `RB` and writes how many it copied into the last two boxes
of the block; line 640 reads that count. If there were some, line 660
prints them one by one through the subroutine at 2000 and line 670
goes round for more.

When nothing was waiting, line 700 asks the state again. If the server
is still connected and has not said it is finished, we go back and
keep polling: an empty read means "nothing yet", not "nothing more".
Box `$1607` has bit 1 set when the server has finished sending. Only
then does line 710, job `$204E`, TCP_CLOSE, close our side. The order
matters: read everything first, then close. Closing early throws away
whatever the helper still held.

### Printing the text

```basic
2000 if c=13 then return
2010 if c=10 then print:return
2020 if c>=65 and c<=90 then c=c+32:goto 2040
2030 if c>=97 and c<=122 then c=c-32
2040 print chr$(c);:return
```

The internet writes text in ASCII and the MEGA65 in PETSCII, and the
two agree on almost everything except that the capital and small
letters are swapped. This subroutine swaps them back, prints a newline
for the byte 10, and ignores the byte 13, which ASCII uses alongside
it.

## 9. The jobs used here

| E | Job | What you set | What comes back |
|---|---|---|---|
| `$2000` | INIT | nothing | `R` = 0 |
| `$201E` | DHCP_START | nothing | |
| `$200F` | POLL | nothing | |
| `$2021` | DHCP_STATE | nothing | `R`: 1 or 2 working, 3 online, 4 failed |
| `$2015` | GET_IP | `A`,`X` = a block | 12 bytes at the block: address, mask, gateway |
| `$202A` | DNS_START | `A`,`X` = a block with the name and a zero | |
| `$202D` | DNS_STATE | nothing | `R`: 1 waiting, 2 found, 3 failed |
| `$2030` | DNS_RESULT | nothing | the four numbers in `$1606` to `$1609` |
| `$2042` | TCP_CONNECT | `A`,`X` = a block: address, port low, port high; `Z` = 0 | |
| `$2045` | TCP_STATE | `Z` = 0 | `R`: 1 connecting, 2 connected, 0 failed; `$1607` why |
| `$2048` | TCP_SEND | `A`,`X` = a block: where, 0, how many | |
| `$204B` | TCP_RECV | `A`,`X` = a block: where, 0, at most how many, then two spare boxes | how many, in those two boxes |
| `$204E` | TCP_CLOSE | `Z` = 0 | |

`GUIDE-BASIC.md` has the full table, including UDP and how to let
another computer connect to you.

## 10. Running it

The quickest way is the disk this project builds: `python3
tools/basic_d81.py` in the mega-net folder makes `build/basic/MNBASIC.D81`
with `TRAMP`, `MEGANET` and this program as `TUTORIAL`. Copy the disk
to your SD card, then at the BASIC prompt:

```
MOUNT "MNBASIC.D81"
RUN "TUTORIAL"
```

You should see your address, then the server's, then the front page of
`gopher.floodgap.com` scrolling up the screen, and `done`. If you would
rather type the program yourself, type it from the listing in section
11 onto a disk that has `TRAMP` and `MEGANET` on it, and `SAVE` it
before you run it; a typo in line 1000 is the commonest reason for a
program that does nothing.

If it stops:

- `no dhcp server` or `no answer from the router`: the cable, or the
  router is not handing out addresses.
- `no such name`: a typo in the name, or DNS is not reachable.
- `could not connect, reason 8`: the server refused; `4`: it never
  answered, which is usually a firewall or a wrong port.
- It prints nothing and sits there: it is waiting somewhere without
  polling. Every wait must have a `E=$200F:GOSUB 1000` in it.

## 11. The whole program

```basic
10 bank 0
20 bload "tramp",b0
30 bload "meganet",b4
40 bf=$6000:print chr$(14)
50 print "starting the network"
60 e=$2000:a=0:x=0:y=0:z=0:gosub 1000
70 e=$201e:gosub 1000
100 t=ti
110 e=$200f:gosub 1000
120 e=$2021:gosub 1000
130 if r=3 then 200
140 if r=4 then print "no dhcp server: is the cable in?":end
150 if ti-t<600 then 110
160 print "no answer from the router":end
200 e=$2015:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
210 print "my address is";peek(bf);".";peek(bf+1);".";peek(bf+2);".";peek(bf+3)
300 n$="gopher.floodgap.com"
310 for i=1 to len(n$):poke bf+i-1,asc(mid$(n$,i,1)):next:poke bf+len(n$),0
320 e=$202a:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
330 e=$200f:gosub 1000:e=$202d:gosub 1000:if r=1 then 330
340 if r<>2 then print "no such name":end
350 e=$2030:gosub 1000:h0=peek($1606):h1=peek($1607):h2=peek($1608):h3=peek($1609)
360 print n$;" is";h0;".";h1;".";h2;".";h3
400 poke bf,h0:poke bf+1,h1:poke bf+2,h2:poke bf+3,h3:poke bf+4,70:poke bf+5,0
410 e=$2042:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
420 t=ti
430 e=$200f:gosub 1000:e=$2045:gosub 1000
440 if r=2 then 500
450 if r=0 then print "could not connect, reason";peek($1607):end
460 if ti-t<750 then 430
470 print "the server did not answer":end
500 print "connected, asking for the front page":print
510 sb=bf+$100:poke sb,13:poke sb+1,10
520 poke bf+$10,sb and 255:poke bf+$11,int(sb/256):poke bf+$12,0:poke bf+$13,2:poke bf+$14,0
530 e=$2048:a=(bf+$10) and 255:x=int((bf+$10)/256):gosub 1000
600 rb=bf+$200
610 poke bf+$20,rb and 255:poke bf+$21,int(rb/256):poke bf+$22,0:poke bf+$23,200:poke bf+$24,0
620 e=$200f:gosub 1000
630 e=$204b:a=(bf+$20) and 255:x=int((bf+$20)/256):gosub 1000
640 l=peek(bf+$25)+256*peek(bf+$26)
650 if l=0 then 700
660 for i=0 to l-1:c=peek(rb+i):gosub 2000:next
670 goto 620
700 e=$2045:gosub 1000:if (peek($1607) and 1)=0 and r=2 then 620
710 e=$204e:gosub 1000
720 print:print "done":end
1000 poke $1600,e and 255:poke $1601,int(e/256)
1010 poke $1602,a:poke $1603,x:poke $1604,y:poke $1605,z
1020 sys $160e
1030 r=peek($1606):return
2000 if c=13 then return
2010 if c=10 then print:return
2020 if c>=65 and c<=90 then c=c+32:goto 2040
2030 if c>=97 and c<=122 then c=c-32
2040 print chr$(c);:return
```

## 12. Where to go from here

Change line 300 to another gopher server, or send a page name instead
of an empty line: put `"/"` followed by the name into `SB` and set the
count in line 520 to its length plus two. `GUIDE-BASIC.md` shows how to
send a message to another computer with UDP, and how to make your
MEGA65 answer when another computer connects to it, which is the
beginning of a server. The subroutine at 1000 and the polling rule are
the same in every one of them.

The lines of this program are the guide's recipes, which were run on
the machine as one program; the tutorial's own copy has not yet been
run as itself.
