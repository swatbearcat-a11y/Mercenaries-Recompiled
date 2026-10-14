/* Included by recomp_manual.c through multiplayer_guest.h, after dev_spawn.h
 * (dev_call, dev_property, dev_human_available, dev_load_layer, dev_player_human).
 * M1 experiment, [Developer] puppet_replay=1 (log output needs logging=1):
 * record the local player's control IDs (what its AI sends to the human through
 * vt[0x1E4]), the human's matrix and its character controller's movement state
 * every world update, spawn an NPC human with aiType=none (so no AI competes for
 * its controls), and replay the recording on it puppet_replay_delay_ms of guest
 * time later: discrete controls through its own vt[0x1E4], movement as described
 * at PuppetFrame. The recorded heading is applied with vt[0x68], and the
 * recorded matrix is used as a vt[0x30] teleport (the respawn teleport) when the
 * puppet drifts too far.
 * Off by default: with the option off nothing here reads or writes guest state.
 * Evidence for every address: docs/multiplayer/NOTES.md. */
#ifndef MERCENARIES_PUPPET_REPLAY_H
#define MERCENARIES_PUPPET_REPLAY_H

#define PUPPET_EVENT_COUNT 4096u /* power of two */
#define PUPPET_FRAME_COUNT 2048u /* power of two; about a minute of world updates */
#define PUPPET_NAME "mp_puppet_1"
#define PUPPET_TEMPLATE "template_allies_soldier"
#define PUPPET_STEER_GAIN 1.5f      /* m/s of correction per metre of offset */
#define PUPPET_STEER_TURN 1.5707964f /* at most 90 degrees */

typedef struct PuppetEvent { double t; uint32_t id, value; } PuppetEvent;
/* Character controller: human->vt[0x3BC]() returns human + 0x280 for both human
 * vtables. Its locomotion update 0x13D8C0 moves the human along
 *   +0x1C  move frame, a unit vector (default (0,0,-1)): the move input is
 *          relative to it, not to the body's facing;
 *   +0x30  strafe and +0x34 forward input (setters 0x13C540/0x13C570; the NPC
 *          control slot 0x53D20 stores IDs 0x1E/0x1F there unchanged; the
 *          player's slot 0x5D5C0 derives them from the stick and camera);
 * at a speed set by the gait it picks into +0x0C: 0 idle (input under 0.01),
 * 1 walk (+0x44 m/s), 2 run (+0x48 m/s). Running needs +0x65 (run allowed) and
 * input of at least 0.95 to start (0.7 to keep running, 0.6 for the player).
 * The human's 0x4EA90 sets the two speeds from the root motion of its walk and
 * run animations (controller setters 0x13C5B0/0x13C5C0), so they depend on the
 * character: 2.27/8.47 for the player's merc, 1.34/4.93 for the allied soldier.
 * +0x68 is strafe mode (set through the human's 0x4E970).
 * Each world update records these from the player and replays them on the
 * puppet: the move frame through the human setter 0x7E910 (what the game's AI
 * steering 0x7F520 uses), the speeds through the controller setters, the input
 * through 0x1E/0x1F scaled into the recorded gait, all steered toward the
 * recorded position (puppet_drive). m1-replay-03 replayed only the input: the
 * puppet ran (5 m/s) along its spawn frame while the player walked (2.3 m/s,
 * running is off in the opening C-17). */
typedef struct PuppetFrame {
    double t;
    float matrix[16];
    float move[2];   /* +0x30, +0x34 */
    float frame[3];  /* +0x1C */
    float speed[2];  /* +0x44 walk, +0x48 run */
    uint8_t gait, run_allowed, strafe;
} PuppetFrame;

static struct {
    double now;                       /* guest seconds while recording */
    PuppetEvent events[PUPPET_EVENT_COUNT];
    uint32_t event_head, event_next;  /* free-running: recorded / next to replay */
    PuppetFrame frames[PUPPET_FRAME_COUNT];
    uint32_t frame_head;              /* free-running count of recorded frames */
    uint32_t human;                   /* player human being recorded */
    uint32_t guid;                    /* puppet spore GUID; 0 while there is none */
    double next_spawn, next_log;
    uint32_t seq, spawns, spawn_failures, fed, teleports, dropped;
    uint32_t fed_ids[8], fed_id_count;
    int catching_up;                  /* walking to a recording that stands still */
    float turn, speed_scale;          /* last steering, for the log */
    double stuck_since;               /* start of the current stuck check; 0 when not checking */
    float stuck_at[2];                /* puppet x, z when it started */
    uint32_t stuck_teleports;
    int occupied;                     /* the local player stands on the replayed spot */
} puppet;

