#pragma once
// The per-tick poll: reads the player's state through natives, compares it with the last tick, and turns the changes
// into bHaptics events. Runs on RedHook's script fiber only (the one place natives may be called).

#include <cstdint>

#include "config.h"

namespace rdrbh::game {

void init(const Config& cfg);
void tick(uint64_t now_ms);

}  // namespace rdrbh::game
