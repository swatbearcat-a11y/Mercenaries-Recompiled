/* Included by recomp_manual.c through multiplayer_guest.h, after puppet_drive.h.
 * M2, the guest-thread side of multiplayer. With [Multiplayer] mode host or join
 * (multiplayer_net.h), every world update it:
 *  - takes what the other players sent;
 *  - sends the local player's frame (puppet_drive.h) MP_STATE_HZ times a second
 *    and the controls the player's AI sent to its human since the last update;
 *  - drives one puppet per remote player MpConfig.interp_ms behind that
 *    player's newest update, interpolated between updates;
 *  - removes the puppet of a player who left or timed out.
 * Off by default: with the mode off it returns at once and touches nothing. */
#ifndef MERCENARIES_MULTIPLAYER_SESSION_H
#define MERCENARIES_MULTIPLAYER_SESSION_H
#include "multiplayer_net.h"

#define MP_STATE_HZ 20.0
#define MP_SNAPSHOTS 64u      /* power of two; about 3 s of updates */
#define MP_EVENTS 256u        /* power of two */
#define MP_OUT_EVENTS 63u     /* controls per events message */
#define MP_CORRECT_CM 300     /* teleport a puppet this far from where it should be */
#define MP_EXTRAPOLATE_MS 200 /* keep a puppet moving this long past its newest update */

enum { MP_MSG_STATE = MP_MSG_FIRST_APP, MP_MSG_EVENTS };
/* STATE, unreliable: type, player, seq u16, sender_ms f64, position f32[3],
 * yaw f32, input f32[2], move frame f32[3], walk/run speeds f32[2], gait,
 * run allowed, strafe, 0. EVENTS, reliable: type, player, count u16, then
 * count x (sender_ms f64, control id u32, value u32). Little-endian; times are
 * the sender's mp_net_now_ms. */
#define MP_STATE_SIZE 60u
#define MP_EVENT_SIZE 16u

typedef struct MpSnapshot { double sender_ms; PuppetFrame frame; } MpSnapshot;

typedef struct MpRemote {
    int active, leaving;
    MpSnapshot snapshots[MP_SNAPSHOTS];
    uint32_t snapshot_head;          /* free-running count */
    PuppetEvent events[MP_EVENTS];   /* t in the sender's milliseconds */
    uint32_t event_head, event_next, dropped_events;
    /* Receive clock minus sender clock: the smallest sample of this and the
     * previous two-second window, so jitter delays nothing but the clock can
     * still drift. */
    double offset_ms, window_start_ms, window_min, previous_min;
    double render_ms, last_receive_ms, next_log;
    uint32_t received;
    PuppetActor actor;
} MpRemote;

static struct {
    MpNet *net;
    int failed;
    double now;                      /* guest seconds */
    double next_send_ms, next_log;
    uint32_t human;
    uint16_t seq;
    PuppetEvent out[MP_OUT_EVENTS];
    uint32_t out_count, out_dropped, sent_states, sent_events;
    MpRemote remotes[MP_MAX_PLAYERS];
} mp;

static uint8_t *mp_put(uint8_t *p, const void *value, size_t size)
{
    memcpy(p, value, size);
    return p + size;
}

static const uint8_t *mp_get(const uint8_t *p, void *value, size_t size)
{
    memcpy(value, p, size);
    return p + size;
}

/* An upright human's matrix for a position and game yaw (puppet_yaw of the back
 * row gives the yaw back). */
static void mp_matrix(float *matrix, const float *position, float yaw)
{
    float c = cosf(yaw), s = sinf(yaw);
    float rows[16] = {c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, position[0], position[1], position[2], 1};
    memcpy(matrix, rows, sizeof(rows));
}

