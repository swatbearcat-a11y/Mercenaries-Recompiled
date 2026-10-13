# Multiplayer plan

The goal is online multiplayer as an option that's off by default. The first target is a free-roam sandbox: two
or more players see each other, travel and drive together, and can fight. Each player keeps their own world
(enemies, buildings, factions) until the shared world in M4.

## Approach

- Each player runs their own game. Other players appear as **puppets**: characters in your world, moved by
  network updates. This is how SA:MP and Just Cause 2 Multiplayer work.
- Not lockstep (sending only inputs and simulating the same game on each PC): the game isn't deterministic. Its timing
  comes from host clocks, interrupts are delivered at checkpoints, the audio clock drives the movie clock, and
  streaming finishes when it finishes.
- Each player sends their own state about 20 times a second; puppets are drawn about 100 ms behind, interpolated
  between updates, with a snap when they're too far off.
- The network runs on a host thread; updates are applied on the guest thread at the world-update checkpoint.
- Network library: ENet to start (small, C, UDP with reliable and unreliable channels). Players connect by IP; for
  early tests over the internet, a virtual LAN (Tailscale, ZeroTier) avoids port forwarding. Relays and lobbies later.
  Add its licence notice as `docs/dependencies.md` describes.

## M0: build and baseline

- [x] Build from the ISO; note the executable hash from `mercenaries-build-provenance.json`.
- [x] The game runs; `developer_menu=1` and `logging=1` work; spawning from F9 works.
- [x] A test run recorded and compared with the 458/471 baseline (see NOTES.md for the local baseline).
- [ ] A debug option that logs the player's state (position, heading, velocity, current vehicle, health, weapon)
      a few times a second. Every later step is checked with it. Implemented as `player_state_log` and
      unit-tested; still to be checked in game.

## M1: a puppet on one PC (no network yet)

Answer these first, in `NOTES.md`:

1. **What does the game's script API offer?** Find the mission scripts in the game data and list the `Actor_*`,
   `Ai_*`, `Utility_*`, `Vehicle_*` (and similar) functions: setting a position, move-to orders, animations,
   getting in and out of vehicles, damage, invulnerability, faction, turning AI off.
2. **Does the engine know about more than one player?** Scripts call the player `'player0'`. Check whether
   `'player1'` means anything, and whether the player-controller class (found by the name lookup `0x8C7E0`,
   vtable `0x2E65E0`) can be created for a second human.
3. **How does the player controller get its input?** Trace from `XInputGetState` (`src/input/xinput_device.c`) to
   the controller object. Could a second controller read a virtual port fed by the network?

Then pick how to drive the puppet, in this order of preference:

- (a) a second player controller fed the remote player's input, corrected by position updates (animations,
  firing and vehicles then come from the game itself);
- (b) script or AI move-to orders, with a teleport when it's more than a few metres off;
- (c) writing its position and animation state directly.

Answered 2026-10-13 (NOTES.md, "M1 answers"). The engine supports exactly one player: a second player controller
would take over the camera, HUD and control scheme, and its 2-slot lookup table can hang. So (a) becomes **(a′)**:
the puppet is an ordinary NPC human, and the remote player's actor-level control IDs go into its control slot
`vt[0x1E4]`, the same channel NPC AI uses. Position updates correct it through the respawn teleport `vt[0x30]`.
(b) and (c) stay as fallbacks.

Build:

- [ ] Record your own play (the M0 state log plus the control IDs the player sends through `vt[0x1E4]`), spawn a
      puppet human, and replay the recording on it a few seconds behind you.
- [ ] The puppet walks, runs, turns and aims with the right animations; AI doesn't attack it and it doesn't attack
      AI; it isn't despawned; it survives streaming as you move around.

## M2: two copies connected

- [ ] Two installations side by side on one PC (the launcher refuses a second copy from the same folder). Check
      that saves and caches don't collide.
- [ ] The network library, host/join settings (command line or a `[Multiplayer]` section in an ini), off by default.
- [ ] Exchange player state; one puppet per remote player; interpolation; dropping a player who times out.
- [ ] Test on one PC, then over the internet with a second person.

## M3: vehicles and weapons

- [ ] Puppets get in, out and drive; a puppet's vehicle follows network updates; passenger and gunner seats.
- [ ] Firing, muzzle flash and tracers on puppets; grenades.
- [ ] Damage between players: the shooter's PC decides hits on a puppet and sends a damage event; the victim's PC
      applies it to its own player. Friendly fire as an option.

## M4: shared world (later; write a design here before any code)

Buildings destroyed for everyone, the same enemies near both players, faction standing, money and support. The
hard part: each PC only keeps the area round its own player loaded.

## Testing

- Prefer checks that run without a person: the state log, recordings replayed on puppets, two copies on one PC.
- A person judges the feel: lag, jitter, animation.
- Before committing: build, run the tests for what changed, and note in the commit what was tested and what wasn't.
