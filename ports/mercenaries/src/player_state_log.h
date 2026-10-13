/* Included by recomp_manual.c through multiplayer_guest.h, after dev_spawn.h
 * (dev_call, dev_guest_address, dev_player_human). Developer option for the
 * multiplayer work: with [Developer] player_state_log=1 and logging=1, sample
 * the local player at the retail world-update checkpoint and write one
 * "player-state" preview-log line per sample (player_state_log_hz, default 5).
 * With the option off it returns before reading guest memory.
 *
 * Layout evidence, supported XBE only (details in docs/multiplayer/NOTES.md):
 * 0x8C7E0(name hash) returns the player0 controller; controller+0x998 is the
 * human and controller+0x10 the entity its position accessor reports
 * (tools/recomp/test_player_actor_retail.py). Actor matrix rows: forward is
 * -(+0xD0), position +0xE0. human+0x768 seat -> manager -> vehicle (retail
 * 0x590AA). 0x4CEE0(human) is the retail current-weapon getter of the fire
 * path. Health +0x98 / maximum +0x170 come from the lifted damage routine
 * 0x3F950 (test_actor_health_native.py) and still need confirming in play. */
#ifndef MERCENARIES_PLAYER_STATE_LOG_H
#define MERCENARIES_PLAYER_STATE_LOG_H

/* recomp_human_weapon_update_checkpoint identifies the player's human by it. */
#define PLAYER_STATE_HUMAN_VTABLE 0x002E32B8u

static struct {
    unsigned ticks;       /* checkpoint calls since the last sample */
    float game_seconds;   /* guest frame time since the last sample */
    ULONGLONG next_ms;
    uint32_t seq, human;
    int have_position;
    float position[3];
} player_state;

static int player_state_vec3(uint32_t address, float out[3])
{
    if (!dev_guest_address(address, 12)) return 0;
    for (unsigned i = 0; i < 3; ++i) out[i] = guest_f32(address + 4u * i);
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}

static void player_state_log_tick(uint32_t world, uint32_t delta_bits)
{
    int hz = recomp_dev_player_state_log_hz();
    if (hz <= 0 || !xbox_preview_log_enabled()) return;
    union { uint32_t bits; float seconds; } delta;
    delta.bits = delta_bits;
    if (isfinite(delta.seconds) && delta.seconds > 0.0f && delta.seconds < 1.0f)
        player_state.game_seconds += delta.seconds;
    ++player_state.ticks;
    ULONGLONG now = GetTickCount64();
    if (now < player_state.next_ms) return;
    player_state.next_ms = now + 1000u / (unsigned)hz;

    unsigned ticks = player_state.ticks;
    float elapsed = player_state.game_seconds;
    uint32_t seq = ++player_state.seq;
    player_state.ticks = 0;
    player_state.game_seconds = 0.0f;
    uint32_t state = g_xbox_mem_offset ? guest_u32(0x413F68u) : 0;
    if (!g_xbox_mem_offset || g_esp < 0x20000u || g_esp >= 0x4000000u ||
        guest_u32(0x413F6Cu) != 0x4249D707u || !recomp_lookup(0x8C7E0u)) {
        player_state.have_position = 0;
        xbox_preview_log_sample("player-state", "seq=%u ticks=%u dt=%.4f world=%08X state=%08X gameplay=0",
                                seq, ticks, elapsed, world, state);
        return;
    }

    recomp_saved_guest_cpu_context saved;
    recomp_save_guest_cpu_context(&saved);
    uint32_t stack = (g_esp - 0x4000u) & ~15u, name = 0x660E4490u; /* "player0" */
    uint32_t controller = dev_call(stack, 0x8C7E0u, 0, 1, &name);
    uint32_t human = dev_player_human(controller), weapon = 0;
    if (human && guest_u32(human) == PLAYER_STATE_HUMAN_VTABLE && recomp_lookup(0x4CEE0u))
        weapon = dev_call(stack, 0x4CEE0u, human, 0, NULL);
    recomp_restore_guest_cpu_context(&saved);

    float position[3] = {0}, back[3] = {0}, velocity[3] = {0};
    float target_position[3] = {0}, hp = 0.0f, hp_max = 0.0f, yaw = 0.0f;
    int has_position = human && player_state_vec3(human + 0xE0u, position);
    /* Heading of forward = -back in the x/z ground plane; 0 - x avoids "-0.0". */
    if (has_position && player_state_vec3(human + 0xD0u, back))
        yaw = atan2f(0.0f - back[0], 0.0f - back[2]) * 57.29578f;
    int vel_ok = has_position && player_state.have_position && player_state.human == human &&
                 elapsed > 0.0f;
    if (vel_ok)
        for (unsigned i = 0; i < 3; ++i) velocity[i] = (position[i] - player_state.position[i]) / elapsed;
    player_state.have_position = has_position;
    player_state.human = human;
    if (has_position) memcpy(player_state.position, position, sizeof(position));

    uint32_t target = dev_guest_address(controller, 0x14) ? guest_u32(controller + 0x10u) : 0;
    if (!dev_guest_address(target, 0xF0)) target = 0;
    if (target) player_state_vec3(target + 0xE0u, target_position);
    uint32_t seat = 0, manager = 0, vehicle = 0;
    if (human) {
        hp = guest_f32(human + 0x98u);
        hp_max = guest_f32(human + 0x170u);
        seat = guest_u32(human + 0x768u);
        manager = dev_guest_address(seat, 4) ? guest_u32(seat) : 0;
        vehicle = dev_guest_address(manager, 4) ? guest_u32(manager) : 0;
        if (!dev_guest_address(vehicle, 0xF0)) vehicle = 0;
    }
    if (!dev_guest_address(weapon, 0x1A4)) weapon = 0;
    xbox_preview_log_sample("player-state",
        "seq=%u ticks=%u dt=%.4f world=%08X state=%08X ctrl=%08X human=%08X hvt=%08X "
        "pos=%.3f,%.3f,%.3f back=%.3f,%.3f,%.3f yaw=%.1f vel=%.3f,%.3f,%.3f vel_ok=%d "
        "target=%08X tvt=%08X tpos=%.3f,%.3f,%.3f seat=%08X vehicle=%08X vvt=%08X vmodel=%08X "
        "hp=%.2f/%.2f weapon=%08X wvt=%08X wtype=%u",
        seq, ticks, elapsed, world, state, controller, human, human ? guest_u32(human) : 0,
        position[0], position[1], position[2], back[0], back[1], back[2], yaw,
        velocity[0], velocity[1], velocity[2], vel_ok,
        target, target ? guest_u32(target) : 0, target_position[0], target_position[1], target_position[2],
        seat, vehicle, vehicle ? guest_u32(vehicle) : 0, vehicle ? guest_u32(vehicle + 0x58u) : 0,
        hp, hp_max, weapon, weapon ? guest_u32(weapon) : 0, weapon ? guest_u32(weapon + 0x1A0u) : 0);
}
#endif
