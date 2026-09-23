10 bank 0
20 bload "tramp",b0
30 bload "meganet",b4
40 bank 4:if peek($2000)<>76 then print "meganet not at $42000":end
50 bank 0:if peek(5646)<>8 then print "trampoline not at $1600":end
60 rem the trampoline restores basic's own sys map after each call (5.10)
100 e=$2003:a=0:x=0:y=0:z=0:gosub 1000
110 print "version";peek($1606);".";peek($1607);" ";chr$(peek($1608));chr$(peek($1609))
120 e=$2000:gosub 1000:print "init";r
130 e=$201e:gosub 1000:print "dhcp start";r
140 t=ti
150 e=$200f:gosub 1000:e=$2021:gosub 1000:if r=3 then 200
160 if r=4 then print "dhcp failed":end
170 if ti-t<900 then 150
180 print "dhcp timeout, state";r:end
200 print "bound after";int((ti-t)/6)/10;"s"
210 e=$2015:a=0:x=$60:y=0:gosub 1000
220 print "ip ";:for i=0 to 3:print peek($6000+i);:next:print
230 print "gw ";:for i=8 to 11:print peek($6000+i);:next:print
240 print "polling 30s":t=ti
250 e=$200f:gosub 1000:if ti-t<1800 then 250
260 print "done"
270 end
1000 poke $1600,e and 255:poke $1601,int(e/256):poke $1602,a:poke $1603,x:poke $1604,y:poke $1605,z
1010 sys 5646:r=peek($1606):return:rem 5646 = $160e, the call
