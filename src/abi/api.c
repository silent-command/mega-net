/* The C side of the ABI. Each function reads its arguments from the
 * mailboxes jumptable.S filled and leaves results in mn_api_r*. Nothing
 * here touches the caller's memory through the CPU -- pointers arriving
 * in A/X/Y are 28-bit physical addresses and are only ever handed to DMA.
 */
#include <stdint.h>
#include "../net/mn_netif.h"
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../hal/mn_eth45e100.h"
#include "../net/mn_net.h"
#include "../net/mn_dhcp.h"
#include "../net/mn_dns.h"
#include "../net/mn_ntp.h"
#include "../net/mn_tcp.h"

extern volatile uint8_t mn_api_a, mn_api_x, mn_api_y, mn_api_z;
extern volatile uint8_t mn_api_ra, mn_api_rx, mn_api_ry, mn_api_rz;

/* Linker-provided: where .bss is and how big. Absolute symbols, so their
 * "addresses" are the values -- the idiom llvm-mos's own zero-bss uses. */
extern char __bss_start[];
extern char __bss_size[];
extern char __zp_bss_start[];
extern char __zp_bss_size[];
extern char __zp_data_start[];
extern char __zp_data_load_start[];
extern char __zp_data_size[];

#define MN_ABI_MAJOR 0
#define MN_ABI_MINOR 2   /* 0.2: socket sets, listeners, UDP entries, DHCP_STATE phase and lease, stats appended (5.11-5.17) */

/* Module state lives in .bss, not zero page. llvm-mos would put anything
 * "small" in zero page, and 80 bytes for each of these is the whole
 * budget; zero page is for the compiler's temporaries. Pinned by name. */
#define IN_BSS(n) __attribute__((section(".bss." n)))
static mn_netif nif IN_BSS("nif");
static mn_net net IN_BSS("net");
static mn_dhcp dhcp IN_BSS("dhcp");
static mn_dns dns IN_BSS("dns");
static mn_ntp ntp IN_BSS("ntp");
#define MN_ABI_TCP_SOCKETS 8
static mn_tcp tcps[MN_ABI_TCP_SOCKETS] IN_BSS("tcps");
static mn_tcp_set tcpset IN_BSS("tcpset");
#ifdef MN_CRUMBS
volatile uint8_t mn_crumb IN_BSS("crumb");
volatile uint8_t mn_crumb_p IN_BSS("crumbp");
#endif

static uint8_t pool_kind IN_BSS("pool");
static volatile uint8_t window_irqs IN_BSS("wirq");   /* interrupts caught during calls */
#ifdef MN_NMI_PROBE
static volatile uint8_t nmi_cia2 IN_BSS("nmic"), nmi_cia1 IN_BSS("nmi1"), nmi_vic IN_BSS("nmiv");
static volatile uint8_t irq_count IN_BSS("irqn"), irq_cia2 IN_BSS("irqc"), irq_cia1 IN_BSS("irq1"), irq_vic IN_BSS("irqv");
#endif      /* 1 attic RAM, 2 bank 5 */
static uint32_t pool_base IN_BSS("poolb");
static char name[MN_DNS_NAME_MAX + 1] IN_BSS("name");

/* A 16-bit tick from the 8-bit frame counter, advanced on every poll.
 * Good enough for protocol timeouts as long as polls come more often
 * than every 256 frames (5 s), which any client loop does. */
static uint16_t ticks IN_BSS("ticks");
static uint8_t last_frame IN_BSS("lastf");

static uint16_t tick(void)
{
    uint8_t f = MN_FRAMECOUNT;
    ticks = (uint16_t)(ticks + (uint8_t)(f - last_frame));
    last_frame = f;
    return ticks;
}
/* Staging for raw frames and UDP payloads: the network layer's receive
 * buffer, idle between polls. The window has no room for a second one. */
#define frame (mn_net_scratch())
static uint8_t blk[40] IN_BSS("blk");

static uint32_t arg_ptr(void)
{
    return (uint32_t)mn_api_a | ((uint32_t)mn_api_x << 8) |
           ((uint32_t)mn_api_y << 16);
}

