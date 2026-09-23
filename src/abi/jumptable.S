; The mega-net ABI: a jump table at $2000 (physical $42000) and the
; prologue/epilogue every entry passes through.
;
; Calling convention, from either BASIC (SYS $42xxx,a,x,y,z / RREG) or a
; C or assembly caller through the trampoline: arguments in A/X/Y/Z,
; results in A/X/Y/Z. Where an entry needs more than four bytes, A/X/Y
; hold a 28-bit pointer (lo/mid/hi) to a parameter block in the caller's
; memory, which the stack reads and writes back by DMA. The stack never
; reads the caller's memory through the CPU: while it runs, the caller's
; $2000-$7FFF is not there.
;
; Entries are appended, never reordered. The table is the ABI.

        .section .jumptable,"ax",@progbits
        .global mn_jumptable
mn_jumptable:
        jmp     mn_entry_init           ; $42000  init; A=0 ok
        jmp     mn_entry_version        ; $42003  A=major X=minor Y='M' Z='N'
        jmp     mn_entry_get_mac        ; $42006  A/X/Y -> 6-byte buffer
        jmp     mn_entry_link_tx        ; $42009  A/X/Y -> {ptr28, len16}; A=1 sent
        jmp     mn_entry_link_rx        ; $4200C  A/X/Y -> {ptr28, cap16, len16}; A=1 got
        jmp     mn_entry_poll           ; $4200F  network pump: A=1 if a frame was handled
        jmp     mn_entry_set_ip         ; $42012  A/X/Y -> {ip4, mask4, gw4}
        jmp     mn_entry_get_ip         ; $42015  A/X/Y -> {ip4, mask4, gw4} out
        jmp     mn_entry_get_stats      ; $42018  A/X/Y -> {rx, arp_replies, echo_replies, tx_fail} u16 each
        jmp     mn_entry_set_local_ip4  ; $4201B  A,X,Y,Z = ip (BASIC: SYS addr,a,b,c,d)
        jmp     mn_entry_dhcp_start     ; $4201E  start DHCP; A=0
        jmp     mn_entry_dhcp_state     ; $42021  A = 0 idle 1 selecting 2 requesting 3 bound 4 failed; X = phase, Y/Z = lease left (min)
        jmp     mn_entry_get_dns        ; $42024  A/X/Y -> 4-byte DNS server address
        jmp     mn_entry_set_dns        ; $42027  A,X,Y,Z = DNS server (static setups)
        jmp     mn_entry_dns_start      ; $4202A  A/X/Y -> NUL-terminated name; A=0 started
        jmp     mn_entry_dns_state      ; $4202D  A = 0 idle 1 waiting 2 done 3 failed
        jmp     mn_entry_dns_result     ; $42030  A,X,Y,Z = the address (RREG A,X,Y,Z)
        jmp     mn_entry_ntp_start      ; $42033  A,X,Y,Z = server address
        jmp     mn_entry_ntp_state      ; $42036  A = 0 idle 1 waiting 2 done 3 failed
        jmp     mn_entry_ntp_result     ; $42039  A/X/Y -> {offset_min i16 in; secs u32, year u16, mon, day, hr, min, sec, wday out}
        jmp     mn_entry_set_rtc        ; $4203C  A/X/Y -> {year u16, mon, day, hr, min, sec, wday}; A=0
        jmp     mn_entry_dhcp_last_msg  ; $4203F  A/X/Y -> 300 bytes: the DHCP module's message buffer (diagnostic)
        ; TCP: Z = the socket index (0..7); a caller that leaves Z at 0 has
        ; socket 0, as before socket sets (5.11). An index past the end: A=$FF.
        jmp     mn_entry_tcp_connect    ; $42042  A/X/Y -> {ip4, port u16}; A=1 started
        jmp     mn_entry_tcp_state      ; $42045  A=state X=flags Y/Z=bytes available lo/hi
        jmp     mn_entry_tcp_send       ; $42048  A/X/Y -> {ptr28, len u16}; A/X = accepted lo/hi
        jmp     mn_entry_tcp_recv       ; $4204B  A/X/Y -> {ptr28, cap u16, len u16 out}; A=1 if any
        jmp     mn_entry_tcp_close      ; $4204E  graceful close
        jmp     mn_entry_tcp_abort      ; $42051  RST and drop
        jmp     mn_entry_tcp_listen     ; $42054  A/X = port; A=1 listening (state 9)
        jmp     mn_entry_tcp_peer       ; $42057  A/X/Y -> {ip4, port u16} out: the peer
        jmp     mn_entry_tcp_info       ; $4205A  A=sockets X=buffers (1 attic, 2 bank 5) Y/Z=base>>12
        ; UDP sockets for applications: Z = the socket index from UDP_OPEN.
        jmp     mn_entry_udp_open       ; $4205D  A/X = port; A = socket or $FF
        jmp     mn_entry_udp_close      ; $42060  close socket Z
        jmp     mn_entry_udp_send       ; $42063  A/X/Y -> {ip4, port u16, ptr28, len u16}; A = 0 failed 1 sent 2 resolving
        jmp     mn_entry_udp_recv       ; $42066  A/X/Y -> {ptr28, cap u16; out: len u16, src ip4, src port u16}; A=1 if any

; --- register mailboxes: the C side reads mn_api_a..z and writes
; mn_api_ra..rz, so no C calling convention is assumed. .noinit because
; init zeroes .bss and these must survive that. ---
        .section .noinit,"aw",@nobits
        .global mn_api_a, mn_api_x, mn_api_y, mn_api_z
        .global mn_api_ra, mn_api_rx, mn_api_ry, mn_api_rz
