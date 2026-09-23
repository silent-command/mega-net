/* Step 2 spike: one raw frame out, one in, on real hardware.
 *
 * This is a throwaway. It proves the 45E100 path -- DMA to and from the
 * controller buffer, the transmit handshake, the receive queue -- before
 * anything is built on it. It does not use src/net/ beyond the netif
 * definition, and it does not use ARP from mn_arp.c (which is still a
 * stub); the frame below is hand-built bytes.
 *
 * The frame is shaped as an ARP request for the development host's
 * address, for one reason: the host's kernel will answer it. That gives two
 * witnesses that need no root on the host -- `arp -an` shows our MAC
 * against SPARE_IP, and the reply lands in our receive queue as a unicast
 * frame. The alternative, tcpdump, needs /dev/bpf.
 *
 * Results go to a status block at $1400 (verified free below a $2001 PRG
 * by the gopher project), read back over the serial monitor with
 *   m65 --memsave 1400:1480=out.bin
 * No screen code, so nothing else has to work for the spike to report.
 */
#include <stdint.h>
#include "../net/mn_netif.h"
#include "../hal/mn_m65.h"
#include "../hal/mn_eth45e100.h"

/* --- status block layout, all offsets from $1400 --- */
#define ST_BASE     0x1400
#define ST_MAGIC    0       /* 'M','N' */
#define ST_STATE    2       /* see states below */
#define ST_TXCOUNT  3
#define ST_RXCOUNT  4
#define ST_CTRL1    5
#define ST_CTRL2    6
#define ST_MAC      7       /* 6 bytes */
#define ST_TXFAIL   13
#define ST_RXLEN    14      /* 2 bytes, little-endian */
#define ST_RXFRAME  16      /* first 64 bytes of most recent frame */
#define ST_LOOPS    80      /* low byte of main-loop count: proves liveness */

#define STATE_START   1
#define STATE_INITED  2
#define STATE_SENT    3
#define STATE_GOT_RX  4

#define ST(off) (*(volatile uint8_t *)(ST_BASE + (off)))

/* The development host, and a free address on its subnet for us to claim
 * as sender. Confirmed unused before the spike was written. */
static const uint8_t host_ip[4]  = { 192, 168, 1, 232 };
static const uint8_t spare_ip[4] = { 192, 168, 1, 199 };

static mn_netif nif;
static uint8_t frame[MN_MAX_FRAME];

static uint16_t build_arp_request(uint8_t *f)
{
    uint8_t i;
    for (i = 0; i < 6; i++) f[i] = 0xff;            /* dst: broadcast */
    for (i = 0; i < 6; i++) f[6 + i] = nif.mac[i];  /* src: us */
    f[12] = 0x08; f[13] = 0x06;                     /* ARP */
    f[14] = 0x00; f[15] = 0x01;                     /* hw: ethernet */
    f[16] = 0x08; f[17] = 0x00;                     /* proto: IPv4 */
    f[18] = 6;    f[19] = 4;                        /* hlen, plen */
    f[20] = 0x00; f[21] = 0x01;                     /* request */
    for (i = 0; i < 6; i++) f[22 + i] = nif.mac[i]; /* sender mac */
    for (i = 0; i < 4; i++) f[28 + i] = spare_ip[i];
    for (i = 0; i < 6; i++) f[32 + i] = 0;          /* target mac */
    for (i = 0; i < 4; i++) f[38 + i] = host_ip[i];
    return 42;
}

int main(void)
{
    uint16_t n, i;
    uint8_t last, seen = 0;
    uint8_t loops = 0;

    ST(ST_MAGIC) = 'M'; ST(ST_MAGIC + 1) = 'N';
    ST(ST_STATE) = STATE_START;
    ST(ST_TXCOUNT) = 0; ST(ST_RXCOUNT) = 0; ST(ST_TXFAIL) = 0;
    ST(ST_RXLEN) = 0; ST(ST_RXLEN + 1) = 0;

    mn_eth45e100_init(&nif);
    for (i = 0; i < 6; i++)
        ST(ST_MAC + i) = nif.mac[i];
    ST(ST_STATE) = STATE_INITED;

    last = MN_FRAMECOUNT;
    for (;;) {
        ST(ST_LOOPS) = ++loops;
        ST(ST_CTRL1) = mn_eth45e100_ctrl1();
        ST(ST_CTRL2) = mn_eth45e100_ctrl2();

        /* Every ~2 s (100 frames), send the request again so a witness
         * on the host has as many chances as it needs. */
        if (MN_FRAMECOUNT != last) {
            last = MN_FRAMECOUNT;
            if (++seen >= 100) {
                seen = 0;
                n = build_arp_request(frame);
                if (nif.tx(&nif, frame, n)) {
                    ST(ST_TXCOUNT)++;
                    if (ST(ST_STATE) < STATE_SENT)
                        ST(ST_STATE) = STATE_SENT;
                } else {
                    ST(ST_TXFAIL)++;
                }
            }
        }

        if (nif.rx(&nif, frame, sizeof frame, &n)) {
            ST(ST_RXCOUNT)++;
            ST(ST_RXLEN) = (uint8_t)(n & 0xff);
            ST(ST_RXLEN + 1) = (uint8_t)(n >> 8);
            for (i = 0; i < 64 && i < n; i++)
                ST(ST_RXFRAME + i) = frame[i];
            ST(ST_STATE) = STATE_GOT_RX;
        }
    }
}