static void ret(uint8_t a, uint8_t x, uint8_t y, uint8_t z)
{
    mn_api_ra = a; mn_api_rx = x; mn_api_ry = y; mn_api_rz = z;
}

static uint32_t blk_ptr(void)
{
    return (uint32_t)blk[0] | ((uint32_t)blk[1] << 8) |
           ((uint32_t)blk[2] << 16);
}

/* Where the socket buffers go. Bank 5 ($50000-$59FFF): attic RAM was the
 * first choice (5.11), and it fails -- DMA reads of a ring that DMA is
 * also writing came back stale, and a 4 KB document arrived spliced with
 * its own earlier bytes (5.13). Until the attic RAM cache is understood
 * the pool is chip RAM; build with -DMN_POOL_ATTIC=1 to probe attic RAM
 * again, which TCP_INFO then reports as kind 1. */
#ifndef MN_POOL_ATTIC
#define MN_POOL_ATTIC 0
#endif
#define POOL_ATTIC 0x8000000UL
#define POOL_BANK5 0x0050000UL
static void probe_pool(void)
{
#if !MN_POOL_ATTIC
    pool_kind = 2; pool_base = POOL_BANK5; return;
#endif
#ifdef MN_ATTIC_CACHE_CTRL
    { static const uint8_t v = MN_ATTIC_CACHE_CTRL; mn_dma_copy(MN_PHYS(&v), 0xBFFFFF2UL, 1); }  /* experiment (5.15) */
#endif
    static const uint8_t p1[4] = { 0x4D, 0x4E, 0x5A, 0xA5 };
    static const uint8_t p2[4] = { 0xB2, 0xB1, 0xA5, 0x5A };
    uint8_t i, ok = 1;
    mn_dma_copy(MN_PHYS(p1), POOL_ATTIC, 4);
    mn_dma_copy(POOL_ATTIC, MN_PHYS(blk), 4);
    for (i = 0; i < 4; i++) if (blk[i] != p1[i]) ok = 0;
    mn_dma_copy(MN_PHYS(p2), POOL_ATTIC, 4);
    mn_dma_copy(POOL_ATTIC, MN_PHYS(blk), 4);
    for (i = 0; i < 4; i++) if (blk[i] != p2[i]) ok = 0;
    pool_kind = ok ? 1 : 2;
    pool_base = ok ? POOL_ATTIC : POOL_BANK5;
}

/* The interrupt vectors for the window, at the top of bank 4 (visible as
 * $FFxx during a call): NMI, RESET and IRQ/BRK to a stub that counts and
 * returns. Installed first thing in INIT, before the controller comes up,
 * so no stray ethernet event (5.16) is ever fetched through a stale vector
 * (the ssh client's 5.19). The stub only touches a fixed counter, so the
 * bss clear that follows may zero it freely. */