static size_t mp_encode_state(uint8_t *out, uint16_t seq, double sender_ms, const PuppetFrame *frame)
{
    float position[3] = {frame->matrix[12], frame->matrix[13], frame->matrix[14]};
    float yaw = puppet_yaw(frame->matrix + 8);
    uint8_t *p = out;
    *p++ = MP_MSG_STATE;
    *p++ = 0;
    p = mp_put(p, &seq, 2);
    p = mp_put(p, &sender_ms, 8);
    p = mp_put(p, position, 12);
    p = mp_put(p, &yaw, 4);
    p = mp_put(p, frame->move, 8);
    p = mp_put(p, frame->frame, 12);
    p = mp_put(p, frame->speed, 8);
    *p++ = frame->gait;
    *p++ = frame->run_allowed;
    *p++ = frame->strafe;
    *p++ = 0;
    return (size_t)(p - out);
}

static int mp_decode_state(const uint8_t *in, size_t size, double *sender_ms, PuppetFrame *frame)
{
    float position[3], yaw;
    if (size != MP_STATE_SIZE || in[0] != MP_MSG_STATE) return 0;
    memset(frame, 0, sizeof(*frame));
    const uint8_t *p = in + 4;
    p = mp_get(p, sender_ms, 8);
    p = mp_get(p, position, 12);
    p = mp_get(p, &yaw, 4);
    p = mp_get(p, frame->move, 8);
    p = mp_get(p, frame->frame, 12);
    p = mp_get(p, frame->speed, 8);
    frame->gait = p[0];
    frame->run_allowed = p[1];
    frame->strafe = p[2];
    float check = position[0] + position[1] + position[2] + yaw + frame->move[0] + frame->move[1] + frame->frame[0] +
                  frame->frame[1] + frame->frame[2] + frame->speed[0] + frame->speed[1];
    if (!isfinite(check) || !isfinite(*sender_ms)) return 0;
    mp_matrix(frame->matrix, position, yaw);
    frame->t = *sender_ms / 1000.0;
    return 1;
}

/* The local player's controls (multiplayer_guest.h's hook at the entry of the
 * player's control slot 0x5D5C0), sent with the next update. Movement (0x1E,
 * 0x1F) travels in the frames; 0x13 repeats every update while the stick is
 * held and does nothing on an NPC (it falls through to 0x41C50). */
static void mp_session_record_control(uint32_t human, uint32_t id, uint32_t value)
{
    if (mp_config()->mode == MP_MODE_OFF || !human || human != mp.human) return;
    if (id == 0x1Eu || id == 0x1Fu || id == 0x13u) return;
    if (mp.out_count >= MP_OUT_EVENTS) {
        ++mp.out_dropped;
        return;
    }
    PuppetEvent *event = &mp.out[mp.out_count++];
    event->t = mp_net_now_ms();
    event->id = id;
    event->value = value;
}

static void mp_remote_reset(MpRemote *r)
{
    memset(r, 0, sizeof(*r));
}

static void mp_remote_state(MpRemote *r, double sender_ms, double receive_ms, const PuppetFrame *frame)
{
    if (r->snapshot_head &&
        sender_ms <= r->snapshots[(r->snapshot_head - 1u) & (MP_SNAPSHOTS - 1u)].sender_ms) return;
    MpSnapshot *snapshot = &r->snapshots[r->snapshot_head++ & (MP_SNAPSHOTS - 1u)];
    snapshot->sender_ms = sender_ms;
    snapshot->frame = *frame;
    double sample = receive_ms - sender_ms;
    if (!r->received || receive_ms - r->window_start_ms >= 2000.0) {
        r->previous_min = r->received ? r->window_min : sample;
        r->window_min = sample;
        r->window_start_ms = receive_ms;
    } else if (sample < r->window_min) {
        r->window_min = sample;
    }
    r->offset_ms = r->window_min < r->previous_min ? r->window_min : r->previous_min;
    r->last_receive_ms = receive_ms;
    ++r->received;
}

static void mp_remote_events(MpRemote *r, const uint8_t *in, size_t size)
{
    uint16_t count;
    if (size < 4) return;
    memcpy(&count, in + 2, 2);
    if (size != 4u + (size_t)count * MP_EVENT_SIZE) return;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *p = in + 4 + (size_t)i * MP_EVENT_SIZE;
        if (r->event_head - r->event_next >= MP_EVENTS) {
            ++r->event_next;
            ++r->dropped_events;
        }
        PuppetEvent *event = &r->events[r->event_head++ & (MP_EVENTS - 1u)];
        p = mp_get(p, &event->t, 8);
        p = mp_get(p, &event->id, 4);
        mp_get(p, &event->value, 4);
    }
}

