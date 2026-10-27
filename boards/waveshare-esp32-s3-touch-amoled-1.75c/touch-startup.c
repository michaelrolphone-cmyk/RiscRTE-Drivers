#include "touch-startup.h"
#include "startup.h"

bool risc_badge_prepare_touch(risc_board_prepare_state *state,
                              uint64_t admitted_bus_instance_id,
                              const risc_i2c_bus_api_v1 *bus,
                              const risc_board_prepare_io *io) {
    const risc_board_prepare_config *source = risc_badge_prepare_config();
    if (!source || source->reset_count > RISC_BOARD_RESET_MAX) return false;
    risc_board_prepare_config selected = *source;
    selected.reset_count = 0;
    for (size_t i = 0; i < source->reset_count; ++i) {
        if (source->resets[i].pin != 2) continue;
        if (selected.reset_count) return false;
        selected.resets[selected.reset_count++] = source->resets[i];
    }
    if (selected.reset_count != 1) return false;
    /* prepare copies no config pointer; the retained owners are state/io/bus. */
    return risc_board_prepare(state, &selected, admitted_bus_instance_id, bus, io);
}
