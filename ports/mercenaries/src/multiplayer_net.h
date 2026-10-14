/* Multiplayer transport (multiplayer_net.c): ENet over UDP on its own host
 * thread. The guest thread queues outgoing messages and takes incoming ones at
 * the world-update checkpoint (multiplayer_session.h); nothing here touches
 * guest memory. Players form a star: clients connect to the host, which gives
 * each a player id (the host is 0), stamps that id into every message a client
 * sends and passes the message on to the other clients. Off unless
 * mercenaries_recomp.ini [Multiplayer] mode is host or join. */
#ifndef MERCENARIES_MULTIPLAYER_NET_H
#define MERCENARIES_MULTIPLAYER_NET_H
#include <stddef.h>
#include <stdint.h>

#define MP_PROTOCOL_VERSION 1u
#define MP_MAX_PLAYERS 8u     /* player ids 0 (the host) to 7 */
#define MP_MAX_MESSAGE 1024u
#define MP_NO_PLAYER 0xFFu

/* Every message starts with its type and the sending player's id. Types below
 * 16 belong to the transport; the rest to multiplayer_session.h. */
enum {
    MP_MSG_WELCOME = 1, /* host to a new client: type, its id, protocol (u16) */
    MP_MSG_LEFT = 2,    /* host to the clients: type, the id that left */
    MP_MSG_FIRST_APP = 16
};

enum { MP_MODE_OFF, MP_MODE_HOST, MP_MODE_JOIN };

typedef struct MpConfig {
    int mode;
    char address[128];      /* join: the host's name or IP */
    char bind[64];          /* host: local address to listen on; empty for all */
    uint16_t port;
    unsigned max_players;   /* host: including itself, 2 to MP_MAX_PLAYERS */
    unsigned timeout_ms;    /* a peer that acknowledges nothing for this long is dropped */
    unsigned interp_ms;     /* how far behind the newest update puppets are drawn */
    char name[32];
} MpConfig;

typedef enum MpIncomingKind {
    MP_IN_MESSAGE,     /* data from player */
    MP_IN_JOINED,      /* host: a client joined as player */
    MP_IN_LEFT,        /* player left or timed out */
    MP_IN_CONNECTED,   /* client: the host accepted us as player */
    MP_IN_DISCONNECTED /* client: the host is gone (data[0] is ENet's reason) */
} MpIncomingKind;

typedef struct MpIncoming {
    MpIncomingKind kind;
    uint8_t player;
    uint16_t size;
    double receive_ms;  /* mp_net_now_ms when the network thread got it */
    uint8_t data[MP_MAX_MESSAGE];
} MpIncoming;

typedef struct MpNet MpNet;

/* [Multiplayer] from mercenaries_recomp.ini (MERCENARIES_CONFIG_ROOT, else
 * beside the exe), read once. */
const MpConfig *mp_config(void);
/* The game's transport: created on first call when the mode isn't off, and
 * closed politely at exit. NULL when off or when it could not start. */
MpNet *mp_net_default(void);

MpNet *mp_net_create(const MpConfig *config); /* starts the thread; NULL on failure */
void mp_net_destroy(MpNet *net);              /* disconnects politely, stops the thread */
/* Queue a message for everyone else (a client's go to the host, which passes
 * them on). Unreliable messages are sequenced: a late one is dropped. */
int mp_net_send(MpNet *net, const void *data, size_t size, int reliable);
int mp_net_poll(MpNet *net, MpIncoming *out); /* 1 and the next item, or 0 */
uint8_t mp_net_local_player(MpNet *net);      /* 0 for the host; MP_NO_PLAYER until a client is accepted */
unsigned mp_net_peer_count(MpNet *net);       /* connected peers */
double mp_net_now_ms(void);                   /* QueryPerformanceCounter in milliseconds */
#endif
