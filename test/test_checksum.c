#include "mn_test.h"
#include "../src/net/mn_checksum.h"

/* The worked example from RFC 1071 section 3. */
static const uint8_t rfc1071[] = {
    0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7
};

/* A real IPv4 header with its checksum field zeroed. The correct value is
 * 0xb861, which is where it goes back at offset 10. */
static const uint8_t ipv4_hdr[] = {
    0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00,
    0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01,
    0xc0, 0xa8, 0x00, 0xc7
};

void test_checksum(void)
{
    uint8_t filled[sizeof ipv4_hdr];
    uint8_t odd[3] = { 0x12, 0x34, 0x56 };
    uint16_t i;

    mn_suite("checksum");

    mn_expect_eq_u16(mn_checksum(rfc1071, sizeof rfc1071), 0x220d,
                     "RFC 1071 worked example");

    mn_expect_eq_u16(mn_checksum(ipv4_hdr, sizeof ipv4_hdr), 0xb861,
                     "IPv4 header checksum");

    /* Put the checksum back and the whole header must sum to zero -- this
     * is how received packets get verified. */
    for (i = 0; i < sizeof ipv4_hdr; i++)
        filled[i] = ipv4_hdr[i];
    filled[10] = 0xb8;
    filled[11] = 0x61;
    mn_expect(mn_checksum_valid(filled, sizeof filled),
              "filled header validates", "");

    filled[12] ^= 0x01;
    mn_expect(!mn_checksum_valid(filled, sizeof filled),
              "corrupted header is rejected", "");

    /* Odd length: the trailing byte is padded low, not dropped. */
    mn_expect(mn_checksum(odd, 3) != mn_checksum(odd, 2),
              "odd trailing byte is included", "");
}
