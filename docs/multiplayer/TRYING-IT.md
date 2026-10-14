# Trying multiplayer (M2)

Two or more players, each running their own game, see each other walking around. Each of you keeps your own world:
enemies, vehicles, buildings and missions aren't shared yet. The other players appear as allied soldiers (not their
mercs), and can't drive, shoot or be shot yet (M3). Details and test results: [NOTES.md](NOTES.md), "M2".

## What you need

- The same build on every PC: everyone builds the same commit, or uses one copy of the build. The game refuses a
  player whose multiplayer protocol differs.
- A way for the joining PCs to reach the host over UDP:
  - **Easiest: a virtual LAN.** Everyone installs [Tailscale](https://tailscale.com) or ZeroTier and joins the same
    network. The host's address is then its Tailscale (or ZeroTier) IP.
  - **Or port forwarding.** The host forwards UDP port 27410 on their router to their PC, and the others join the
    host's public IP.

## Settings

Edit `mercenaries_recomp.ini` beside `mercenaries_recomp.exe` (it's created after the first run), and add a
`[Multiplayer]` section.

On the host's PC:

```ini
[Multiplayer]
mode=host
port=27410
```

On each joining PC:

```ini
[Multiplayer]
mode=join
address=100.101.102.103
port=27410
```

Put the host's address in `address`. Remove the section, or set `mode=off`, to play alone again.

The first time the host starts, Windows Firewall asks whether to let the game use the network. Allow it, or nobody
can connect.

Other settings, all optional:
- `max_players` (host, default 4, up to 8);
- `timeout_ms` (default 5000: how long before a silent player is dropped);
- `interp_ms` (default 100: how far behind the others are drawn, to smooth their movement).

## Playing

1. Start the game on every PC (any order) and start or load a game.
2. Each player appears to the others once both of you are in normal gameplay.
3. Walking and turning are shared. Running should be too, but it hasn't been tested yet: the opening C-17, where
   the automated tests play, doesn't let you run.
4. When someone quits, their soldier disappears for the others at once. If their connection drops, it disappears
   after `timeout_ms`.

## If something looks wrong

Set `logging=1` in `developer.ini` (beside the exe) and play again. The `preview-logs` folder gets `multiplayer`
lines (connected, joined, left), and `mp-puppet` lines for each other player (how far behind they're drawn, and the
error). Send those files back with a note of what you saw.
