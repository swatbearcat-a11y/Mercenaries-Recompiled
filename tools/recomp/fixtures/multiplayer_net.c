/* The multiplayer transport (multiplayer_net.c, built here with ENet) over
 * loopback: one host and clients in this process, each on its own network
 * thread. Checks player ids, id stamping and relaying, reliable and unreliable
 * delivery, a full server, polite leaving and the timeout of a frozen client. */
#include "../../../ports/mercenaries/src/multiplayer_net.c"
#include <assert.h>

typedef struct Seen { MpIncoming items[64]; unsigned count; } Seen;

/* Poll until an item of this kind (and player, unless MP_NO_PLAYER) arrives;
 * everything taken on the way is kept in seen. */
static const MpIncoming *wait_for(MpNet *net, Seen *seen, MpIncomingKind kind, uint8_t player, unsigned timeout_ms)
{
    double until = mp_net_now_ms() + timeout_ms;
    for (unsigned i = 0; i < seen->count; ++i)
        if (seen->items[i].kind == kind && (player == MP_NO_PLAYER || seen->items[i].player == player))
            return &seen->items[i];
    while (mp_net_now_ms() < until) {
        MpIncoming in;
        while (mp_net_poll(net, &in)) {
            assert(seen->count < 64);
            seen->items[seen->count++] = in;
            if (in.kind == kind && (player == MP_NO_PLAYER || in.player == player)) return &seen->items[seen->count - 1];
        }
        Sleep(2);
    }
    return NULL;
}

static MpConfig config(int mode, uint16_t port, unsigned max_players)
{
    MpConfig c;
    memset(&c, 0, sizeof(c));
    c.mode = mode;
    c.port = port;
    c.max_players = max_players;
    c.timeout_ms = 1500;
    c.interp_ms = 100;
    strcpy(c.address, "127.0.0.1");
    strcpy(c.bind, "127.0.0.1");
    return c;
}

