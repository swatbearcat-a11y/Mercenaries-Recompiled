/* Included by recomp_manual.c through multiplayer_guest.h, after dev_spawn.h
 * (dev_call, dev_property, dev_human_available, dev_load_layer).
 * A puppet: an NPC human spawned with aiType=none (so no AI competes for its
 * controls) and driven every world update toward a stream of player frames:
 * the M1 replay experiment's own recording (puppet_replay.h) or a remote
 * player's snapshots (multiplayer_session.h). Guest thread only, at the
 * world-update checkpoint, inside a saved CPU context. Evidence for every
 * address: docs/multiplayer/NOTES.md. */
#ifndef MERCENARIES_PUPPET_DRIVE_H
#define MERCENARIES_PUPPET_DRIVE_H

#define PUPPET_TEMPLATE "template_allies_soldier"
#define PUPPET_STEER_GAIN 1.5f      /* m/s of correction per metre of offset */
#define PUPPET_STEER_TURN 1.5707964f /* at most 90 degrees */
#define PUPPET_OCCUPIED_M 1.2f      /* two humans stay about 1.05 m apart (m1-replay-10) */

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
 * A frame holds these for the player; the puppet gets them back: the move frame
 * through the human setter 0x7E910 (what the game's AI steering 0x7F520 uses),
 * the speeds through the controller setters, the input through 0x1E/0x1F scaled
 * into the frame's gait, all steered toward the frame's position (puppet_drive).
 * m1-replay-03 replayed only the input: the puppet ran (5 m/s) along its spawn
 * frame while the player walked (2.3 m/s, running is off in the opening C-17). */
typedef struct PuppetFrame {
    double t;
    float matrix[16];
    float move[2];   /* +0x30, +0x34 */
    float frame[3];  /* +0x1C */
    float speed[2];  /* +0x44 walk, +0x48 run */
    uint8_t gait, run_allowed, strafe;
} PuppetFrame;

/* One driven puppet. Start from zeros with a name; the GUID is found again
 * every update, so a puppet the world removed is noticed and respawned. */
typedef struct PuppetActor {
    char name[32];
    uint32_t guid;                    /* spore GUID; 0 while there is none */
    double next_spawn;
    int catching_up;                  /* walking to a target that stands still */
    int occupied;                     /* the local player stands on the target spot */
    float turn, speed_scale;          /* last steering, for the log */
    double stuck_since;               /* start of the current stuck check; 0 when not checking */
    float stuck_at[2];                /* puppet x, z when it started */
    uint32_t spawns, spawn_failures, teleports, stuck_teleports, fed;
    uint32_t fed_ids[8], fed_id_count;
} PuppetActor;

/* The next due discrete control for the puppet, or 0 when there is none. */
typedef int (*PuppetEventSource)(void *context, PuppetEvent *event);

/* The game's yaw (radians) for a matrix back row (+0xD0). Actor_GetYaw's helper
 * 0x1F8860 computes -atan2(-x, -z) of the forward vector, which is -back. The
 * first automated run (m1-replay-01) confirmed it: the back-vector reading
 * turned the puppet exactly 180 degrees from the player. */
static float puppet_yaw(const float *back)
{
    return -atan2f(back[0], back[2]);
}

/* Scale for the frame's input so that the puppet, an NPC that may run, picks
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

/* What the puppet gets this update: the frame's move frame and input (scaled
 * into its gait) and a factor for its speeds. The input alone doesn't say where
 * the player went: sliding along a wall, being pushed and animations that move
 * the player without input (standing up from a seat) are not in it, and a
 * puppet a little off the path collides differently. So with steering on, the
 * puppet heads for the player's velocity plus PUPPET_STEER_GAIN times its
 * horizontal offset (ex, ez): the frame turns (at most 90 degrees) and the speed
 * scales (0.25 to 1.5) while the frame has input; without input, the puppet
 * walks when that velocity is over 0.3 m/s or it is more than 0.3 m away, until
 * it is within 0.1 m and the player stands still. */
typedef struct PuppetDrive { float frame[3], move[2], speed_scale; } PuppetDrive;

static PuppetDrive puppet_drive(PuppetActor *p, const PuppetFrame *target, const float *velocity, float ex, float ez,
                                int steer)
{
    PuppetDrive drive;
    float scale = puppet_gait_scale(target);
    memcpy(drive.frame, target->frame, sizeof(drive.frame));
    drive.move[0] = target->move[0] * scale;
    drive.move[1] = target->move[1] * scale;
    drive.speed_scale = 1.0f;
    p->turn = 0.0f;
    float input = sqrtf(drive.move[0] * drive.move[0] + drive.move[1] * drive.move[1]);
    float vx = velocity[0] + PUPPET_STEER_GAIN * ex, vz = velocity[1] + PUPPET_STEER_GAIN * ez;
    float wanted = sqrtf(vx * vx + vz * vz), moving = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    float distance = sqrtf(ex * ex + ez * ez);
    float fx = drive.frame[0], fz = drive.frame[2], length = sqrtf(fx * fx + fz * fz);
    if (!steer || length < 0.5f) {
        p->catching_up = 0;
    } else if (input > 0.01f) {
        p->catching_up = 0;
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
            p->turn = turn;
        }
    } else if (wanted > 0.01f && (moving > 0.3f || distance > 0.3f || (p->catching_up && distance > 0.1f))) {
        p->catching_up = 1;
        drive.frame[0] = vx / wanted;
        drive.frame[1] = 0.0f;
        drive.frame[2] = vz / wanted;
        drive.move[0] = 0.0f;
        drive.move[1] = 0.5f; /* walk */
        if (target->speed[0] > 0.0f) drive.speed_scale = puppet_clamp(wanted / target->speed[0], 0.3f, 1.0f);
    } else {
        p->catching_up = 0;
    }
    p->speed_scale = drive.speed_scale;
    return drive;
}