mn_api_a:  .skip 1
mn_api_x:  .skip 1
mn_api_y:  .skip 1
mn_api_z:  .skip 1
mn_api_ra: .skip 1
mn_api_rx: .skip 1
mn_api_ry: .skip 1
mn_api_rz: .skip 1
mn_zp_caller: .skip 0x70        ; the caller's $90-$FF while we run
mn_zp_ours:   .skip 0x70        ; our $90-$FF between calls

        .section .text,"ax",@progbits

; Each entry stashes the registers, runs the common prologue, calls its C
; function, and leaves through the epilogue.
        .macro ENTRY name, cfunc
        .global \name
\name:
        sta     mn_api_a
        stx     mn_api_x
        sty     mn_api_y
        stz     mn_api_z
        ldz     #0              ; our C stores zero with STZ: Z must be 0
        php                     ; BASIC's SYS leaves the KERNAL IRQ running,
        sei                     ; and it uses $90-$FF -- our zero page.
        jsr     mn_prologue
        jsr     \cfunc
        jmp     mn_epilogue
        .endm

        ENTRY mn_entry_init,    mn_api_init
        ENTRY mn_entry_version, mn_api_version
        ENTRY mn_entry_get_mac, mn_api_get_mac
        ENTRY mn_entry_link_tx, mn_api_link_tx
        ENTRY mn_entry_link_rx, mn_api_link_rx
        ENTRY mn_entry_poll,    mn_api_poll
        ENTRY mn_entry_set_ip,  mn_api_set_ip
        ENTRY mn_entry_get_ip,  mn_api_get_ip
        ENTRY mn_entry_get_stats, mn_api_get_stats
        ENTRY mn_entry_set_local_ip4, mn_api_set_local_ip4
        ENTRY mn_entry_dhcp_start, mn_api_dhcp_start
        ENTRY mn_entry_dhcp_state, mn_api_dhcp_state
        ENTRY mn_entry_get_dns,    mn_api_get_dns
        ENTRY mn_entry_set_dns,    mn_api_set_dns
        ENTRY mn_entry_dns_start,  mn_api_dns_start
        ENTRY mn_entry_dns_state,  mn_api_dns_state
        ENTRY mn_entry_dns_result, mn_api_dns_result
        ENTRY mn_entry_ntp_start,  mn_api_ntp_start
        ENTRY mn_entry_ntp_state,  mn_api_ntp_state
        ENTRY mn_entry_ntp_result, mn_api_ntp_result
        ENTRY mn_entry_set_rtc,    mn_api_set_rtc
        ENTRY mn_entry_dhcp_last_msg, mn_api_dhcp_last_msg
        ENTRY mn_entry_tcp_connect, mn_api_tcp_connect
        ENTRY mn_entry_tcp_state,   mn_api_tcp_state
        ENTRY mn_entry_tcp_send,    mn_api_tcp_send
        ENTRY mn_entry_tcp_recv,    mn_api_tcp_recv
        ENTRY mn_entry_tcp_close,   mn_api_tcp_close
        ENTRY mn_entry_tcp_abort,   mn_api_tcp_abort
        ENTRY mn_entry_tcp_listen,  mn_api_tcp_listen
        ENTRY mn_entry_tcp_peer,    mn_api_tcp_peer
        ENTRY mn_entry_tcp_info,    mn_api_tcp_info
        ENTRY mn_entry_udp_open,    mn_api_udp_open
        ENTRY mn_entry_udp_close,   mn_api_udp_close
        ENTRY mn_entry_udp_send,    mn_api_udp_send
        ENTRY mn_entry_udp_recv,    mn_api_udp_recv

; The status register, for the crash breadcrumbs (5.11).
        .global mn_get_p
mn_get_p:
        php
        pla
        ldx     #0
        rts

; SWAP the zero page: the caller's $90-$FF goes to safety and ours comes
; back. Ours must persist between calls -- the compiler keeps statics
; there (the netif's function pointers, the DHCP machine) -- so merely
; restoring the caller's copy on exit, as an earlier version did, wiped
; our state every call (REQUIREMENTS.md 5.6). The caller's own $02-$8F
; is never touched. Then the soft stack goes to the top of our window.
mn_prologue:
        ; The MEGA65 I/O personality, every time: a client's screen or
        ; disk code may have selected another, and then the ethernet
        ; controller's registers are not at $D6E0 (REQUIREMENTS.md 5.9).
        lda     #0x47
        sta     0xD02F
        lda     #0x53
        sta     0xD02F
        ldx     #0
1:      lda     $90,x
        sta     mn_zp_caller,x
        lda     mn_zp_ours,x
        sta     $90,x
        inx
        cpx     #0x70
        bne     1b
        lda     #0x00
        sta     __rc0
        lda     #0xC0                   ; soft stack: $BC00-$BFFF, grows down
        sta     __rc1
        rts

; Swap back: ours to safety, the caller's into place. Then the results.
mn_epilogue:
        ldx     #0
1:      lda     $90,x
        sta     mn_zp_ours,x
        lda     mn_zp_caller,x
        sta     $90,x
        inx
        cpx     #0x70
        bne     1b
        lda     mn_api_ra
        ldx     mn_api_rx
        ldy     mn_api_ry
        ldz     mn_api_rz
        plp                     ; caller's I flag back
        rts