/* PuppetEventSource: the remote player's controls up to the time shown. */
static int mp_remote_next_event(void *context, PuppetEvent *event)
{
    MpRemote *r = (MpRemote *)context;
    if (r->event_next == r->event_head) return 0;
    const PuppetEvent *next = &r->events[r->event_next & (MP_EVENTS - 1u)];
    if (next->t > r->render_ms) return 0;
    *event = *next;
    ++r->event_next;
    return 1;
}

static float mp_angle_lerp(float a, float b, float u)
{
    float d = b - a;
    while (d > 3.14159265f) d -= 6.28318531f;
    while (d < -3.14159265f) d += 6.28318531f;
    return a + d * u;
}

/* The remote player at sender time render_ms: interpolated between the two
 * updates around it, or carried on along the last velocity for up to
 * MP_EXTRAPOLATE_MS past the newest and held where that ends. velocity is x, z
 * in m/s (0 once held). 0 when nothing has arrived yet. */
static int mp_remote_target(const MpRemote *r, double render_ms, PuppetFrame *target, float *velocity)
{
    uint32_t count = r->snapshot_head < MP_SNAPSHOTS ? r->snapshot_head : MP_SNAPSHOTS;
    velocity[0] = velocity[1] = 0.0f;
    if (!count) return 0;
    uint32_t newest = r->snapshot_head - 1u, a = newest, i = 0;
    while (i + 1 < count && r->snapshots[a & (MP_SNAPSHOTS - 1u)].sender_ms > render_ms) {
        --a;
        ++i;
    }
    const MpSnapshot *sa = &r->snapshots[a & (MP_SNAPSHOTS - 1u)];
    *target = sa->frame;
    if (a != newest) {
        const MpSnapshot *sb = &r->snapshots[(a + 1u) & (MP_SNAPSHOTS - 1u)];
        double span = sb->sender_ms - sa->sender_ms;
        float u = span > 0.0 ? (float)puppet_clamp((float)((render_ms - sa->sender_ms) / span), 0.0f, 1.0f) : 1.0f;
        for (unsigned k = 12; k < 15; ++k) target->matrix[k] += (sb->frame.matrix[k] - sa->frame.matrix[k]) * u;
        float yaw = mp_angle_lerp(puppet_yaw(sa->frame.matrix + 8), puppet_yaw(sb->frame.matrix + 8), u);
        float position[3] = {target->matrix[12], target->matrix[13], target->matrix[14]};
        mp_matrix(target->matrix, position, yaw);
        if (span > 0.0) {
            velocity[0] = (float)((sb->frame.matrix[12] - sa->frame.matrix[12]) * 1000.0 / span);
            velocity[1] = (float)((sb->frame.matrix[14] - sa->frame.matrix[14]) * 1000.0 / span);
        }
    } else if (count > 1 && render_ms > sa->sender_ms) {
        const MpSnapshot *before = &r->snapshots[(newest - 1u) & (MP_SNAPSHOTS - 1u)];
        double span = sa->sender_ms - before->sender_ms, ahead = render_ms - sa->sender_ms;
        if (span > 0.0) {
            float vx = (float)((sa->frame.matrix[12] - before->frame.matrix[12]) * 1000.0 / span);
            float vz = (float)((sa->frame.matrix[14] - before->frame.matrix[14]) * 1000.0 / span);
            double carried = ahead < MP_EXTRAPOLATE_MS ? ahead : MP_EXTRAPOLATE_MS;
            target->matrix[12] += vx * (float)(carried / 1000.0);
            target->matrix[14] += vz * (float)(carried / 1000.0);
            if (ahead < MP_EXTRAPOLATE_MS) {
                velocity[0] = vx;
                velocity[1] = vz;
            }
        }
    }
    target->t = render_ms / 1000.0;
    return 1;
}