/* Called from the generated hook at the entry of the player human's control
 * slot (sub_0005D5C0, vtable 0x2E32B8 slot +0x1E4): every control the player's
 * AI sends to its human. Records only; never touches guest state. */
void recomp_multiplayer_player_control(uint32_t human, uint32_t id, uint32_t value)
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

/* The game's yaw (radians) for a matrix back row (+0xD0). Actor_GetYaw's helper
 * 0x1F8860 computes -atan2(-x, -z) of the forward vector, which is -back. The
 * first automated run (m1-replay-01) confirmed it: the back-vector reading
 * turned the puppet exactly 180 degrees from the player. */
static float puppet_yaw(const float *back)
{
    return -atan2f(back[0], back[2]);
}

/* Scale for the recorded input so that the puppet, an NPC that may run, picks
 * the player's gait: walk under every run threshold, run at full input. */
static float puppet_gait_scale(const PuppetFrame *frame)
{
    float magnitude = sqrtf(frame->move[0] * frame->move[0] + frame->move[1] * frame->move[1]);
    if (magnitude <= 0.01f) return 1.0f;
    if (frame->gait == 2) return 1.0f / magnitude;
    if ((frame->gait == 1 || !frame->run_allowed) && magnitude > 0.5f) return 0.5f / magnitude;
    return 1.0f;
}

static float puppet_clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

/* What the puppet gets this update: the recorded move frame and input (scaled
 * into the recorded gait) and a factor for the recorded speeds. The input alone
 * doesn't say where the player went: sliding along a wall, being pushed and
 * animations that move the player without input (standing up from a seat) are
 * not in it, and a puppet a little off the recorded path collides differently.
 * So with steering on, the puppet heads for the recorded velocity (over the
 * last 0.1 s of the recording) plus PUPPET_STEER_GAIN times its horizontal
 * offset (ex, ez): the frame turns (at most 90 degrees) and the speed scales
 * (0.25 to 1.5) while the recording has input; without input, the puppet walks
 * when that velocity is over 0.3 m/s or it is more than 0.3 m away, until it is
 * within 0.1 m and the recording stands still. */
typedef struct PuppetDrive { float frame[3], move[2], speed_scale; } PuppetDrive;

static PuppetDrive puppet_drive(const PuppetFrame *target, const float *velocity, float ex, float ez, int steer)
{
    PuppetDrive drive;
    float scale = puppet_gait_scale(target);
    memcpy(drive.frame, target->frame, sizeof(drive.frame));
    drive.move[0] = target->move[0] * scale;
    drive.move[1] = target->move[1] * scale;
    drive.speed_scale = 1.0f;
    puppet.turn = 0.0f;
    float input = sqrtf(drive.move[0] * drive.move[0] + drive.move[1] * drive.move[1]);
    float vx = velocity[0] + PUPPET_STEER_GAIN * ex, vz = velocity[1] + PUPPET_STEER_GAIN * ez;
    float wanted = sqrtf(vx * vx + vz * vz), moving = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    float distance = sqrtf(ex * ex + ez * ez);
    float fx = drive.frame[0], fz = drive.frame[2], length = sqrtf(fx * fx + fz * fz);
    if (!steer || length < 0.5f) {
        puppet.catching_up = 0;
    } else if (input > 0.01f) {
        puppet.catching_up = 0;
        float speed = target->speed[target->gait == 2 ? 1 : 0];
        if (speed > 0.0f) {
            /* Input direction in the world: forward along the frame, strafe
             * along its right (-z, x), as the locomotion update 0x13D8C0 adds
             * their yaws. */
            fx /= length;
            fz /= length;
            float dx = drive.move[1] * fx - drive.move[0] * fz, dz = drive.move[1] * fz + drive.move[0] * fx;
            float d = sqrtf(dx * dx + dz * dz);
            dx /= d;
            dz /= d;
            float turn = wanted > 0.01f ? puppet_clamp(atan2f(dz * vx - dx * vz, dx * vx + dz * vz),
                                                       -PUPPET_STEER_TURN, PUPPET_STEER_TURN) : 0.0f;
            float c = cosf(turn), s = sinf(turn);
            drive.frame[0] = fx * c + fz * s;
            drive.frame[1] = 0.0f;
            drive.frame[2] = fz * c - fx * s;
            float along = vx * (dx * c + dz * s) + vz * (dz * c - dx * s); /* wanted velocity along the turned path */
            drive.speed_scale = puppet_clamp(along / speed, 0.25f, 1.5f);
            puppet.turn = turn;
        }
    } else if (wanted > 0.01f && (moving > 0.3f || distance > 0.3f || (puppet.catching_up && distance > 0.1f))) {
        puppet.catching_up = 1;
        drive.frame[0] = vx / wanted;
        drive.frame[1] = 0.0f;
        drive.frame[2] = vz / wanted;
        drive.move[0] = 0.0f;
        drive.move[1] = 0.5f; /* walk */
        if (target->speed[0] > 0.0f) drive.speed_scale = puppet_clamp(wanted / target->speed[0], 0.3f, 1.0f);
    } else {
        puppet.catching_up = 0;
    }
    puppet.speed_scale = drive.speed_scale;
    return drive;
}

