#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_GPIO_EXPANDER_API_V1 1u
/* One chip owner, nonoverlapping pin grants. Masks and input samples are
 * scoped to a grant. Claim sets safe output levels before output directions.
 * A failed write may be physically uncertain: retain this provider and its
 * dependencies. Failed claim may return a token which must also be retained.
 * Release restores safe output levels; it never restores a whole-bank shadow.
 */
typedef struct risc_gpio_expander_api_v1 {
    uint32_t api_version, struct_size;
    void *context;
    bool (*claim)(void *context, uint16_t pins, uint16_t inputs,
                  uint16_t safe_levels, uint64_t *grant);
    bool (*read)(void *context, uint64_t grant, uint16_t *levels);
    bool (*write)(void *context, uint64_t grant, uint16_t mask, uint16_t levels);
    bool (*release)(void *context, uint64_t grant);
} risc_gpio_expander_api_v1;
#ifdef __cplusplus
}
#endif
