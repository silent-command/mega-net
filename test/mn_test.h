/* A deliberately tiny test framework. Host-only: it uses stdio, which the
 * stack core never does. */
#ifndef MN_TEST_H
#define MN_TEST_H

#include <stdio.h>
#include <stdint.h>

extern int mn_tests_run;
extern int mn_tests_failed;
extern const char *mn_current_suite;

void mn_expect(int ok, const char *what, const char *detail);
void mn_expect_eq_u16(uint16_t got, uint16_t want, const char *what);
void mn_expect_eq_bytes(const uint8_t *got, const uint8_t *want, uint16_t len,
                        const char *what);
void mn_suite(const char *name);

#endif
