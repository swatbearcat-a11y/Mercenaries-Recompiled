/* Included by recomp_manual.c through multiplayer_guest.h, after puppet_drive.h.
 * M1 experiment, [Developer] puppet_replay=1 (log output needs logging=1):
 * record the local player every world update (the control IDs its AI sends to
 * the human through vt[0x1E4], the human's matrix and its character
 * controller's movement state) and replay it puppet_replay_delay_ms of guest
 * time later on a puppet (puppet_drive.h). Off by default: with the option off
 * nothing here reads or writes guest state. Evidence for every address:
 * docs/multiplayer/NOTES.md. */
#ifndef MERCENARIES_PUPPET_REPLAY_H
#define MERCENARIES_PUPPET_REPLAY_H

#define PUPPET_EVENT_COUNT 4096u /* power of two */
#define PUPPET_FRAME_COUNT 2048u /* power of two; about a minute of world updates */
#define PUPPET_NAME "mp_puppet_1"

static struct {
    double now;                       /* guest seconds while recording */
    PuppetEvent events[PUPPET_EVENT_COUNT];
    uint32_t event_head, event_next;  /* free-running: recorded / next to replay */
    PuppetFrame frames[PUPPET_FRAME_COUNT];
    uint32_t frame_head;              /* free-running count of recorded frames */
    uint32_t human;                   /* player human being recorded */
    double next_log, target_time;
    uint32_t seq, dropped;
    PuppetActor actor;
} puppet;

/* Every control the player's AI sends to its human (multiplayer_guest.h's hook
 * at the entry of the player's control slot 0x5D5C0). Records only. */
static void puppet_replay_record_control(uint32_t human, uint32_t id, uint32_t value)
{
    if (recomp_dev_puppet_replay_delay_ms() <= 0 || !human || human != puppet.human) return;
    if (puppet.event_head - puppet.event_next >= PUPPET_EVENT_COUNT) {
        ++puppet.event_next;
        ++puppet.dropped;
    }
    PuppetEvent *event = &puppet.events[puppet.event_head++ & (PUPPET_EVENT_COUNT - 1u)];
    event->t = puppet.now;
    event->id = id;
    event->value = value;
}

static const PuppetFrame *puppet_frame_at(double t)
{
    uint32_t count = puppet.frame_head < PUPPET_FRAME_COUNT ? puppet.frame_head : PUPPET_FRAME_COUNT;
    for (uint32_t i = 1; i <= count; ++i) {
        const PuppetFrame *frame = &puppet.frames[(puppet.frame_head - i) & (PUPPET_FRAME_COUNT - 1u)];
        if (frame->t <= t) return frame;
    }
    return NULL;
}

/* Horizontal velocity of the recording over the 0.1 s before the frame (x, z). */
static void puppet_recorded_velocity(const PuppetFrame *target, float *velocity)
{
    const PuppetFrame *before = puppet_frame_at(target->t - 0.1);
    velocity[0] = velocity[1] = 0.0f;
    if (!before || target->t - before->t < 0.01) return;
    float dt = (float)(target->t - before->t);
    velocity[0] = (target->matrix[12] - before->matrix[12]) / dt;
    velocity[1] = (target->matrix[14] - before->matrix[14]) / dt;
}

/* PuppetEventSource: the recorded controls up to the replayed time. */
static int puppet_replay_next_event(void *context, PuppetEvent *event)
{
    (void)context;
    if (puppet.event_next == puppet.event_head) return 0;
    const PuppetEvent *next = &puppet.events[puppet.event_next & (PUPPET_EVENT_COUNT - 1u)];
    if (next->t > puppet.target_time) return 0;
    *event = *next;
    ++puppet.event_next;
    return 1;
}

static void puppet_replay_tick(uint32_t world, uint32_t delta_bits)
{
    (void)world;
    int delay_ms = recomp_dev_puppet_replay_delay_ms();
    if (delay_ms <= 0) return;
    union { uint32_t bits; float seconds; } delta;
    delta.bits = delta_bits;
    if (isfinite(delta.seconds) && delta.seconds > 0.0f && delta.seconds < 1.0f) puppet.now += delta.seconds;
    if (!g_xbox_mem_offset || g_esp < 0x20000u || g_esp >= 0x4000000u ||
        guest_u32(0x413F6Cu) != 0x4249D707u || guest_u32(0x413F68u) != 0xC2CBD863u ||
        !recomp_lookup(0x8C7E0u) || !recomp_lookup(0x1EC220u) || !recomp_lookup(0x174480u))
        return;

    recomp_saved_guest_cpu_context saved;
    recomp_save_guest_cpu_context(&saved);
    uint32_t stack = (g_esp - 0x4000u) & ~15u, name = 0x660E4490u; /* "player0" */
    uint32_t human = dev_player_human(dev_call(stack, 0x8C7E0u, 0, 1, &name));
    if (human != puppet.human) { /* new or respawned player: start a fresh recording */
        puppet.human = human;
        puppet.event_next = puppet.event_head;
        puppet.frame_head = 0;
    }
    if (human) {
        PuppetFrame *frame = &puppet.frames[puppet.frame_head++ & (PUPPET_FRAME_COUNT - 1u)];
        puppet_read(stack, human, frame);
        frame->t = puppet.now;
    }

    if (!puppet.actor.name[0]) strcpy(puppet.actor.name, PUPPET_NAME);
    puppet.target_time = puppet.now - delay_ms / 1000.0;
    const PuppetFrame *target = puppet_frame_at(puppet.target_time);
    int spawned = 0;
    uint32_t actor = puppet_actor(stack, &puppet.actor, puppet.now, target, human, &spawned);
    if (spawned) /* replay what the player did after the spawn point; skip older controls */
        while (puppet.event_next != puppet.event_head &&
               puppet.events[puppet.event_next & (PUPPET_EVENT_COUNT - 1u)].t <= puppet.target_time) ++puppet.event_next;

    int yaw_mode = recomp_dev_puppet_yaw_mode();
    uint32_t body = 0;
    if (actor && target) {
        float velocity[2];
        puppet_recorded_velocity(target, velocity);
        body = puppet_apply(stack, &puppet.actor, puppet.now, actor, human, target, velocity, puppet_replay_next_event,
                            NULL, recomp_dev_puppet_steer(), recomp_dev_puppet_correct_cm(), yaw_mode);
    }
    recomp_restore_guest_cpu_context(&saved);

    if (xbox_preview_log_enabled() && puppet.now >= puppet.next_log) {
        char fields[600];
        puppet.next_log = puppet.now + 0.2;
        puppet_log_fields(fields, sizeof(fields), &puppet.actor, actor, body, target, yaw_mode);
        xbox_preview_log_sample("puppet-replay", "seq=%u t=%.2f %s dropped=%u events=%u frames=%u", ++puppet.seq,
                                puppet.now, fields, puppet.dropped, puppet.event_head - puppet.event_next,
                                puppet.frame_head);
    }
}
#endif
