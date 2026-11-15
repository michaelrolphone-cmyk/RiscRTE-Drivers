#pragma once
#include <stdint.h>
#ifndef BADGE_FIXTURE_HOME_IDLE_MS
#error "Fixture requires the selected Home idle compile recipe"
#endif
// Faults overlap the configured first idle attempt; never assume the old 15s policy.
static constexpr uint64_t badge_fixture_idle_fault_end_ms(){return BADGE_FIXTURE_HOME_IDLE_MS+10000ULL;}
static constexpr uint64_t badge_fixture_home_watchdog_ms(){return 30000ULL+2ULL*BADGE_FIXTURE_HOME_IDLE_MS;}