static void mp_session_incoming(const MpIncoming *in)
{
    MpRemote *r = in->player < MP_MAX_PLAYERS ? &mp.remotes[in->player] : NULL;
    switch (in->kind) {
    case MP_IN_MESSAGE:
        if (!r || r->leaving || in->size < 2) break;
        if (in->data[0] == MP_MSG_STATE) {
            double sender_ms;
            PuppetFrame frame;
            if (!mp_decode_state(in->data, in->size, &sender_ms, &frame)) break;
            if (!r->active) {
                mp_remote_reset(r);
                r->active = 1;
                snprintf(r->actor.name, sizeof(r->actor.name), "mp_player_%u", (unsigned)in->player);
                xbox_preview_log_event("multiplayer", "first update from player %u", (unsigned)in->player);
            }
            mp_remote_state(r, sender_ms, in->receive_ms, &frame);
        } else if (in->data[0] == MP_MSG_EVENTS && r->active) {
            mp_remote_events(r, in->data, in->size);
        }
        break;
    case MP_IN_JOINED:
        xbox_preview_log_event("multiplayer", "player %u joined", (unsigned)in->player);
        break;
    case MP_IN_CONNECTED:
        xbox_preview_log_event("multiplayer", "connected to the host as player %u", (unsigned)in->player);
        break;
    case MP_IN_LEFT:
        if (r && r->active) r->leaving = 1;
        xbox_preview_log_event("multiplayer", "player %u left", (unsigned)in->player);
        break;
    case MP_IN_DISCONNECTED:
        for (unsigned i = 0; i < MP_MAX_PLAYERS; ++i)
            if (mp.remotes[i].active) mp.remotes[i].leaving = 1;
        xbox_preview_log_event("multiplayer", "lost the host (reason %u); reconnecting",
                               in->size ? (unsigned)in->data[0] : 0u);
        break;
    }
}

static void mp_send_state(uint32_t stack, uint32_t human, double local_ms)
{
    uint8_t message[MP_STATE_SIZE];
    PuppetFrame frame;
    puppet_read(stack, human, &frame);
    size_t size = mp_encode_state(message, ++mp.seq, local_ms, &frame);
    if (mp_net_send(mp.net, message, size, 0)) ++mp.sent_states;
}

static void mp_send_events(void)
{
    uint8_t message[4 + MP_OUT_EVENTS * MP_EVENT_SIZE];
    uint16_t count = (uint16_t)mp.out_count;
    uint8_t *p = message;
    *p++ = MP_MSG_EVENTS;
    *p++ = 0;
    p = mp_put(p, &count, 2);
    for (uint32_t i = 0; i < mp.out_count; ++i) {
        p = mp_put(p, &mp.out[i].t, 8);
        p = mp_put(p, &mp.out[i].id, 4);
        p = mp_put(p, &mp.out[i].value, 4);
    }
    if (mp_net_send(mp.net, message, (size_t)(p - message), 1)) mp.sent_events += mp.out_count;
    mp.out_count = 0;
}

