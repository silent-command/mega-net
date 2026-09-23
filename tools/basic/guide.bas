10 bank 0
20 bload "tramp",b0
30 bload "meganet",b4
50 bf=$6000:print chr$(14)
60 e=$2000:a=0:x=0:y=0:z=0:gosub 1000
70 e=$201e:gosub 1000
110 t=ti
120 e=$200f:gosub 1000
130 e=$2021:gosub 1000
140 if r=3 then 200
150 if r=4 then print "no dhcp server":end
160 if ti-t<600 then 120
170 print "dhcp timed out":end
200 print "online"
210 e=$2015:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
220 print "ip";peek(bf);".";peek(bf+1);".";peek(bf+2);".";peek(bf+3)
300 n$="gopher.floodgap.com"
310 for i=1 to len(n$):poke bf+i-1,asc(mid$(n$,i,1)):next:poke bf+len(n$),0
320 e=$202a:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
330 e=$200f:gosub 1000:e=$202d:gosub 1000:if r=1 then 330
340 if r<>2 then print "not found":end
350 e=$2030:gosub 1000:h0=peek($1606):h1=peek($1607):h2=peek($1608):h3=peek($1609)
360 print "address";h0;h1;h2;h3
410 poke bf,h0:poke bf+1,h1:poke bf+2,h2:poke bf+3,h3:poke bf+4,70:poke bf+5,0
420 e=$2042:a=bf and 255:x=int(bf/256):y=0:z=0:gosub 1000
430 t=ti
440 e=$200f:gosub 1000:e=$2045:gosub 1000
450 if r=2 then 500
460 if r=0 then print "connection failed, flags";peek($1607):end
470 if ti-t<750 then 440
480 print "no answer":end
500 sb=bf+$100:poke sb,13:poke sb+1,10
520 poke bf+$10,sb and 255:poke bf+$11,int(sb/256):poke bf+$12,0:poke bf+$13,2:poke bf+$14,0
530 e=$2048:a=(bf+$10) and 255:x=int((bf+$10)/256):gosub 1000
550 rb=bf+$200:nl=0
560 poke bf+$20,rb and 255:poke bf+$21,int(rb/256):poke bf+$22,0:poke bf+$23,200:poke bf+$24,0
570 e=$200f:gosub 1000
580 e=$204b:a=(bf+$20) and 255:x=int((bf+$20)/256):gosub 1000
590 l=peek(bf+$25)+256*peek(bf+$26)
600 if l=0 then 640
610 for i=0 to l-1:c=peek(rb+i):gosub 2000:next
620 goto 570
640 e=$2045:gosub 1000:if (peek($1607) and 1)=0 and r=2 then 570
650 e=$204e:gosub 1000:print:print "done, lines:";nl
700 e=$205d:a=5000 and 255:x=int(5000/256):y=0:z=0:gosub 1000:u=r
710 if u=255 then print "no free socket":end
720 m$="hello from the mega65":mb=bf+$300
730 for i=1 to len(m$):poke mb+i-1,asc(mid$(m$,i,1)):next
740 poke bf,192:poke bf+1,168:poke bf+2,1:poke bf+3,232:poke bf+4,5000 and 255:poke bf+5,int(5000/256)
750 poke bf+6,mb and 255:poke bf+7,int(mb/256):poke bf+8,0:poke bf+9,len(m$):poke bf+10,0
760 e=$2063:a=bf and 255:x=int(bf/256):y=0:z=u:gosub 1000
770 if r=2 then e=$200f:gosub 1000:goto 760
780 print "udp sent, r=";r
800 e=$2054:a=6400 and 255:x=int(6400/256):y=0:z=0:gosub 1000
810 print "listening on port 6400"
820 rb=bf+$200:ec=0
830 e=$200f:gosub 1000
840 e=$2045:gosub 1000:s=r:f=peek($1607)
850 if s=9 then 830
860 if s=0 then print "caller gone, echoed";ec:goto 800
870 poke bf+$20,rb and 255:poke bf+$21,int(rb/256):poke bf+$22,0:poke bf+$23,200:poke bf+$24,0
880 e=$204b:a=(bf+$20) and 255:x=int((bf+$20)/256):gosub 1000
890 l=peek(bf+$25)+256*peek(bf+$26)
900 if l>0 then poke bf+$10,rb and 255:poke bf+$11,int(rb/256):poke bf+$12,0:poke bf+$13,l:poke bf+$14,0:e=$2048:a=(bf+$10) and 255:x=int((bf+$10)/256):gosub 1000:ec=ec+l
910 if (f and 1) and l=0 then e=$204e:gosub 1000
920 goto 830
1000 poke $1600,e and 255:poke $1601,int(e/256)
1010 poke $1602,a:poke $1603,x:poke $1604,y:poke $1605,z
1020 sys 5646
1030 r=peek($1606):return
2000 if c=13 then return
2010 if c=10 then nl=nl+1:if nl<6 then print
2015 if c=10 then return
2020 if nl>=5 then return
2030 if c>=65 and c<=90 then c=c+32:goto 2050
2040 if c>=97 and c<=122 then c=c-32
2050 print chr$(c);:return
