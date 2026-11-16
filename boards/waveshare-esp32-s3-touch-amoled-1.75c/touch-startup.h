#ifndef RISC_BADGE_TOUCH_STARTUP_H
#define RISC_BADGE_TOUCH_STARTUP_H
#include "prepare.h"
/* Private product composition, not a Runtime hook or a public capability.
 * The panel facade/native backend own GPIO1. This adapter prepares/retains only
 * GPIO2 for the existing CST9217 consumer, which does not own its reset pin.
 * Validate the full hardware graph before calling. Keep callbacks/dependencies
 * alive and stop/quiesce CST9217 before risc_board_prepare_finish succeeds. */
bool risc_badge_prepare_touch(risc_board_prepare_state *, uint64_t,
                              const risc_i2c_bus_api_v1 *,
                              const risc_board_prepare_io *);
#endif