static void install_window_vectors(void)
{
    /* The interrupt vectors in force while the window is mapped, at the
     * top of bank 4 (visible as $FFxx during a call): NMI, RESET and
     * IRQ/BRK all go to a stub that counts and returns. The caller's own
     * vectors are under its KERNAL and, under BASIC 65, what the CPU would
     * fetch there is variable space; an NMI that BASIC's environment raises
     * at a random moment during a call used to run that as code (5.11). */
#ifdef MN_NMI_PROBE
    /* Diagnostic (5.16): two stubs, so the NMI and the IRQ/BRK vector
     * count separately, each recording CIA2, CIA1 and the VIC's status.
     * NMI stub at $FFC0, IRQ stub at $FFD8. */
    {
        static const uint8_t stub[] = {
            0xEE, 0, 0,             /* +0  inc counter */
            0x48,                   /* +3  pha */
            0xAD, 0x0D, 0xDD,       /* +4  lda $DD0D */
            0x8D, 0, 0,             /* +7  sta cia2 */
            0xAD, 0x0D, 0xDC,       /* +10 lda $DC0D */
            0x8D, 0, 0,             /* +13 sta cia1 */
            0xAD, 0xE1, 0xD6,       /* +16 lda $D6E1: the ethernet controller's status */
            0x8D, 0, 0,             /* +19 sta vic (the slot now holds $D6E1) */
            0x68,                   /* +22 pla */
            0x40 };                 /* +23 rti */
        uint8_t k;
        for (k = 0; k < sizeof stub; k++) { MN_POKE(0xFFC0 + k, stub[k]); MN_POKE(0xFFD8 + k, stub[k]); }
#define PUT16(a, v) do { MN_POKE((a), (uint8_t)(uintptr_t)(v)); MN_POKE((a) + 1, (uint8_t)((uintptr_t)(v) >> 8)); } while (0)
        PUT16(0xFFC1, &window_irqs); PUT16(0xFFC8, &nmi_cia2); PUT16(0xFFCE, &nmi_cia1); PUT16(0xFFD4, &nmi_vic);
        PUT16(0xFFD9, &irq_count);   PUT16(0xFFE0, &irq_cia2); PUT16(0xFFE6, &irq_cia1); PUT16(0xFFEC, &irq_vic);
        MN_POKE(0xFFFA, 0xC0); MN_POKE(0xFFFB, 0xFF);     /* NMI */
        MN_POKE(0xFFFC, 0xC0); MN_POKE(0xFFFD, 0xFF);
        MN_POKE(0xFFFE, 0xD8); MN_POKE(0xFFFF, 0xFF);     /* IRQ / BRK */
    }
#else
    MN_POKE(0xFFF0, 0xEE);                                               /* inc window_irqs */
    MN_POKE(0xFFF1, (uint8_t)(uintptr_t)&window_irqs);
    MN_POKE(0xFFF2, (uint8_t)((uintptr_t)&window_irqs >> 8));
    MN_POKE(0xFFF3, 0x40);                                               /* rti */
    MN_POKE(0xFFFA, 0xF0); MN_POKE(0xFFFB, 0xFF);
    MN_POKE(0xFFFC, 0xF0); MN_POKE(0xFFFD, 0xFF);
    MN_POKE(0xFFFE, 0xF0); MN_POKE(0xFFFF, 0xFF);
#endif
}

void mn_api_init(void)
{
    uint16_t n = (uint16_t)(uintptr_t)__bss_size;
    uint16_t i;

    install_window_vectors();          /* before anything can fault: 5.19 */

    /* No crt0 ran: zero .bss ourselves. Mailboxes and the ZP save area
     * are .noinit and survive this. */
    for (i = 0; i < n; i++)
        __bss_start[i] = 0;
    n = (uint16_t)(uintptr_t)__zp_bss_size;
    for (i = 0; i < n; i++)
        __zp_bss_start[i] = 0;
    /* Initialised zero-page data: the compiler puts small constants there
     * too (the DHCP magic cookie, once), and their initial bytes sit in
     * the image at __zp_data_load_start. No crt0 copies them for us.
     * Without this, the cookie was zero and the router answered our
     * DISCOVER as if it were BOOTP (REQUIREMENTS.md 5.7). */
    n = (uint16_t)(uintptr_t)__zp_data_size;
    for (i = 0; i < n; i++)
        __zp_data_start[i] = __zp_data_load_start[i];

    mn_eth45e100_init(&nif);
    mn_net_init(&net, &nif);
    dhcp.state = MN_DHCP_IDLE;
    dhcp.sock = 0xff;
    dns.state = MN_DNS_IDLE;
    dns.sock = 0xff;
    ntp.state = MN_NTP_IDLE;
    ntp.sock = 0xff;
    probe_pool();
    /* The pool: the TCP sockets' rings first, the UDP mailboxes after them
     * (5.17). Eight 7 KB sockets and four 576-byte mailboxes: $50000-$5E8FF. */
    mn_tcp_init(&tcpset, tcps, MN_ABI_TCP_SOCKETS, &net, pool_base);
    mn_net_set_pool(&net, pool_base + (uint32_t)MN_ABI_TCP_SOCKETS * MN_TCP_SOCKET_XMEM);
    last_frame = MN_FRAMECOUNT;
    ret(0, 0, 0, 0);
}

void mn_api_version(void)
{
    ret(MN_ABI_MAJOR, MN_ABI_MINOR, 'M', 'N');
}