static void mp_session_tick(uint32_t world, uint32_t delta_bits)
{
    (void)world;
    if (mp_config()->mode == MP_MODE_OFF || mp.failed) return;
    if (!mp.net) {
        mp.net = mp_net_default();
        if (!mp.net) {
            mp.failed = 1;
            xbox_preview_log_event("multiplayer", "could not start %s on port %u", mp_config()->mode == MP_MODE_HOST ?
                                   "hosting" : "joining", (unsigned)mp_config()->port);
            return;
        }
        xbox_preview_log_event("multiplayer", "started: %s %s port %u", mp_config()->mode == MP_MODE_HOST ? "host" :
                               "join", mp_config()->mode == MP_MODE_HOST ? mp_config()->bind : mp_config()->address,
                               (unsigned)mp_config()->port);
    }
    union { uint32_t bits; float seconds; } delta;
    delta.bits = delta_bits;
    if (isfinite(delta.seconds) && delta.seconds > 0.0f && delta.seconds < 1.0f) mp.now += delta.seconds;
    double local_ms = mp_net_now_ms();
    MpIncoming in;
    for (unsigned budget = 256; budget && mp_net_poll(mp.net, &in); --budget) mp_session_incoming(&in);
    if (!g_xbox_mem_offset || g_esp < 0x20000u || g_esp >= 0x4000000u ||
        guest_u32(0x413F6Cu) != 0x4249D707u || guest_u32(0x413F68u) != 0xC2CBD863u ||
        !recomp_lookup(0x8C7E0u) || !recomp_lookup(0x1EC220u) || !recomp_lookup(0x174480u))
        return;

    recomp_saved_guest_cpu_context saved;
    recomp_save_guest_cpu_context(&saved);
    uint32_t stack = (g_esp - 0x4000u) & ~15u, name = 0x660E4490u; /* "player0" */
    uint32_t human = dev_player_human(dev_call(stack, 0x8C7E0u, 0, 1, &name));
    if (human != mp.human) {
        mp.human = human;
        mp.out_count = 0;
    }
    for (unsigned i = 0; i < MP_MAX_PLAYERS; ++i) {
        MpRemote *r = &mp.remotes[i];
        if (!r->leaving) continue;
        puppet_remove(stack, &r->actor);
        mp_remote_reset(r);
    }
    if (human && local_ms >= mp.next_send_ms) {
        mp_send_state(stack, human, local_ms);
        mp.next_send_ms = local_ms - mp.next_send_ms < 1000.0 / MP_STATE_HZ ? mp.next_send_ms + 1000.0 / MP_STATE_HZ
                                                                            : local_ms + 1000.0 / MP_STATE_HZ;
    }
    if (mp.out_count) mp_send_events();

    char fields[600];
    for (unsigned i = 0; i < MP_MAX_PLAYERS; ++i) {
        MpRemote *r = &mp.remotes[i];
        PuppetFrame target;
        float velocity[2];
        if (!r->active) continue;
        r->render_ms = local_ms - r->offset_ms - (double)mp_config()->interp_ms;
        if (!mp_remote_target(r, r->render_ms, &target, velocity)) continue;
        int spawned = 0;
        uint32_t actor = puppet_actor(stack, &r->actor, mp.now, &target, human, &spawned);
        if (spawned) /* controls from before the puppet existed don't apply */
            while (r->event_next != r->event_head && r->events[r->event_next & (MP_EVENTS - 1u)].t <= r->render_ms)
                ++r->event_next;
        uint32_t body = puppet_apply(stack, &r->actor, mp.now, actor, human, &target, velocity, mp_remote_next_event, r,
                                     1, MP_CORRECT_CM, 1);
        if (xbox_preview_log_enabled() && mp.now >= r->next_log) {
            const MpSnapshot *newest = &r->snapshots[(r->snapshot_head - 1u) & (MP_SNAPSHOTS - 1u)];
            r->next_log = mp.now + 0.2;
            puppet_log_fields(fields, sizeof(fields), &r->actor, actor, body, &target, 1);
            xbox_preview_log_sample("mp-puppet", "player=%u t=%.2f behind_ms=%.0f age_ms=%.0f offset_ms=%.1f updates=%u %s",
                                    i, mp.now, newest->sender_ms - r->render_ms, local_ms - r->last_receive_ms,
                                    r->offset_ms, r->received, fields);
        }
    }
    recomp_restore_guest_cpu_context(&saved);

    if (xbox_preview_log_enabled() && mp.now >= mp.next_log) {
        unsigned remotes = 0;
        for (unsigned i = 0; i < MP_MAX_PLAYERS; ++i) remotes += mp.remotes[i].active != 0;
        mp.next_log = mp.now + 1.0;
        xbox_preview_log_sample("multiplayer", "t=%.2f me=%u peers=%u remotes=%u sent_states=%u sent_events=%u "
                                "dropped_events=%u", mp.now, (unsigned)mp_net_local_player(mp.net),
                                mp_net_peer_count(mp.net), remotes, mp.sent_states, mp.sent_events, mp.out_dropped);
    }
}
#endif
