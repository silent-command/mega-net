#include "mn_ether.h"
#include "mn_byteorder.h"

const uint8_t mn_eth_broadcast[MN_ETH_ADDR_LEN] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static uint8_t addr_eq(const uint8_t *a, const uint8_t *b)
{
    uint8_t i;
    for (i = 0; i < MN_ETH_ADDR_LEN; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void addr_copy(uint8_t *dst, const uint8_t *src)
{
    uint8_t i;
    for (i = 0; i < MN_ETH_ADDR_LEN; i++)
        dst[i] = src[i];
}

uint16_t mn_ether_build(uint8_t *buf, const uint8_t *dst, const uint8_t *src,
                        uint16_t ethertype)
{
    addr_copy(buf + MN_ETH_OFF_DST, dst);
    addr_copy(buf + MN_ETH_OFF_SRC, src);
    mn_put16(buf + MN_ETH_OFF_TYPE, ethertype);
    return MN_ETH_HDR_LEN;
}

uint16_t mn_ether_type(const uint8_t *frame)
{
    return mn_get16(frame + MN_ETH_OFF_TYPE);
}

const uint8_t *mn_ether_dst(const uint8_t *frame)
{
    return frame + MN_ETH_OFF_DST;
}

const uint8_t *mn_ether_src(const uint8_t *frame)
{
    return frame + MN_ETH_OFF_SRC;
}

uint8_t mn_ether_is_for_us(const uint8_t *frame, const uint8_t *our_mac)
{
    const uint8_t *dst = frame + MN_ETH_OFF_DST;
    return (uint8_t)(addr_eq(dst, our_mac) || addr_eq(dst, mn_eth_broadcast));
}
