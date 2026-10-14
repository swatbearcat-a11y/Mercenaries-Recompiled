/* Multiplayer transport; see multiplayer_net.h. ENet runs only on this file's
 * thread; the guest thread reaches it through two locked rings. ENet's headers
 * pull in winsock2.h, which must come before windows.h. */
#include <enet/enet.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "multiplayer_net.h"

#define MP_QUEUE 512u          /* items per ring */
#define MP_RECONNECT_MS 3000.0 /* client: retry after losing or failing to reach the host */
#define MP_CHANNEL_UNRELIABLE 0
#define MP_CHANNEL_RELIABLE 1

typedef struct MpOutgoing {
    uint8_t reliable;
    uint16_t size;
    uint8_t data[MP_MAX_MESSAGE];
} MpOutgoing;

struct MpNet {
    MpConfig config;
    HANDLE thread;
    volatile LONG running;
    volatile LONG local_player;
    volatile LONG peer_count;
    SRWLOCK lock; /* guards both rings */
    MpOutgoing *out;
    uint32_t out_head, out_tail;
    MpIncoming *in;
    uint32_t in_head, in_tail;
    uint32_t dropped_out, dropped_in;
    /* Network thread only. */
    ENetHost *host;
    ENetPeer *server;                /* client: the host */
    ENetPeer *peers[MP_MAX_PLAYERS]; /* host: connected clients by player id */
    double next_connect_ms;
};

static volatile LONG mp_enet_users;

