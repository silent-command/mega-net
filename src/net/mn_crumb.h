/* Breadcrumbs: a byte written at points along a call's path into a
 * pinned location, so that after a crash the monitor can say how far the
 * call got without a breakpoint perturbing its timing (5.11). Only in the
 * target image; nothing on the host. */
#ifndef MN_CRUMB_H
#define MN_CRUMB_H
#ifdef __mos__
#include <stdint.h>
extern volatile uint8_t mn_crumb, mn_crumb_p;
uint8_t mn_get_p(void);
#define MN_CRUMB(n) (mn_crumb = (n), mn_crumb_p = mn_get_p())
#define MN_SNAP(i) ((void)0)
#else
#define MN_CRUMB(n) ((void)0)
#define MN_SNAP(i) ((void)0)
#endif
#endif
