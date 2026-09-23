/* 45E100 driver. Every sequence here is from REQUIREMENTS.md 5.1, which
 * was read out of the earlier stack's driver rather than guessed. The one
 * deliberate departure is timing: that driver's waits spin on a VIC-II raster
 * register that never advances in native mode, and this uses the frame
 * counter instead. */
#include "mn_eth45e100.h"
#include "mn_m65.h"
#include "mn_dma.h"

#define ETH_CTRL1   0xD6E0
#define ETH_CTRL2   0xD6E1
#define ETH_TXSIZEL 0xD6E2
#define ETH_TXSIZEH 0xD6E3
#define ETH_COMMAND 0xD6E4
#define ETH_CTRL3   0xD6E5
#define ETH_MAC     0xD6E9

#define ETH_BUFFER  0xFFDE800UL     /* shared TX/RX window, 28-bit */

#define CTRL1_TXIDLE    0x80
#define CTRL2_RXWAITING 0x20
#define META1_CRCERR    0x80

#define MIN_FRAME 60
#define FCS_LEN   4

/* Frames are staged here before DMA; the DMA source must be a 28-bit
 * address we can compute, and a static array in bank 0 is that. */
/* Short frames are padded to the minimum in here; anything longer is
 * transmitted straight from the caller's buffer, which is DMA-reachable
 * memory of the image (5.11: the window had no room for a staging frame). */
static uint8_t pad[MIN_FRAME] __attribute__((section(".bss.mn_eth_pad")));
static uint8_t meta[2];

/* Diagnostics, readable from bank 4 over the monitor: what the driver
 * itself last saw. [0] CTRL1 at last tx, [1] CTRL2 at last rx poll,
 * [2] tx failure reason (1 too long, 2 TXIDLE timeout), [3] rx polls lo,
 * [4] rx polls hi, [5] tx attempts, [6] meta[0], [7] meta[1]. */
volatile uint8_t mn_eth_dbg[8] __attribute__((section(".bss.mn_eth_dbg")));

uint8_t mn_eth45e100_ctrl1(void) { return MN_PEEK(ETH_CTRL1); }
uint8_t mn_eth45e100_ctrl2(void) { return MN_PEEK(ETH_CTRL2); }

static uint8_t eth_rx(mn_netif *nif, uint8_t *buf, uint16_t cap,
                      uint16_t *len)
{
    uint16_t n;
    (void)nif;

    mn_eth_dbg[1] = MN_PEEK(ETH_CTRL2);
    if (++mn_eth_dbg[3] == 0) mn_eth_dbg[4]++;
    if (!(mn_eth_dbg[1] & CTRL2_RXWAITING))
        return 0;

    /* Rotate FIRST. The receive ring's CPU-visible buffer is the one
     * already handled; the flag says a newer one is waiting. Reading
     * before rotating serves the previous frame -- which answered every
     * ping exactly one ping late (REQUIREMENTS.md 5.5). */
    MN_POKE(ETH_CTRL2, 0x01);
    MN_POKE(ETH_CTRL2, 0x03);

    /* Two metadata bytes lead the buffer: length low, then the length
     * high nibble with flag bits above it. */
    mn_dma_copy(ETH_BUFFER, MN_PHYS(meta), 2);
    n = (uint16_t)(meta[0] | ((uint16_t)(meta[1] & 0x0f) << 8));
    mn_eth_dbg[6] = meta[0]; mn_eth_dbg[7] = meta[1];

    /* The reported length includes the 4-byte FCS: a 60-byte frame reads
     * as 64, with the CRC visible after the padding (step 2 spike,
     * REQUIREMENTS.md 5.2). Upper layers never want it. */
    if (n <= FCS_LEN || (meta[1] & META1_CRCERR))
        return 0;
    n = (uint16_t)(n - FCS_LEN);
    if (n > cap)
        n = cap;

    mn_dma_copy(ETH_BUFFER + 2, MN_PHYS(buf), n);
    *len = n;
    return 1;
}

static uint8_t eth_tx(mn_netif *nif, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t last, seen = 0;
    uint16_t spins = 0;
    uint8_t wraps = 0;
    (void)nif;

    mn_eth_dbg[5]++;
    if (len > MN_MAX_FRAME) {
        mn_eth_dbg[2] = 1;
        return 0;
    }

    if (len < MIN_FRAME) {
        for (i = 0; i < len; i++)
            pad[i] = buf[i];
        for (; i < MIN_FRAME; i++)          /* link-layer padding */
            pad[i] = 0;
        buf = pad;
        len = MIN_FRAME;
    }

    MN_POKE(ETH_TXSIZEL, len & 0xff);
    MN_POKE(ETH_TXSIZEH, len >> 8);
    mn_dma_copy(MN_PHYS(buf), ETH_BUFFER, len);

    MN_POKE(ETH_CTRL1, 0x03);               /* not under reset */

    /* Bounded wait for TX idle: 50 frames (~1 s), with a spin backstop. */
    last = MN_FRAMECOUNT;
    while (!((mn_eth_dbg[0] = MN_PEEK(ETH_CTRL1)) & CTRL1_TXIDLE)) {
        uint8_t cur = MN_FRAMECOUNT;
        if (cur != last) {
            last = cur;
            if (++seen >= 50) {
                mn_eth_dbg[2] = 2;
                return 0;
            }
        }
        if (++spins == 0 && ++wraps >= 200) {
            mn_eth_dbg[2] = 3;
            return 0;
        }
    }

    MN_POKE(ETH_COMMAND, 0x01);             /* transmit */
    mn_eth_dbg[2] = 0;
    return 1;
}

void mn_eth45e100_init(mn_netif *nif)
{
    uint8_t i, v;

    mn_m65_io_enable();

    /* RX filter: no multicast, broadcast on (ARP needs it), promiscuous
     * off (NOPROM=1). */
    v = MN_PEEK(ETH_CTRL3);
    v = (uint8_t)((v & 0xdf) | 0x11);
    MN_POKE(ETH_CTRL3, v);

    for (i = 0; i < MN_ETH_ADDR_LEN; i++)
        nif->mac[i] = MN_PEEK(ETH_MAC + i);

    /* Reset, release, then pulse the TX state machine. */
    MN_POKE(ETH_CTRL1, 0x00);
    mn_m65_wait_frames(5);
    MN_POKE(ETH_CTRL1, 0x03);
    mn_m65_wait_frames(5);
    MN_POKE(ETH_CTRL2, 0x03);
    MN_POKE(ETH_CTRL2, 0x00);       /* also leaves RXQEN/TXQEN clear */

    /* The PHY needs about four seconds to come back. */
    mn_m65_wait_frames(200);

    /* RXQEN/TXQEN (CTRL2 bits 7,6) enable controller interrupts nothing
     * here services. Clear them read-modify-write, never with a blind
     * store: the low bits are the RX queue advance controls. */
    v = MN_PEEK(ETH_CTRL2);
    MN_POKE(ETH_CTRL2, v & 0x3f);

    nif->rx = eth_rx;
    nif->tx = eth_tx;
    nif->ctx = 0;
}