double mp_net_now_ms(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

/* Network thread: queue an item for the guest thread; drops it when full. */
static void mp_push_incoming(MpNet *net, MpIncomingKind kind, uint8_t player, const void *data, size_t size)
{
    if (size > MP_MAX_MESSAGE) return;
    AcquireSRWLockExclusive(&net->lock);
    if (net->in_head - net->in_tail >= MP_QUEUE) {
        ++net->dropped_in;
    } else {
        MpIncoming *item = &net->in[net->in_head++ % MP_QUEUE];
        item->kind = kind;
        item->player = player;
        item->size = (uint16_t)size;
        item->receive_ms = mp_net_now_ms();
        if (size) memcpy(item->data, data, size);
    }
    ReleaseSRWLockExclusive(&net->lock);
}

static void mp_send_packet(ENetPeer *peer, const void *data, size_t size, int reliable)
{
    ENetPacket *packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (packet && enet_peer_send(peer, reliable ? MP_CHANNEL_RELIABLE : MP_CHANNEL_UNRELIABLE, packet) < 0)
        enet_packet_destroy(packet);
}

/* Host: pass a message on to every connected client except one. */
static void mp_relay(MpNet *net, uint8_t except, const void *data, size_t size, int reliable)
{
    for (unsigned id = 1; id < MP_MAX_PLAYERS; ++id)
        if (id != except && net->peers[id]) mp_send_packet(net->peers[id], data, size, reliable);
}

static void mp_flush_outgoing(MpNet *net)
{
    for (;;) {
        MpOutgoing item;
        int have = 0;
        AcquireSRWLockExclusive(&net->lock);
        if (net->out_tail != net->out_head) {
            item = net->out[net->out_tail++ % MP_QUEUE];
            have = 1;
        }
        ReleaseSRWLockExclusive(&net->lock);
        if (!have) return;
        if (net->config.mode == MP_MODE_HOST) {
            item.data[1] = 0; /* the host is player 0 */
            mp_relay(net, MP_NO_PLAYER, item.data, item.size, item.reliable);
        } else if (net->server && net->server->state == ENET_PEER_STATE_CONNECTED) {
            mp_send_packet(net->server, item.data, item.size, item.reliable);
        }
    }
}

static void mp_host_event(MpNet *net, const ENetEvent *event)
{
    ENetPeer *peer = event->peer;
    uint8_t id = (uint8_t)(uintptr_t)peer->data;
    switch (event->type) {
    case ENET_EVENT_TYPE_CONNECT: {
        unsigned free_id = 0;
        for (unsigned i = 1; i < net->config.max_players && i < MP_MAX_PLAYERS && !free_id; ++i)
            if (!net->peers[i]) free_id = i;
        if (event->data != MP_PROTOCOL_VERSION || !free_id) {
            enet_peer_disconnect(peer, event->data != MP_PROTOCOL_VERSION ? 1u : 2u); /* 1 version, 2 full */
            peer->data = (void *)(uintptr_t)MP_NO_PLAYER;
            break;
        }
        peer->data = (void *)(uintptr_t)free_id;
        net->peers[free_id] = peer;
        enet_peer_timeout(peer, 0, net->config.timeout_ms, net->config.timeout_ms);
        uint8_t welcome[4] = {MP_MSG_WELCOME, (uint8_t)free_id, MP_PROTOCOL_VERSION & 0xFFu,
                              (MP_PROTOCOL_VERSION >> 8) & 0xFFu};
        mp_send_packet(peer, welcome, sizeof(welcome), 1);
        InterlockedIncrement(&net->peer_count);
        mp_push_incoming(net, MP_IN_JOINED, (uint8_t)free_id, NULL, 0);
        break;
    }
    case ENET_EVENT_TYPE_RECEIVE: {
        ENetPacket *packet = event->packet;
        if (id < MP_MAX_PLAYERS && net->peers[id] == peer && packet->dataLength >= 2 &&
            packet->dataLength <= MP_MAX_MESSAGE && packet->data[0] >= MP_MSG_FIRST_APP) {
            int reliable = (packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0;
            packet->data[1] = id; /* clients can't speak for anyone else */
            mp_push_incoming(net, MP_IN_MESSAGE, id, packet->data, packet->dataLength);
            mp_relay(net, id, packet->data, packet->dataLength, reliable);
        }
        enet_packet_destroy(packet);
        break;
    }
    case ENET_EVENT_TYPE_DISCONNECT:
        if (id < MP_MAX_PLAYERS && net->peers[id] == peer) {
            uint8_t left[2] = {MP_MSG_LEFT, id};
            net->peers[id] = NULL;
            InterlockedDecrement(&net->peer_count);
            mp_relay(net, MP_NO_PLAYER, left, sizeof(left), 1);
            mp_push_incoming(net, MP_IN_LEFT, id, NULL, 0);
        }
        peer->data = (void *)(uintptr_t)MP_NO_PLAYER;
        break;
    default:
        break;
    }
}

static void mp_client_event(MpNet *net, const ENetEvent *event)
{
    switch (event->type) {
    case ENET_EVENT_TYPE_CONNECT:
        enet_peer_timeout(event->peer, 0, net->config.timeout_ms, net->config.timeout_ms);
        InterlockedExchange(&net->peer_count, 1);
        break; /* accepted once the host's welcome arrives */
    case ENET_EVENT_TYPE_RECEIVE: {
        ENetPacket *packet = event->packet;
        if (packet->dataLength >= 2 && packet->dataLength <= MP_MAX_MESSAGE) {
            uint8_t type = packet->data[0], player = packet->data[1];
            if (type == MP_MSG_WELCOME && player < MP_MAX_PLAYERS) {
                InterlockedExchange(&net->local_player, player);
                mp_push_incoming(net, MP_IN_CONNECTED, player, NULL, 0);
            } else if (type == MP_MSG_LEFT && player < MP_MAX_PLAYERS) {
                mp_push_incoming(net, MP_IN_LEFT, player, NULL, 0);
            } else if (type >= MP_MSG_FIRST_APP && player < MP_MAX_PLAYERS) {
                mp_push_incoming(net, MP_IN_MESSAGE, player, packet->data, packet->dataLength);
            }
        }
        enet_packet_destroy(packet);
        break;
    }
    case ENET_EVENT_TYPE_DISCONNECT: {
        uint8_t reason = (uint8_t)event->data;
        int was_accepted = InterlockedExchange(&net->local_player, MP_NO_PLAYER) != MP_NO_PLAYER;
        net->server = NULL;
        InterlockedExchange(&net->peer_count, 0);
        net->next_connect_ms = mp_net_now_ms() + MP_RECONNECT_MS;
        if (was_accepted || reason) mp_push_incoming(net, MP_IN_DISCONNECTED, MP_NO_PLAYER, &reason, 1);
        break;
    }
    default:
        break;
    }
}

static void mp_client_connect(MpNet *net)
{
    ENetAddress address;
    net->next_connect_ms = mp_net_now_ms() + MP_RECONNECT_MS;
    if (enet_address_set_host(&address, net->config.address) != 0) return;
    address.port = net->config.port;
    net->server = enet_host_connect(net->host, &address, 2, MP_PROTOCOL_VERSION);
}

static DWORD WINAPI mp_net_thread(void *parameter)
{
    MpNet *net = (MpNet *)parameter;
    while (InterlockedCompareExchange(&net->running, 1, 1)) {
        if (net->config.mode == MP_MODE_JOIN && !net->server && mp_net_now_ms() >= net->next_connect_ms)
            mp_client_connect(net);
        mp_flush_outgoing(net);
        ENetEvent event;
        int result = enet_host_service(net->host, &event, 5);
        while (result > 0) {
            if (net->config.mode == MP_MODE_HOST) mp_host_event(net, &event);
            else mp_client_event(net, &event);
            result = enet_host_service(net->host, &event, 0);
        }
        if (result < 0) Sleep(5);
    }
    /* Say goodbye so the others drop us now rather than after a timeout. */
    mp_flush_outgoing(net);
    for (unsigned id = 1; id < MP_MAX_PLAYERS; ++id)
        if (net->peers[id]) enet_peer_disconnect(net->peers[id], 0);
    if (net->server) enet_peer_disconnect(net->server, 0);
    double until = mp_net_now_ms() + 200.0;
    ENetEvent event;
    while (mp_net_now_ms() < until && enet_host_service(net->host, &event, 10) >= 0)
        if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
    return 0;
}

MpNet *mp_net_create(const MpConfig *config)
{
    if (!config || (config->mode != MP_MODE_HOST && config->mode != MP_MODE_JOIN)) return NULL;
    if (InterlockedIncrement(&mp_enet_users) == 1 && enet_initialize() != 0) {
        InterlockedDecrement(&mp_enet_users);
        return NULL;
    }
    MpNet *net = (MpNet *)calloc(1, sizeof(MpNet));
    if (net) {
        net->out = (MpOutgoing *)calloc(MP_QUEUE, sizeof(MpOutgoing));
        net->in = (MpIncoming *)calloc(MP_QUEUE, sizeof(MpIncoming));
    }
    if (!net || !net->out || !net->in) goto failed;
    net->config = *config;
    if (net->config.max_players < 2 || net->config.max_players > MP_MAX_PLAYERS) net->config.max_players = 4;
    if (net->config.timeout_ms < 500) net->config.timeout_ms = 5000;
    InitializeSRWLock(&net->lock);
    if (config->mode == MP_MODE_HOST) {
        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = config->port;
        if (config->bind[0] && enet_address_set_host_ip(&address, config->bind) != 0) goto failed;
        net->host = enet_host_create(&address, MP_MAX_PLAYERS, 2, 0, 0);
        net->local_player = 0;
    } else {
        /* Bind the client to loopback when the host is on this PC, so a test
         * on one machine opens nothing to the network. */
        ENetAddress local;
        local.host = ENET_HOST_ANY;
        local.port = 0;
        int loopback = !strcmp(config->address, "127.0.0.1") || !_stricmp(config->address, "localhost");
        if (loopback) enet_address_set_host_ip(&local, "127.0.0.1");
        net->host = enet_host_create(loopback ? &local : NULL, 1, 2, 0, 0);
        net->local_player = MP_NO_PLAYER;
    }
    if (!net->host) goto failed;
    net->running = 1;
    net->thread = CreateThread(NULL, 0, mp_net_thread, net, 0, NULL);
    if (!net->thread) goto failed;
    return net;
failed:
    if (net) {
        if (net->host) enet_host_destroy(net->host);
        free(net->out);
        free(net->in);
        free(net);
    }
    if (InterlockedDecrement(&mp_enet_users) == 0) enet_deinitialize();
    return NULL;
}

void mp_net_destroy(MpNet *net)
{
    if (!net) return;
    InterlockedExchange(&net->running, 0);
    WaitForSingleObject(net->thread, 2000);
    CloseHandle(net->thread);
    enet_host_destroy(net->host);
    free(net->out);
    free(net->in);
    free(net);
    if (InterlockedDecrement(&mp_enet_users) == 0) enet_deinitialize();
}

int mp_net_send(MpNet *net, const void *data, size_t size, int reliable)
{
    int queued = 0;
    if (!net || size < 2 || size > MP_MAX_MESSAGE) return 0;
    AcquireSRWLockExclusive(&net->lock);
    if (net->out_head - net->out_tail >= MP_QUEUE) {
        ++net->dropped_out;
    } else {
        MpOutgoing *item = &net->out[net->out_head++ % MP_QUEUE];
        item->reliable = reliable != 0;
        item->size = (uint16_t)size;
        memcpy(item->data, data, size);
        queued = 1;
    }
    ReleaseSRWLockExclusive(&net->lock);
    return queued;
}

int mp_net_poll(MpNet *net, MpIncoming *out)
{
    int have = 0;
    if (!net) return 0;
    AcquireSRWLockExclusive(&net->lock);
    if (net->in_tail != net->in_head) {
        *out = net->in[net->in_tail++ % MP_QUEUE];
        have = 1;
    }
    ReleaseSRWLockExclusive(&net->lock);
    return have;
}

uint8_t mp_net_local_player(MpNet *net)
{
    return net ? (uint8_t)InterlockedCompareExchange(&net->local_player, 0, 0) : MP_NO_PLAYER;
}

unsigned mp_net_peer_count(MpNet *net)
{
    return net ? (unsigned)InterlockedCompareExchange(&net->peer_count, 0, 0) : 0u;
}

/* The same file recomp_options.c uses. */
static void mp_config_path(char path[MAX_PATH])
{
    char directory[MAX_PATH];
    DWORD configured = GetEnvironmentVariableA("MERCENARIES_CONFIG_ROOT", directory, MAX_PATH);
    if (configured && configured < MAX_PATH - sizeof("\\mercenaries_recomp.ini")) {
        snprintf(path, MAX_PATH, "%s\\mercenaries_recomp.ini", directory);
        return;
    }
    DWORD length = GetModuleFileNameA(NULL, directory, MAX_PATH);
    char *separator = length && length < MAX_PATH ? strrchr(directory, '\\') : NULL;
    if (separator) separator[1] = '\0';
    else directory[0] = '\0';
    snprintf(path, MAX_PATH, "%smercenaries_recomp.ini", directory);
}

const MpConfig *mp_config(void)
{
    static MpConfig config;
    static int loaded;
    if (loaded) return &config;
    char path[MAX_PATH], mode[16];
    mp_config_path(path);
    GetPrivateProfileStringA("Multiplayer", "mode", "off", mode, sizeof(mode), path);
    config.mode = !_stricmp(mode, "host") ? MP_MODE_HOST : !_stricmp(mode, "join") ? MP_MODE_JOIN : MP_MODE_OFF;
    GetPrivateProfileStringA("Multiplayer", "address", "127.0.0.1", config.address, sizeof(config.address), path);
    GetPrivateProfileStringA("Multiplayer", "bind", "", config.bind, sizeof(config.bind), path);
    GetPrivateProfileStringA("Multiplayer", "name", "Player", config.name, sizeof(config.name), path);
    UINT port = GetPrivateProfileIntA("Multiplayer", "port", 27410, path);
    config.port = (uint16_t)(port >= 1024 && port <= 65535 ? port : 27410);
    config.max_players = GetPrivateProfileIntA("Multiplayer", "max_players", 4, path);
    config.timeout_ms = GetPrivateProfileIntA("Multiplayer", "timeout_ms", 5000, path);
    UINT interp = GetPrivateProfileIntA("Multiplayer", "interp_ms", 100, path);
    config.interp_ms = interp >= 20 && interp <= 1000 ? interp : 100;
    loaded = 1;
    return &config;
}

static MpNet *mp_default_net;

static void mp_net_close_default(void)
{
    mp_net_destroy(mp_default_net);
    mp_default_net = NULL;
}

MpNet *mp_net_default(void)
{
    static int tried;
    if (!tried) {
        tried = 1;
        if (mp_config()->mode != MP_MODE_OFF) {
            mp_default_net = mp_net_create(mp_config());
            if (mp_default_net) atexit(mp_net_close_default);
        }
    }
    return mp_default_net;
}