void mn_api_get_mac(void)
{
    mn_dma_copy(MN_PHYS(nif.mac), arg_ptr(), MN_ETH_ADDR_LEN);
    ret(0, 0, 0, 0);
}

/* Block: ptr28[3], len[2]. Frame is fetched from ptr28 by DMA. */
void mn_api_link_tx(void)
{
    uint16_t len;

    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 5);
    len = (uint16_t)(blk[3] | ((uint16_t)blk[4] << 8));
    if (len > MN_MAX_FRAME) {
        ret(0, 0, 0, 0);
        return;
    }
    mn_dma_copy(blk_ptr(), MN_PHYS(frame), len);
    ret(nif.tx(&nif, frame, len), 0, 0, 0);
}

/* Block: ptr28[3], cap[2], len[2]. The frame goes to ptr28 by DMA and
 * len is written back into the block. */
void mn_api_link_rx(void)
{
    uint16_t cap, n = 0;
    uint8_t got;

    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 5);
    cap = (uint16_t)(blk[3] | ((uint16_t)blk[4] << 8));
    if (cap > MN_MAX_FRAME)
        cap = MN_MAX_FRAME;

    got = nif.rx(&nif, frame, cap, &n);
    if (got)
        mn_dma_copy(MN_PHYS(frame), blk_ptr(), n);

    blk[5] = (uint8_t)(n & 0xff);
    blk[6] = (uint8_t)(n >> 8);
    mn_dma_copy(MN_PHYS(blk + 5), arg_ptr() + 5, 2);
    ret(got, 0, 0, 0);
}

void mn_api_poll(void)
{
    uint8_t got = mn_net_poll(&net);
    uint16_t now = tick();
    if (dhcp.state != MN_DHCP_IDLE && dhcp.state != MN_DHCP_FAILED)
        mn_dhcp_poll(&dhcp, &net, now);           /* renewal runs while bound (5.12) */
    if (dns.state == MN_DNS_WAITING)
        mn_dns_poll(&dns, &net, now);
    if (ntp.state == MN_NTP_WAITING)
        mn_ntp_poll(&ntp, &net, now);
    mn_tcp_poll(&tcpset, &net, now);
    ret(got, 0, 0, 0);
}

/* TCP entries take the socket index in Z; a caller that never sets Z
 * gets socket 0, which is how every single-connection client works
 * unchanged. An index past the end answers $FF. */
static mn_tcp *sock_z(void)
{
    return mn_api_z < tcpset.n ? &tcps[mn_api_z] : 0;
}

/* Block: ip[4], port u16 LE. */
void mn_api_tcp_connect(void)
{
    mn_tcp *t = sock_z();
    if (!t) { ret(0xFF, 0, 0, 0); return; }
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 6);
    ret(mn_tcp_connect(t, &net, blk, (uint16_t)(blk[4] | ((uint16_t)blk[5] << 8)), tick()),
        0, 0, 0);
}

/* A/X: the port. */
void mn_api_tcp_listen(void)
{
    mn_tcp *t = sock_z();
    if (!t) { ret(0xFF, 0, 0, 0); return; }
    ret(mn_tcp_listen(t, (uint16_t)(mn_api_a | ((uint16_t)mn_api_x << 8))), 0, 0, 0);
}

void mn_api_tcp_state(void)
{
    mn_tcp *t = sock_z();
    uint16_t a;
    if (!t) { ret(0xFF, 0, 0, 0); return; }
    a = mn_tcp_available(t);
    ret(t->state, t->flags, (uint8_t)a, (uint8_t)(a >> 8));
}

/* Block (out): ip[4], port u16 LE -- the peer of an accepted or
 * connected socket. */
void mn_api_tcp_peer(void)
{
    mn_tcp *t = sock_z();
    if (!t) { ret(0xFF, 0, 0, 0); return; }
    blk[0] = t->remote_ip[0]; blk[1] = t->remote_ip[1];
    blk[2] = t->remote_ip[2]; blk[3] = t->remote_ip[3];
    blk[4] = (uint8_t)t->remote_port; blk[5] = (uint8_t)(t->remote_port >> 8);
    mn_dma_copy(MN_PHYS(blk), arg_ptr(), 6);
    ret(1, 0, 0, 0);
}