/* A puppet that is driven (fed input) more than 0.5 m from the recording but
 * moves less than 0.25 m in a second is stuck (m1-replay-07: on a bench it was
 * spawned on, against cargo the player slid past); the caller teleports it. */
static int puppet_stuck(float x, float z, float error, int driven)
{
    if (!driven || error <= 0.5f) {
        puppet.stuck_since = 0.0;
        return 0;
    }
    if (puppet.stuck_since <= 0.0) {
        puppet.stuck_since = puppet.now;
        puppet.stuck_at[0] = x;
        puppet.stuck_at[1] = z;
        return 0;
    }
    if (puppet.now - puppet.stuck_since < 1.0) return 0;
    float dx = x - puppet.stuck_at[0], dz = z - puppet.stuck_at[1];
    puppet.stuck_since = 0.0;
    return dx * dx + dz * dz < 0.25f * 0.25f;
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

static int puppet_virtual(uint32_t actor, uint32_t slot, uint32_t *function)
{
    uint32_t vt = guest_u32(actor);
    if (!dev_guest_address(vt, slot + 4)) return 0;
    *function = guest_u32(vt + slot);
    return recomp_lookup(*function) != NULL;
}

/* The human's character controller (vt[0x3BC]), or 0. */
static uint32_t puppet_controller(uint32_t stack, uint32_t human)
{
    uint32_t function = 0;
    if (!puppet_virtual(human, 0x3BC, &function)) return 0;
    uint32_t controller = dev_call(stack, function, human, 0, NULL);
    return dev_guest_address(controller, 0x70) ? controller : 0;
}

/* Spawn the puppet like the F9 troop spawns (0x174480 with a property list),
 * plus aiType=none, at the recorded matrix. Returns the spore GUID or 0. */
static uint32_t puppet_spawn(uint32_t stack, const float *matrix)
{
    if (!dev_human_available(stack, PUPPET_TEMPLATE)) dev_load_layer(stack, "template_humans");
    if (!dev_human_available(stack, PUPPET_TEMPLATE)) return 0;
    uint32_t name = stack + 0x100, placement = stack + 0x140, prop = stack + 0x400, list = stack + 0x600;
    memcpy(guest_ptr(placement), matrix, 64);
    dev_call(stack, 0x1EB010, list, 0, NULL);
    dev_property(stack, prop, list, "name", PUPPET_NAME);
    dev_property(stack, prop, list, "aiType", "none");
    dev_property(stack, prop, list, "dieuponhibernation", "true");
    dev_property(stack, prop, list, "path", "none");
    dev_property(stack, prop, list, "encounter", "none");
    dev_property(stack, prop, list, "squad", "none");
    strcpy(guest_ptr(name), PUPPET_NAME);
    uint32_t args[] = {dev_hash(PUPPET_TEMPLATE), name, placement, list, 0};
    uint32_t actor = dev_call(stack, 0x174480, 0x4031B0, 5, args);
    if (!dev_guest_address(actor, 0x770)) return 0;
    dev_transient_actor(actor);
    *(uint8_t *)guest_ptr(actor + 0x124) |= 1u; /* invincible, as Actor_SetInvincible */
    uint32_t spore = guest_u32(actor + 8);
    return dev_guest_address(spore, 0x2C) ? guest_u32(spore + 0x28) : 0;
}

static void puppet_log(uint32_t actor, uint32_t controller, const PuppetFrame *target, int yaw_mode)
{
    float position[3] = {0}, back[3] = {0}, frame[3] = {0}, speed[2] = {0}, error = -1.0f, yaw_error = 0.0f;
    uint32_t ai = 0, gait = 0;
    if (actor) {
        for (unsigned i = 0; i < 3; ++i) {
            position[i] = guest_f32(actor + 0xE0u + 4u * i);
            back[i] = guest_f32(actor + 0xD0u + 4u * i);
        }
        ai = guest_u32(actor + 0x200u);
    }
    if (controller) {
        for (unsigned i = 0; i < 3; ++i) frame[i] = guest_f32(controller + 0x1Cu + 4u * i);
        for (unsigned i = 0; i < 2; ++i) speed[i] = guest_f32(controller + 0x44u + 4u * i);
        gait = guest_u32(controller + 0xCu);
    }
    if (actor && target) {
        float dx = position[0] - target->matrix[12], dy = position[1] - target->matrix[13];
        float dz = position[2] - target->matrix[14];
        error = sqrtf(dx * dx + dy * dy + dz * dz);
        yaw_error = (puppet_yaw(back) - puppet_yaw(target->matrix + 8)) * 57.29578f;
        while (yaw_error > 180.0f) yaw_error -= 360.0f;
        while (yaw_error < -180.0f) yaw_error += 360.0f;
    }
    char ids[64] = "";
    for (uint32_t i = 0, used = 0; i < puppet.fed_id_count && used + 4 < sizeof(ids); ++i)
        used += (uint32_t)snprintf(ids + used, sizeof(ids) - used, i ? ",%02X" : "%02X", puppet.fed_ids[i]);
    xbox_preview_log_sample("puppet-replay",
        "seq=%u t=%.2f actor=%08X ai=%08X pos=%.3f,%.3f,%.3f back=%.3f,%.3f,%.3f "
        "rec=%.3f,%.3f,%.3f move=%.3f,%.3f frame=%.3f,%.3f,%.3f rec_frame=%.3f,%.3f,%.3f gait=%u rec_gait=%u "
        "speeds=%.2f,%.2f turn=%.1f speed_scale=%.2f catch_up=%d occupied=%d "
        "err=%.3f yaw_err=%.1f yaw_mode=%d fed=%u ids=%s teleports=%u stuck_teleports=%u spawns=%u "
        "spawn_failures=%u dropped=%u events=%u frames=%u",
        ++puppet.seq, puppet.now, actor, ai, position[0], position[1], position[2], back[0], back[1], back[2],
        target ? target->matrix[12] : 0.0f, target ? target->matrix[13] : 0.0f,
        target ? target->matrix[14] : 0.0f, target ? target->move[0] : 0.0f, target ? target->move[1] : 0.0f,
        frame[0], frame[1], frame[2], target ? target->frame[0] : 0.0f, target ? target->frame[1] : 0.0f,
        target ? target->frame[2] : 0.0f, gait, target ? target->gait : 0u, speed[0], speed[1],
        puppet.turn * 57.29578f, puppet.speed_scale, puppet.catching_up, puppet.occupied,
        error, yaw_error, yaw_mode, puppet.fed, ids[0] ? ids : "-",
        puppet.teleports, puppet.stuck_teleports, puppet.spawns, puppet.spawn_failures, puppet.dropped,
        puppet.event_head - puppet.event_next, puppet.frame_head);
    puppet.fed = 0;
    puppet.fed_id_count = 0;
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
        memset(frame, 0, sizeof(*frame));
        frame->t = puppet.now;
        memcpy(frame->matrix, guest_ptr(human + 0xB0u), sizeof(frame->matrix));
        uint32_t body = puppet_controller(stack, human);
        if (body) {
            frame->move[0] = guest_f32(body + 0x30u);
            frame->move[1] = guest_f32(body + 0x34u);
            for (unsigned i = 0; i < 3; ++i) frame->frame[i] = guest_f32(body + 0x1Cu + 4u * i);
            frame->speed[0] = guest_f32(body + 0x44u);
            frame->speed[1] = guest_f32(body + 0x48u);
            frame->gait = (uint8_t)guest_u32(body + 0xCu);
            frame->run_allowed = guest_u8(body + 0x65u);
            frame->strafe = guest_u8(body + 0x68u);
        }
    }

    double target_time = puppet.now - delay_ms / 1000.0;
    const PuppetFrame *target = puppet_frame_at(target_time);
    uint32_t actor = 0;
    if (puppet.guid) {
        uint32_t guid = puppet.guid;
        actor = dev_call(stack, 0x1EC220u, 0, 1, &guid);
        if (!dev_guest_address(actor, 0x770)) {
            xbox_preview_log_event("puppet", "lost guid=%08X t=%.2f", puppet.guid, puppet.now);
            puppet.guid = 0;
            actor = 0;
            puppet.next_spawn = puppet.now + 2.0;
        }
    }
    if (!puppet.guid && target && puppet.now >= puppet.next_spawn) {
        puppet.guid = puppet_spawn(stack, target->matrix);
        if (puppet.guid) {
            uint32_t guid = puppet.guid;
            actor = dev_call(stack, 0x1EC220u, 0, 1, &guid);
            ++puppet.spawns;
            uint32_t mine = human ? puppet_controller(stack, human) : 0;
            uint32_t its = dev_guest_address(actor, 0x770) ? puppet_controller(stack, actor) : 0;
            xbox_preview_log_event("puppet", "spawned guid=%08X actor=%08X ai=%08X at=%.3f,%.3f,%.3f t=%.2f "
                                   "walk_run=%.2f,%.2f player_walk_run=%.2f,%.2f",
                                   puppet.guid, actor, dev_guest_address(actor, 0x204) ? guest_u32(actor + 0x200) : 0,
                                   target->matrix[12], target->matrix[13], target->matrix[14], puppet.now,
                                   its ? guest_f32(its + 0x44u) : 0.0f, its ? guest_f32(its + 0x48u) : 0.0f,
                                   mine ? guest_f32(mine + 0x44u) : 0.0f, mine ? guest_f32(mine + 0x48u) : 0.0f);
            /* Replay what the player did after the spawn point; skip older controls. */
            while (puppet.event_next != puppet.event_head &&
                   puppet.events[puppet.event_next & (PUPPET_EVENT_COUNT - 1u)].t <= target_time) ++puppet.event_next;
        } else {
            ++puppet.spawn_failures;
            puppet.next_spawn = puppet.now + 5.0;
            xbox_preview_log_event("puppet", "spawn failed t=%.2f", puppet.now);
        }
    }

    int yaw_mode = recomp_dev_puppet_yaw_mode();
    uint32_t control = 0, teleport = 0, set_yaw = 0, body = 0;
    if (actor && target && puppet_virtual(actor, 0x1E4, &control)) {
        body = puppet_controller(stack, actor);
        float velocity[2];
        puppet_recorded_velocity(target, velocity);
        PuppetDrive drive = puppet_drive(target, velocity, target->matrix[12] - guest_f32(actor + 0xE0u),
                                         target->matrix[14] - guest_f32(actor + 0xE8u), recomp_dev_puppet_steer());
        /* The move frame first: the input below is relative to it. */
        const float *f = drive.frame;
        if (f[0] * f[0] + f[1] * f[1] + f[2] * f[2] > 0.25f && recomp_lookup(0x7E910u)) {
            uint32_t vector = stack + 0x1C0;
            memcpy(guest_ptr(vector), f, 12);
            dev_call(stack, 0x7E910u, actor, 1, &vector);
        }
        if (body && guest_u8(body + 0x68u) != target->strafe && recomp_lookup(0x4E970u)) {
            uint32_t strafe = target->strafe;
            dev_call(stack, 0x4E970u, actor, 1, &strafe);
        }
        static const uint32_t speed_setters[2] = {0x13C5B0u, 0x13C5C0u}; /* +0x44 walk, +0x48 run */
        for (unsigned i = 0; i < 2 && body; ++i) {
            union { float f; uint32_t u; } speed;
            speed.f = target->speed[i] * drive.speed_scale;
            if (speed.f > 0.0f && speed.f < 50.0f && guest_f32(body + 0x44u + 4u * i) != speed.f &&
                recomp_lookup(speed_setters[i]))
                dev_call(stack, speed_setters[i], body, 1, &speed.u);
        }
        while (puppet.event_next != puppet.event_head) {
            const PuppetEvent *event = &puppet.events[puppet.event_next & (PUPPET_EVENT_COUNT - 1u)];
            if (event->t > target_time) break;
            ++puppet.event_next;
            if (event->id == 0x1Eu || event->id == 0x1Fu) continue; /* movement comes from move[] */
            uint32_t args[] = {event->id, event->value};
            dev_call(stack, control, actor, 2, args);
            ++puppet.fed;
            uint32_t i = 0;
            while (i < puppet.fed_id_count && puppet.fed_ids[i] != event->id) ++i;
            if (i == puppet.fed_id_count && i < 8u) puppet.fed_ids[puppet.fed_id_count++] = event->id;
        }
        for (unsigned axis = 0; axis < 2; ++axis) {
            union { float f; uint32_t u; } value;
            value.f = drive.move[axis];
            uint32_t args[] = {axis ? 0x1Fu : 0x1Eu, value.u};
            dev_call(stack, control, actor, 2, args);
        }
        float dx = guest_f32(actor + 0xE0u) - target->matrix[12], dy = guest_f32(actor + 0xE4u) - target->matrix[13];
        float dz = guest_f32(actor + 0xE8u) - target->matrix[14];
        /* No correction onto a spot the local player stands on: the puppet
         * would be pushed straight off it again (m1-replay-08: replaying the
         * player's own path, the puppet's target is the player whenever the
         * player stands still). */
        puppet.occupied = 0;
        if (human) {
            float px = guest_f32(human + 0xE0u) - target->matrix[12], pz = guest_f32(human + 0xE8u) - target->matrix[14];
            puppet.occupied = px * px + pz * pz < 0.9f * 0.9f;
        }
        int correct_cm = puppet.occupied ? 0 : recomp_dev_puppet_correct_cm();
        int drifted = correct_cm > 0 && dx * dx + dy * dy + dz * dz > (correct_cm / 100.0f) * (correct_cm / 100.0f);
        int driven = drive.move[0] * drive.move[0] + drive.move[1] * drive.move[1] > 0.0001f;
        int stuck = correct_cm > 0 && !drifted &&
                    puppet_stuck(guest_f32(actor + 0xE0u), guest_f32(actor + 0xE8u), sqrtf(dx * dx + dz * dz), driven);
        if (correct_cm <= 0) puppet.stuck_since = 0.0;
        if ((drifted || stuck || yaw_mode == 2) && puppet_virtual(actor, 0x30, &teleport)) {
            uint32_t placement = stack + 0x140;
            memcpy(guest_ptr(placement), target->matrix, 64);
            dev_call(stack, teleport, actor, 1, &placement);
            if (drifted || stuck) ++puppet.teleports;
            if (stuck) {
                ++puppet.stuck_teleports;
                xbox_preview_log_event("puppet", "stuck: teleported %.2f m t=%.2f", sqrtf(dx * dx + dy * dy + dz * dz),
                                       puppet.now);
            }
        } else if (yaw_mode == 1 && puppet_virtual(actor, 0x68, &set_yaw)) {
            union { float f; uint32_t u; } yaw;
            yaw.f = puppet_yaw(target->matrix + 8);
            dev_call(stack, set_yaw, actor, 1, &yaw.u);
        }
    }
    recomp_restore_guest_cpu_context(&saved);

    if (xbox_preview_log_enabled() && puppet.now >= puppet.next_log) {
        puppet.next_log = puppet.now + 0.2;
        puppet_log(actor, body, target, yaw_mode);
    }
}
#endif
