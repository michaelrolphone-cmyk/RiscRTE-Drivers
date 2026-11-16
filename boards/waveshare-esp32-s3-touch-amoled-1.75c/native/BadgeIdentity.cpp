// Read-only linked build evidence, not a startup hook or hardware provider.
#include <cstdint>
#include "RiscCo5300Profile.h"
static_assert(RISC_CO5300_WIDTH == 466 && RISC_CO5300_HEIGHT == 466,
              "Badge native selection requires the same 466x466 facade profile");
static_assert(RISC_CO5300_OFFSET_X == 6 && RISC_CO5300_OFFSET_Y == 0,
              "Badge native selection must preserve its pinned panel origin");
extern "C" {
extern const uint32_t risc_badge_native_identity[] = {
    1, RISC_CO5300_WIDTH, RISC_CO5300_HEIGHT,
    RISC_CO5300_OFFSET_X, RISC_CO5300_OFFSET_Y,
    32 * 1024 * 1024, 8 * 1024 * 1024
};
}
