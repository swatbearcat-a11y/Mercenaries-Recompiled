/* Included by recomp_manual.c after dev_spawn.h. Guest-thread entry for the
 * multiplayer work: Patch-Generated.py calls it once per retail world update
 * (top of sub_001702D0, after the developer and boids ticks) with that call's
 * object and frame time. Everything behind it is off by default and returns
 * before touching guest state, so the game behaves exactly as before. */
#ifndef MERCENARIES_MULTIPLAYER_GUEST_H
#define MERCENARIES_MULTIPLAYER_GUEST_H
#include "player_state_log.h"
#include "puppet_replay.h"

void recomp_multiplayer_tick(uint32_t world, uint32_t delta_bits)
{
    player_state_log_tick(world, delta_bits);
    puppet_replay_tick(world, delta_bits);
}
#endif
