/* Included by recomp_manual.c after dev_spawn.h. Guest-thread entry for the
 * multiplayer work: Patch-Generated.py calls recomp_multiplayer_tick once per
 * retail world update (top of sub_001702D0, after the developer and boids
 * ticks) with that call's object and frame time, and
 * recomp_multiplayer_player_control at the entry of the player human's control
 * slot (sub_0005D5C0, vtable 0x2E32B8 slot +0x1E4) with every control the
 * player's AI sends to its human. Everything behind them is off by default and
 * returns before touching guest state, so the game behaves exactly as before. */
#ifndef MERCENARIES_MULTIPLAYER_GUEST_H
#define MERCENARIES_MULTIPLAYER_GUEST_H
#include "player_state_log.h"
#include "puppet_drive.h"
#include "puppet_replay.h"
#include "multiplayer_session.h"

void recomp_multiplayer_player_control(uint32_t human, uint32_t id, uint32_t value)
{
    puppet_replay_record_control(human, id, value);
    mp_session_record_control(human, id, value);
}

void recomp_multiplayer_tick(uint32_t world, uint32_t delta_bits)
{
    player_state_log_tick(world, delta_bits);
    puppet_replay_tick(world, delta_bits);
    mp_session_tick(world, delta_bits);
}
#endif
