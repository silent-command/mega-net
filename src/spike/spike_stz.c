/* Regression demonstrator for REQUIREMENTS.md 5.4: on the 45GS02, a
 * compiler zero-store (STZ) writes the Z register. Expected at $1400:
 *   'S','Z', then 00 (Z clear), then 83 (Z set), then 00 (Z cleared).
 * If the middle byte is not $83, the toolchain has changed and the rule
 * in the project notes can be revisited. */
#include <stdint.h>
void stz_set_z(void); void stz_clear_z(void);
#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
int main(void)
{
    ST(0) = 'S'; ST(1) = 'Z';
    stz_clear_z(); ST(2) = 0;
    stz_set_z();   ST(3) = 0;          /* compiles to STZ: stores Z = $83 */
    stz_clear_z(); ST(4) = 0;
    for (;;) { }
}
