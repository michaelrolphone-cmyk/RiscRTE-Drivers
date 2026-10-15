#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern uint64_t __udivdi3(uint64_t numerator, uint64_t denominator);

static void check(uint64_t numerator, uint64_t denominator) {
    volatile uint64_t n = numerator;
    volatile uint64_t d = denominator;
    assert(d && __udivdi3(n, d) == n / d);
}

int main(void) {
    const uint64_t max = UINT64_MAX;
    check(0, 1);
    check(1, 1);
    check(1, max);
    check(max, 1);
    check(max, 2);
    check(max, 3);
    check(max, max);
    check(max - 1, max);
    check(max - 1, max - 2);
    check((uint64_t)1 << 63, ((uint64_t)1 << 63) + 1);
    check(((uint64_t)1 << 63) + 1, (uint64_t)1 << 63);
    check(8000000, 100);
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    for (unsigned i = 0; i < 10000; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const uint64_t numerator = state;
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        check(numerator, state ? state : 1);
    }
    puts("Native ELF unsigned division helper boundary and randomized tests passed");
    return 0;
}
