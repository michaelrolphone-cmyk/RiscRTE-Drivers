/* Board-owned configuration accessor for a future admitted startup adapter.
 * No runtime hook, application variant, public capability or device access. */
#include "startup-config.h"
#include "startup.h"
const risc_board_prepare_config *risc_badge_prepare_config(void) {
    return &board_prepare_config;
}