/* A puppet that is driven (fed input) more than 0.5 m from its target but
 * moves less than 0.25 m in a second is stuck (m1-replay-07: on a bench it was
 * spawned on, against cargo the player slid past); the caller teleports it. */
static int puppet_stuck(PuppetActor *p, double now, float x, float z, float error, int driven)
{
    if (!driven || error <= 0.5f) {
        p->stuck_since = 0.0;
        return 0;
    }
    if (p->stuck_since <= 0.0) {
        p->stuck_since = now;
        p->stuck_at[0] = x;
        p->stuck_at[1] = z;
        return 0;
    }
    if (now - p->stuck_since < 1.0) return 0;
    float dx = x - p->stuck_at[0], dz = z - p->stuck_at[1];
    p->stuck_since = 0.0;
    return dx * dx + dz * dz < 0.25f * 0.25f;
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

/* A frame for a human now: its matrix and its controller's movement state.
 * The caller sets t. */
static void puppet_read(uint32_t stack, uint32_t human, PuppetFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    memcpy(frame->matrix, guest_ptr(human + 0xB0u), sizeof(frame->matrix));
    uint32_t body = puppet_controller(stack, human);
    if (!body) return;
    frame->move[0] = guest_f32(body + 0x30u);
    frame->move[1] = guest_f32(body + 0x34u);
    for (unsigned i = 0; i < 3; ++i) frame->frame[i] = guest_f32(body + 0x1Cu + 4u * i);
    frame->speed[0] = guest_f32(body + 0x44u);
    frame->speed[1] = guest_f32(body + 0x48u);
    frame->gait = (uint8_t)guest_u32(body + 0xCu);
    frame->run_allowed = guest_u8(body + 0x65u);
    frame->strafe = guest_u8(body + 0x68u);
}

/* Spawn a puppet like the F9 troop spawns (0x174480 with a property list),
 * plus aiType=none, at the matrix. Returns the spore GUID or 0. */
static uint32_t puppet_spawn(uint32_t stack, const char *puppet_name, const float *matrix)
{
    if (!dev_human_available(stack, PUPPET_TEMPLATE)) dev_load_layer(stack, "template_humans");
    if (!dev_human_available(stack, PUPPET_TEMPLATE)) return 0;
    uint32_t name = stack + 0x100, placement = stack + 0x140, prop = stack + 0x400, list = stack + 0x600;
    memcpy(guest_ptr(placement), matrix, 64);
    dev_call(stack, 0x1EB010, list, 0, NULL);
    dev_property(stack, prop, list, "name", puppet_name);
    dev_property(stack, prop, list, "aiType", "none");
    dev_property(stack, prop, list, "dieuponhibernation", "true");
    dev_property(stack, prop, list, "path", "none");
    dev_property(stack, prop, list, "encounter", "none");
    dev_property(stack, prop, list, "squad", "none");
    strcpy(guest_ptr(name), puppet_name);
    uint32_t args[] = {dev_hash(PUPPET_TEMPLATE), name, placement, list, 0};
    uint32_t actor = dev_call(stack, 0x174480, 0x4031B0, 5, args);
    if (!dev_guest_address(actor, 0x770)) return 0;
    dev_transient_actor(actor);
    *(uint8_t *)guest_ptr(actor + 0x124) |= 1u; /* invincible, as Actor_SetInvincible */
    uint32_t spore = guest_u32(actor + 8);
    return dev_guest_address(spore, 0x2C) ? guest_u32(spore + 0x28) : 0;
}

/* The puppet's actor this update, or 0: found by GUID (0x1EC220; a lost one is
 * logged and respawned 2 s later), or spawned at the target when due (a failed
 * spawn is retried 5 s later). *spawned is set when it was spawned now. */
static uint32_t puppet_actor(uint32_t stack, PuppetActor *p, double now, const PuppetFrame *target, uint32_t human,
                             int *spawned)
{
    uint32_t actor = 0;
    *spawned = 0;
    if (p->guid) {
        uint32_t guid = p->guid;
        actor = dev_call(stack, 0x1EC220u, 0, 1, &guid);
        if (!dev_guest_address(actor, 0x770)) {
            xbox_preview_log_event("puppet", "lost name=%s guid=%08X t=%.2f", p->name, p->guid, now);
            p->guid = 0;
            actor = 0;
            p->next_spawn = now + 2.0;
        }
    }
    if (p->guid || !target || now < p->next_spawn) return actor;
    p->guid = puppet_spawn(stack, p->name, target->matrix);
    if (!p->guid) {
        ++p->spawn_failures;
        p->next_spawn = now + 5.0;
        xbox_preview_log_event("puppet", "spawn failed name=%s t=%.2f", p->name, now);
        return 0;
    }
    uint32_t guid = p->guid;
    actor = dev_call(stack, 0x1EC220u, 0, 1, &guid);
    ++p->spawns;
    *spawned = 1;
    uint32_t mine = human ? puppet_controller(stack, human) : 0;
    uint32_t its = dev_guest_address(actor, 0x770) ? puppet_controller(stack, actor) : 0;
    xbox_preview_log_event("puppet", "spawned name=%s guid=%08X actor=%08X ai=%08X at=%.3f,%.3f,%.3f t=%.2f "
                           "walk_run=%.2f,%.2f player_walk_run=%.2f,%.2f",
                           p->name, p->guid, actor, dev_guest_address(actor, 0x204) ? guest_u32(actor + 0x200) : 0,
                           target->matrix[12], target->matrix[13], target->matrix[14], now,
                           its ? guest_f32(its + 0x44u) : 0.0f, its ? guest_f32(its + 0x48u) : 0.0f,
                           mine ? guest_f32(mine + 0x44u) : 0.0f, mine ? guest_f32(mine + 0x48u) : 0.0f);
    return actor;
}

/* Remove the puppet from the world, as Actor_Remove (0x116590) does for a live
 * actor: its vt[0x10] (NPC human 0x56670 leaves any seat and removes the spore
 * through 0x1EE0E0). */
static void puppet_remove(uint32_t stack, PuppetActor *p)
{
    if (!p->guid) return;
    uint32_t guid = p->guid, remove = 0;
    uint32_t actor = dev_call(stack, 0x1EC220u, 0, 1, &guid);
    if (dev_guest_address(actor, 0x770) && puppet_virtual(actor, 0x10, &remove)) {
        dev_call(stack, remove, actor, 0, NULL);
        xbox_preview_log_event("puppet", "removed name=%s guid=%08X", p->name, p->guid);
    }
    p->guid = 0;
}

/* Drive the actor toward the target frame for this update. velocity is the
 * player's horizontal velocity at the target (x, z). Feeds the move frame,
 * strafe mode and speeds, then the due discrete controls from events, then the
 * movement input; then the yaw through vt+0x68, or a vt+0x30 teleport to the
 * target when it drifted past correct_cm, is stuck, or yaw_mode is 2. A spot
 * the local player stands on can't be reached: no steering or correction
 * toward it (on one PC the target is the player whenever the player stands
 * still, m1-replay-08 and -10). Returns the actor's controller, for the log. */
static uint32_t puppet_apply(uint32_t stack, PuppetActor *p, double now, uint32_t actor, uint32_t human,
                             const PuppetFrame *target, const float *velocity, PuppetEventSource events,
                             void *context, int steer, int correct_cm, int yaw_mode)
{
    uint32_t control = 0, teleport = 0, set_yaw = 0;
    if (!actor || !target || !puppet_virtual(actor, 0x1E4, &control)) return 0;
    uint32_t body = puppet_controller(stack, actor);
    p->occupied = 0;
    if (human) {
        float px = guest_f32(human + 0xE0u) - target->matrix[12], pz = guest_f32(human + 0xE8u) - target->matrix[14];
        p->occupied = px * px + pz * pz < PUPPET_OCCUPIED_M * PUPPET_OCCUPIED_M;
    }
    float ex = p->occupied ? 0.0f : target->matrix[12] - guest_f32(actor + 0xE0u);
    float ez = p->occupied ? 0.0f : target->matrix[14] - guest_f32(actor + 0xE8u);
    PuppetDrive drive = puppet_drive(p, target, velocity, ex, ez, steer);
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
    PuppetEvent event;
    while (events && events(context, &event)) {
        if (event.id == 0x1Eu || event.id == 0x1Fu) continue; /* movement comes from the frame */
        uint32_t args[] = {event.id, event.value};
        dev_call(stack, control, actor, 2, args);
        ++p->fed;
        uint32_t i = 0;
        while (i < p->fed_id_count && p->fed_ids[i] != event.id) ++i;
        if (i == p->fed_id_count && i < 8u) p->fed_ids[p->fed_id_count++] = event.id;
    }
    for (unsigned axis = 0; axis < 2; ++axis) {
        union { float f; uint32_t u; } value;
        value.f = drive.move[axis];
        uint32_t args[] = {axis ? 0x1Fu : 0x1Eu, value.u};
        dev_call(stack, control, actor, 2, args);
    }
    float dx = guest_f32(actor + 0xE0u) - target->matrix[12], dy = guest_f32(actor + 0xE4u) - target->matrix[13];
    float dz = guest_f32(actor + 0xE8u) - target->matrix[14];
    if (p->occupied) correct_cm = 0;
    int drifted = correct_cm > 0 && dx * dx + dy * dy + dz * dz > (correct_cm / 100.0f) * (correct_cm / 100.0f);
    int driven = drive.move[0] * drive.move[0] + drive.move[1] * drive.move[1] > 0.0001f;
    int stuck = correct_cm > 0 && !drifted &&
                puppet_stuck(p, now, guest_f32(actor + 0xE0u), guest_f32(actor + 0xE8u), sqrtf(dx * dx + dz * dz),
                             driven);
    if (correct_cm <= 0) p->stuck_since = 0.0;
    if ((drifted || stuck || yaw_mode == 2) && puppet_virtual(actor, 0x30, &teleport)) {
        uint32_t placement = stack + 0x140;
        memcpy(guest_ptr(placement), target->matrix, 64);
        dev_call(stack, teleport, actor, 1, &placement);
        if (drifted || stuck) ++p->teleports;
        if (stuck) {
            ++p->stuck_teleports;
            xbox_preview_log_event("puppet", "stuck: name=%s teleported %.2f m t=%.2f", p->name,
                                   sqrtf(dx * dx + dy * dy + dz * dz), now);
        }
    } else if (yaw_mode == 1 && puppet_virtual(actor, 0x68, &set_yaw)) {
        union { float f; uint32_t u; } yaw;
        yaw.f = puppet_yaw(target->matrix + 8);
        dev_call(stack, set_yaw, actor, 1, &yaw.u);
    }
    return body;
}

/* The puppet's fields for a log sample (after puppet_apply; reads guest memory
 * only). Clears the fed-control counters. */
static void puppet_log_fields(char *out, size_t size, PuppetActor *p, uint32_t actor, uint32_t controller,
                              const PuppetFrame *target, int yaw_mode)
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
    for (uint32_t i = 0, used = 0; i < p->fed_id_count && used + 4 < sizeof(ids); ++i)
        used += (uint32_t)snprintf(ids + used, sizeof(ids) - used, i ? ",%02X" : "%02X", p->fed_ids[i]);
    snprintf(out, size,
        "actor=%08X ai=%08X pos=%.3f,%.3f,%.3f back=%.3f,%.3f,%.3f "
        "rec=%.3f,%.3f,%.3f move=%.3f,%.3f frame=%.3f,%.3f,%.3f rec_frame=%.3f,%.3f,%.3f gait=%u rec_gait=%u "
        "speeds=%.2f,%.2f turn=%.1f speed_scale=%.2f catch_up=%d occupied=%d "
        "err=%.3f yaw_err=%.1f yaw_mode=%d fed=%u ids=%s teleports=%u stuck_teleports=%u spawns=%u spawn_failures=%u",
        actor, ai, position[0], position[1], position[2], back[0], back[1], back[2],
        target ? target->matrix[12] : 0.0f, target ? target->matrix[13] : 0.0f,
        target ? target->matrix[14] : 0.0f, target ? target->move[0] : 0.0f, target ? target->move[1] : 0.0f,
        frame[0], frame[1], frame[2], target ? target->frame[0] : 0.0f, target ? target->frame[1] : 0.0f,
        target ? target->frame[2] : 0.0f, gait, target ? target->gait : 0u, speed[0], speed[1],
        p->turn * 57.29578f, p->speed_scale, p->catching_up, p->occupied,
        error, yaw_error, yaw_mode, p->fed, ids[0] ? ids : "-",
        p->teleports, p->stuck_teleports, p->spawns, p->spawn_failures);
    p->fed = 0;
    p->fed_id_count = 0;
}
#endif
