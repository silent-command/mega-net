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
