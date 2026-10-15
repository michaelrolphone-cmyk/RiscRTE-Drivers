/*
 * Native ELF compatibility implementation of the Xtensa compiler's unsigned
 * 64-bit division helper. The stock libgcc archive introduces ELF constructs
 * that the firmware's deliberately restricted ELF validator does not accept.
 * Keep this helper inside only those drivers that actually need it;
 * do not publish it as a privileged or ordinary firmware ABI symbol.
 *
 * Restoring binary division. The carry preserves the 65th bit of the partial
 * remainder before subtraction, including when the divisor is near UINT64_MAX.
 */
#include <stdint.h>

__attribute__((visibility("hidden")))
uint64_t __udivdi3(uint64_t numerator, uint64_t denominator) {
    if (denominator == 0) return 0; /* Division by zero is undefined in C. */
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    for (unsigned bit = 0; bit < 64; ++bit) {
        const unsigned carry = (unsigned)(remainder >> 63);
        remainder = (remainder << 1) | (numerator >> 63);
        numerator <<= 1;
        quotient <<= 1;
        if (carry || remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1;
        }
    }
    return quotient;
}