/* A = sockets, X = where the buffers are (1 attic RAM, 2 bank 5),
 * Y/Z = their base address >> 12. */
void mn_api_tcp_info(void)
{
    uint16_t b = (uint16_t)(pool_base >> 12);
    ret(tcpset.n, pool_kind, (uint8_t)b, (uint8_t)(b >> 8));
}

/* Block: ptr28[3], len u16. Bytes go from the caller's memory straight
 * into the socket's send ring by DMA. */
void mn_api_tcp_send(void)
{
    mn_tcp *t = sock_z();
    uint16_t len, n;
    if (!t) { ret(0xFF, 0xFF, 0, 0); return; }
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 5);
    len = (uint16_t)(blk[3] | ((uint16_t)blk[4] << 8));
    n = mn_tcp_send_x(t, blk_ptr(), len);
    ret((uint8_t)n, (uint8_t)(n >> 8), 0, 0);
}

/* Block: ptr28[3], cap u16, len u16 (out). Straight from the ring. */
void mn_api_tcp_recv(void)
{
    mn_tcp *t = sock_z();
    uint16_t cap, n;
    if (!t) { ret(0xFF, 0, 0, 0); return; }
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 5);
    cap = (uint16_t)(blk[3] | ((uint16_t)blk[4] << 8));
    n = mn_tcp_recv_x(t, blk_ptr(), cap);
    blk[5] = (uint8_t)n; blk[6] = (uint8_t)(n >> 8);
    mn_dma_copy(MN_PHYS(blk + 5), arg_ptr() + 5, 2);
    ret(n ? 1 : 0, 0, 0, 0);
}

void mn_api_tcp_close(void)
{
    mn_tcp *t = sock_z();
    if (!t) { ret(0xFF, 0, 0, 0); return; }
    mn_tcp_close(t); ret(0, 0, 0, 0);
}

void mn_api_tcp_abort(void)
{
    mn_tcp *t = sock_z();
    if (!t) { ret(0xFF, 0, 0, 0); return; }
    mn_tcp_abort(t, &net); ret(0, 0, 0, 0);
}

/* --- UDP sockets for applications. The index goes in Z, as for TCP;
 * the same sockets serve DHCP, DNS and NTP, which take one each while
 * they run. --- */

/* A/X: the port. Returns the socket index, or $FF. */
void mn_api_udp_open(void)
{
    ret(mn_net_udp_open(&net, (uint16_t)(mn_api_a | ((uint16_t)mn_api_x << 8))), 0, 0, 0);
}

void mn_api_udp_close(void)
{
    if (mn_api_z < MN_UDP_SOCKETS) mn_net_udp_close(&net, mn_api_z);
    ret(0, 0, 0, 0);
}

/* Block: dst ip[4], dport u16, ptr28[3], len u16. Returns MN_SEND_*:
 * 2 means the next hop is being resolved, send again. */
void mn_api_udp_send(void)
{
    uint16_t len, dport;
    uint32_t src;
    if (mn_api_z >= MN_UDP_SOCKETS || net.sock[mn_api_z].port == 0) { ret(0xFF, 0, 0, 0); return; }
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 11);
    dport = (uint16_t)(blk[4] | ((uint16_t)blk[5] << 8));
    src = (uint32_t)blk[6] | ((uint32_t)blk[7] << 8) | ((uint32_t)blk[8] << 16);
    len = (uint16_t)(blk[9] | ((uint16_t)blk[10] << 8));
    if (len > MN_UDP_MAX_PAYLOAD) { ret(0, 0, 0, 0); return; }
    mn_dma_copy(src, MN_PHYS(frame), len);
    ret(mn_net_send_udp(&net, blk, net.sock[mn_api_z].port, dport, frame, len), 0, 0, 0);
}

/* Block: ptr28[3], cap u16, then out: len u16, src ip[4], src port u16.
 * Returns 1 if a datagram was waiting. */