int main(void)
{
    uint16_t port = (uint16_t)(30000 + GetCurrentProcessId() % 20000);
    MpConfig host_config = config(MP_MODE_HOST, port, 4), join = config(MP_MODE_JOIN, port, 4);
    MpNet *host = mp_net_create(&host_config);
    assert(host && mp_net_local_player(host) == 0);
    MpNet *a = mp_net_create(&join);
    assert(a && mp_net_local_player(a) == MP_NO_PLAYER);
    Seen seen_host = {0}, seen_a = {0}, seen_b = {0};
    const MpIncoming *got = wait_for(a, &seen_a, MP_IN_CONNECTED, MP_NO_PLAYER, 5000);
    assert(got && got->player == 1 && mp_net_local_player(a) == 1);
    assert(wait_for(host, &seen_host, MP_IN_JOINED, 1, 2000));
    MpNet *b = mp_net_create(&join);
    got = wait_for(b, &seen_b, MP_IN_CONNECTED, MP_NO_PLAYER, 5000);
    assert(got && got->player == 2 && mp_net_local_player(b) == 2 && mp_net_peer_count(host) == 2);
    assert(wait_for(host, &seen_host, MP_IN_JOINED, 2, 2000));

    /* A client's message reaches the host and the other client, stamped with
     * its id whatever it claimed; never itself. */
    uint8_t state[60] = {MP_MSG_FIRST_APP, 7, 0xAB}, events[20] = {MP_MSG_FIRST_APP + 1, 7, 0xCD};
    assert(mp_net_send(a, state, sizeof(state), 0) && mp_net_send(a, events, sizeof(events), 1));
    got = wait_for(host, &seen_host, MP_IN_MESSAGE, 1, 2000);
    assert(got && got->size == 60 && got->data[0] == MP_MSG_FIRST_APP && got->data[1] == 1 && got->data[2] == 0xAB);
    got = wait_for(b, &seen_b, MP_IN_MESSAGE, 1, 2000);
    assert(got && got->data[1] == 1);
    /* Ordering across channels isn't guaranteed: find the reliable one. */
    int reliable_seen = 0;
    for (unsigned tries = 0; tries < 2 && !reliable_seen; ++tries) {
        for (unsigned i = 0; i < seen_b.count; ++i)
            if (seen_b.items[i].kind == MP_IN_MESSAGE && seen_b.items[i].data[0] == MP_MSG_FIRST_APP + 1 &&
                seen_b.items[i].data[1] == 1 && seen_b.items[i].data[2] == 0xCD && seen_b.items[i].size == 20)
                reliable_seen = 1;
        if (!reliable_seen) {
            MpIncoming in;
            Sleep(200);
            while (mp_net_poll(b, &in)) seen_b.items[seen_b.count++] = in;
        }
    }
    assert(reliable_seen);
    Sleep(200);
    for (unsigned i = 0; i < seen_a.count; ++i) assert(seen_a.items[i].kind != MP_IN_MESSAGE); /* not echoed */
    { MpIncoming in; while (mp_net_poll(a, &in)) assert(in.kind != MP_IN_MESSAGE); }

    /* The host's messages reach both clients as player 0. */
    state[1] = 5;
    assert(mp_net_send(host, state, sizeof(state), 1));
    got = wait_for(a, &seen_a, MP_IN_MESSAGE, 0, 2000);
    assert(got && got->data[1] == 0);
    assert(wait_for(b, &seen_b, MP_IN_MESSAGE, 0, 2000));
    /* Transport-only types from a client go nowhere. */
    uint8_t forged[2] = {MP_MSG_LEFT, 2};
    assert(mp_net_send(a, forged, sizeof(forged), 1));
    Sleep(300);
    { MpIncoming in; while (mp_net_poll(b, &in)) assert(in.kind != MP_IN_LEFT); }

    /* A full server turns a third player away (max 2 on this one). */
    MpConfig small = config(MP_MODE_HOST, (uint16_t)(port + 1), 2), join_small = config(MP_MODE_JOIN, (uint16_t)(port + 1), 2);
    MpNet *host2 = mp_net_create(&small), *c1 = mp_net_create(&join_small), *c2;
    Seen s2 = {0}, s_c1 = {0}, s_c2 = {0};
    assert(host2 && c1 && wait_for(c1, &s_c1, MP_IN_CONNECTED, 1, 5000));
    c2 = mp_net_create(&join_small);
    got = wait_for(c2, &s_c2, MP_IN_DISCONNECTED, MP_NO_PLAYER, 5000);
    assert(got && got->size == 1 && got->data[0] == 2 && mp_net_local_player(c2) == MP_NO_PLAYER);
    (void)s2;
    mp_net_destroy(c2);
    mp_net_destroy(c1);
    mp_net_destroy(host2);

    /* Leaving politely: the host and the other client hear at once. */
    mp_net_destroy(b);
    assert(wait_for(host, &seen_host, MP_IN_LEFT, 2, 1000));
    assert(wait_for(a, &seen_a, MP_IN_LEFT, 2, 1000) && mp_net_peer_count(host) == 1);

    /* A frozen client is dropped after the timeout (1.5 s here). */
    double frozen = mp_net_now_ms();
    SuspendThread(a->thread);
    assert(wait_for(host, &seen_host, MP_IN_LEFT, 1, 6000));
    assert(mp_net_now_ms() - frozen >= 1000.0 && mp_net_peer_count(host) == 0);
    ResumeThread(a->thread);
    /* The client notices the host dropped it and reports the loss. */
    assert(wait_for(a, &seen_a, MP_IN_DISCONNECTED, MP_NO_PLAYER, 8000) && mp_net_local_player(a) == MP_NO_PLAYER);
    mp_net_destroy(a);
    mp_net_destroy(host);

    /* Off and malformed configurations start nothing. */
    MpConfig off = config(MP_MODE_OFF, port, 4);
    assert(!mp_net_create(&off) && !mp_net_create(NULL));
    puts("PASS: player ids, id stamping and relaying, reliable and unreliable delivery, no echo, forged transport "
         "messages ignored, full server, polite leave, frozen-client timeout, off config");
    return 0;
}
