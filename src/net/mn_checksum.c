#include "mn_checksum.h"
#include "mn_byteorder.h"

#ifdef __mos__
/* On the target the sum is src/hal/mn_checksum_m65.S: 13 instructions a
 * word where the C below compiled to about 40, and it is every byte the
 * stack sends or receives, twice. The C stays the host's, and the
 * reference the assembly is checked against. */
extern uint16_t mn_cs_ptr, mn_cs_len, mn_cs_sum;
void mn_sum16_m65(void);

static uint16_t sum16(const uint8_t *data, uint16_t len)
{
    mn_cs_ptr = (uint16_t)(uintptr_t)data;
    mn_cs_len = len;
    mn_sum16_m65();
    return mn_cs_sum;
}
#else
/* Accumulates in 16 bits with an end-around carry rather than summing into
 * a uint32_t. Ones-complement addition makes the two equivalent, and on the
 * 6502 it avoids dragging in 32-bit arithmetic for every packet. */
static uint16_t sum16(const uint8_t *data, uint16_t len)
{
    uint16_t sum = 0;
    uint16_t word;
    uint16_t i = 0;

    while ((uint16_t)(i + 1) < len) {
        word = mn_get16(data + i);
        sum += word;
        if (sum < word)
            sum++;               /* end-around carry */
        i = (uint16_t)(i + 2);
    }

    if (i < len) {               /* odd trailing byte, padded low */
        word = (uint16_t)((uint16_t)data[i] << 8);
        sum += word;
        if (sum < word)
            sum++;
    }

    return sum;
}
#endif

uint16_t mn_checksum(const uint8_t *data, uint16_t len)
{
    return (uint16_t)~sum16(data, len);
}

uint8_t mn_checksum_valid(const uint8_t *data, uint16_t len)
{
    return (uint16_t)~sum16(data, len) == 0 ? 1 : 0;
}

uint16_t mn_checksum_raw(const uint8_t *data, uint16_t len)
{
    return sum16(data, len);
}

uint16_t mn_checksum_pseudo(const uint8_t *src_ip, const uint8_t *dst_ip,
                            uint8_t proto, const uint8_t *data,
                            uint16_t len)
{
    uint8_t ph[12];
    uint16_t a, b, sum;
    uint8_t i;

    for (i = 0; i < 4; i++) {
        ph[i] = src_ip[i];
        ph[4 + i] = dst_ip[i];
    }
    ph[8] = 0;
    ph[9] = proto;
    mn_put16(ph + 10, len);

    a = sum16(ph, 12);
    b = sum16(data, len);
    sum = (uint16_t)(a + b);
    if (sum < a)
        sum++;                       /* end-around carry */
    return (uint16_t)~sum;
}
