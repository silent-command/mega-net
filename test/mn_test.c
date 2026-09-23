#include "mn_test.h"
#include <string.h>

int mn_tests_run = 0;
int mn_tests_failed = 0;
const char *mn_current_suite = "";

void mn_suite(const char *name)
{
    mn_current_suite = name;
    printf("\n# %s\n", name);
}

void mn_expect(int ok, const char *what, const char *detail)
{
    mn_tests_run++;
    if (ok) {
        printf("  ok   %s\n", what);
    } else {
        mn_tests_failed++;
        printf("  FAIL %s", what);
        if (detail && detail[0])
            printf(" -- %s", detail);
        printf("\n");
    }
}

void mn_expect_eq_u16(uint16_t got, uint16_t want, const char *what)
{
    char detail[64];
    detail[0] = '\0';
    if (got != want)
        snprintf(detail, sizeof detail, "got 0x%04x, want 0x%04x", got, want);
    mn_expect(got == want, what, detail);
}

void mn_expect_eq_bytes(const uint8_t *got, const uint8_t *want, uint16_t len,
                        const char *what)
{
    uint16_t i;
    char detail[64];

    for (i = 0; i < len; i++) {
        if (got[i] != want[i]) {
            snprintf(detail, sizeof detail,
                     "byte %u: got 0x%02x, want 0x%02x", i, got[i], want[i]);
            mn_expect(0, what, detail);
            return;
        }
    }
    mn_expect(1, what, "");
}