void mn_api_udp_recv(void)
{
    uint16_t cap, len = 0, sport = 0;
    uint8_t got;
    if (mn_api_z >= MN_UDP_SOCKETS) { ret(0xFF, 0, 0, 0); return; }
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 5);
    cap = (uint16_t)(blk[3] | ((uint16_t)blk[4] << 8));
    if (cap > MN_MAX_FRAME) cap = MN_MAX_FRAME;
    got = mn_net_udp_recv(&net, mn_api_z, frame, cap, &len, blk + 7, &sport);
    if (got && len)
        mn_dma_copy(MN_PHYS(frame), blk_ptr(), len);
    blk[5] = (uint8_t)len; blk[6] = (uint8_t)(len >> 8);
    blk[11] = (uint8_t)sport; blk[12] = (uint8_t)(sport >> 8);
    mn_dma_copy(MN_PHYS(blk + 5), arg_ptr() + 5, 8);
    ret(got, 0, 0, 0);
}

void mn_api_set_dns(void)
{
    dhcp.dns[0] = mn_api_a; dhcp.dns[1] = mn_api_x;
    dhcp.dns[2] = mn_api_y; dhcp.dns[3] = mn_api_z;
    ret(0, 0, 0, 0);
}

/* A/X/Y -> a NUL-terminated name in the caller's memory. The server is
 * whatever DHCP gave us, or SET_DNS set. */
void mn_api_dns_start(void)
{
    uint8_t i;
    mn_dma_copy(arg_ptr(), MN_PHYS(name), MN_DNS_NAME_MAX);
    name[MN_DNS_NAME_MAX] = 0;
    for (i = 0; i < 4; i++)
        if (dhcp.dns[i])
            break;
    if (i == 4) {
        dns.state = MN_DNS_FAILED;      /* no server known */
        ret(1, 0, 0, 0);
        return;
    }
    mn_dns_start(&dns, &net, name, dhcp.dns, tick());
    ret(0, 0, 0, 0);
}

void mn_api_dns_state(void)  { ret(dns.state, 0, 0, 0); }
void mn_api_dns_result(void) { ret(dns.ip[0], dns.ip[1], dns.ip[2], dns.ip[3]); }

void mn_api_ntp_start(void)
{
    uint8_t s[4];
    s[0] = mn_api_a; s[1] = mn_api_x; s[2] = mn_api_y; s[3] = mn_api_z;
    mn_ntp_start(&ntp, &net, s, tick());
    ret(0, 0, 0, 0);
}

void mn_api_ntp_state(void) { ret(ntp.state, 0, 0, 0); }

/* Block: offset_min (i16 LE, in); then out: seconds u32 LE, year u16 LE,
 * month, day, hour, minute, second, weekday (0 = Sunday). 14 bytes. */
void mn_api_ntp_result(void)
{
    mn_civil c;
    int16_t off;
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 2);
    off = (int16_t)(blk[0] | ((uint16_t)blk[1] << 8));
    mn_ntp_civil(ntp.seconds, off, &c);
    blk[2] = (uint8_t)ntp.seconds; blk[3] = (uint8_t)(ntp.seconds >> 8);
    blk[4] = (uint8_t)(ntp.seconds >> 16); blk[5] = (uint8_t)(ntp.seconds >> 24);
    blk[6] = (uint8_t)c.year; blk[7] = (uint8_t)(c.year >> 8);
    blk[8] = c.month; blk[9] = c.day; blk[10] = c.hour; blk[11] = c.minute;
    blk[12] = c.second; blk[13] = c.weekday;
    mn_dma_copy(MN_PHYS(blk + 2), arg_ptr() + 2, 12);
    ret(0, 0, 0, 0);
}

static uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Block: year u16 LE, month, day, hour, minute, second, weekday (0 =
 * Sunday, written as given). The RTC
 * is BCD at $FFD7110-$FFD7116; bit 7 of the hour selects 24-hour mode. */
void mn_api_set_rtc(void)
{
    uint16_t year;
    uint8_t r[7];
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 8);
    year = (uint16_t)(blk[0] | ((uint16_t)blk[1] << 8));
    r[0] = bcd(blk[6]);                         /* seconds */
    r[1] = bcd(blk[5]);                         /* minutes */
    r[2] = (uint8_t)(bcd(blk[4]) | 0x80);       /* hours, 24h mode */
    r[3] = bcd(blk[3]);                         /* day */
    r[4] = bcd(blk[2]);                         /* month */
    r[5] = bcd((uint8_t)(year % 100));          /* year */
    r[6] = bcd(blk[7]);                         /* weekday */
    mn_dma_copy(MN_PHYS(r), 0xFFD7110UL, 7);
    ret(0, 0, 0, 0);
}

void mn_api_dhcp_start(void)
{
    mn_dhcp_start(&dhcp, &net, tick());
    ret(0, 0, 0, 0);
}

/* A = state; X = phase while bound (0 bound, 1 renewing, 2 rebinding);
 * Y/Z = minutes of lease left, 65535 for no expiry (5.12). */
void mn_api_dhcp_state(void)
{
    uint16_t left = mn_dhcp_lease_left(&dhcp);
    ret(dhcp.state, dhcp.phase, (uint8_t)left, (uint8_t)(left >> 8));
}

void mn_api_get_dns(void)
{
    blk[0] = dhcp.dns[0]; blk[1] = dhcp.dns[1]; blk[2] = dhcp.dns[2]; blk[3] = dhcp.dns[3];
    mn_dma_copy(MN_PHYS(blk), arg_ptr(), 4);
    ret(0, 0, 0, 0);
}

/* Block: ip[4], mask[4], gw[4]. */
void mn_api_set_ip(void)
{
    mn_dma_copy(arg_ptr(), MN_PHYS(blk), 12);
    mn_net_set_ip(&net, blk, blk + 4, blk + 8);
    ret(0, 0, 0, 0);
}

void mn_api_get_ip(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        blk[i] = net.ip[i]; blk[4 + i] = net.mask[i]; blk[8 + i] = net.gw[i];
    }
    mn_dma_copy(MN_PHYS(blk), arg_ptr(), 12);
    ret(0, 0, 0, 0);
}

/* Block: rx_frames, arp_replies, echo_replies, tx_failures, then
 * arp_learned, udp_rx, udp_dropped, dhcp_tries; u16 LE each. Older
 * callers read the first eight bytes; the rest is appended. */
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void mn_api_get_stats(void)
{
    put16(blk + 0,  net.rx_frames);
    put16(blk + 2,  net.arp_replies);
    put16(blk + 4,  net.echo_replies);
    put16(blk + 6,  net.tx_failures);
    put16(blk + 8,  net.arp_learned);
    put16(blk + 10, net.udp_rx);
    put16(blk + 12, net.udp_dropped);
    put16(blk + 14, (uint16_t)(dhcp.tries | ((uint16_t)dhcp.pending << 8)));
    put16(blk + 16, dhcp.rx_count);
    put16(blk + 18, dhcp.last_len);
    blk[20] = dhcp.last_op; blk[21] = dhcp.last_type;
    blk[22] = dhcp.last_xid[0]; blk[23] = dhcp.last_xid[1];
    blk[24] = dhcp.last_xid[2]; blk[25] = dhcp.last_xid[3];
    blk[26] = dhcp.xid[0]; blk[27] = dhcp.xid[1]; blk[28] = dhcp.xid[2]; blk[29] = dhcp.xid[3];
    blk[30] = dhcp.last_chaddr[0]; blk[31] = dhcp.last_chaddr[5];
    put16(blk + 32, window_irqs);                 /* interrupts caught during calls (5.11) */
    put16(blk + 34, tcpset.rst_sent);             /* TCP segments nobody owned, answered with RST */
    mn_dma_copy(MN_PHYS(blk), arg_ptr(), 36);
    ret(0, 0, 0, 0);
}

/* For BASIC: SYS $4201B,a,b,c,d. Mask and gateway are left as they are. */
void mn_api_set_local_ip4(void)
{
    uint8_t ip[4];
    ip[0] = mn_api_a; ip[1] = mn_api_x; ip[2] = mn_api_y; ip[3] = mn_api_z;
    mn_net_set_ip(&net, ip, net.mask, net.gw);
    ret(0, 0, 0, 0);
}

void mn_api_dhcp_last_msg(void)
{
    mn_dma_copy(MN_PHYS(mn_dhcp_msg_buffer()), arg_ptr(), 300);
    ret(0, 0, 0, 0);
}
