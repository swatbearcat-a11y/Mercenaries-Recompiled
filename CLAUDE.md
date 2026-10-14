# Mercenaries Recompiled: multiplayer fork

This fork adds online multiplayer to Mercenaries Recompiled, a static recompilation of the Xbox game
(USA retail XBE) into C, built on the xboxrecomp toolkit. The work is on the `multiplayer` branch.
The plan and milestones are in `docs/multiplayer/PLAN.md`. Record findings in `docs/multiplayer/NOTES.md`
as you go: addresses, structure offsets, Lua functions, what each was tested with, and what failed.

Read before changing code: `ARCHITECTURE.md`, `CODE_MAP.md`, `SUBSYSTEMS.md`, `DATA_FLOW.md`, `CONTRIBUTING.md`
(where changes belong), `WORKAROUNDS.md` (before removing anything odd) and `MAINTENANCE.md`.

## Rules

- Never edit `ports/mercenaries/src/recomp/gen` or `ports/mercenaries/analysis`: they're regenerated. Lasting
  changes to generated functions go in `ports/mercenaries/scripts/Patch-Generated.py`; Mercenaries code in
  `ports/mercenaries/src`; the shared runtime in `src`.
- Never commit the ISO, the extracted game (`game_files/`), generated C, saves, logs (`preview-logs`), memory dumps
  or test output (`artifacts/`). This fork is public.
- The guest (the game's translated code) runs on one thread. Networking runs on its own host thread and hands data
  over through a queue; anything that reads or writes guest memory or calls guest functions runs on the guest thread
  at a checkpoint (see the world-update hook below).
- A call into guest code saves and restores the guest CPU context (`mod_run_lua` in `mod_gameplay_guest.h`,
  `dev_call` in `dev_spawn.h`). Bounds-check guest addresses (`dev_guest_address`).
- Addresses and layouts are only valid for the supported XBE (SHA-256
  `AA08EA21D952AC35F49C02C7E2ED08AA25AD7535FBBBCCC95636775F37BE99D7`). Note where each one came from in NOTES.md.
- Multiplayer is off by default. With it off, the game must behave exactly as before.
- Keep install and checkout paths short: the runtime uses `MAX_PATH` buffers.
- Check out with LF line endings (`git config core.autocrlf false`, then a fresh checkout): Git for Windows defaults
  to CRLF, and several tests pin file hashes. The exe is the same either way.

## Commands (PowerShell, from the repo root)

- First build from the ISO: `& .\ports\mercenaries\scripts\Build-From-Iso.ps1 -IsoPath '<path to the ISO>'`
  (extracts to `game_files\mercenaries-retail`, regenerates the C, builds `build\mercenaries\bin\Release\`)
- After changing `Patch-Generated.py` or anything the translation reads: `& .\ports\mercenaries\scripts\Recompile.ps1`,
  then `& .\ports\mercenaries\scripts\Build.ps1`
- After changing only port or runtime C: `& .\ports\mercenaries\scripts\Build.ps1`
- Run: `& .\ports\mercenaries\scripts\Run.ps1` (starts `mercenaries_recomp.exe` with the extracted game)
- Developer settings: `developer.ini` beside the exe. `developer_menu=1` gives F9 developer tools and F11 free cam;
  `logging=1` writes logs to `preview-logs` (F8 marks a moment in them); `player_state_log=1` (with `logging=1`)
  logs the player's state a few times a second (`player_state_log_hz`, default 5). Restart after changing it.
- Tests: `.tools\run-tests.cmd artifacts\<new dir> [--match <name>]` on this machine (local, git-ignored: MSVC
  14.44 developer shell, GCC at `C:\MinGW\bin`, a header shim). By hand: `run_tests.py --output artifacts/<new dir>`
  from an x64 VS developer shell with GCC on PATH, `C:\msys64\mingw64\bin\gcc.exe`, and `pytest`, `numpy`,
  `unicorn==2.1.4` in `.venv` (`--list` lists them; use a new output folder each run). Local baseline: 463 of 489
  modules pass; the 26 failures are explained in `docs/multiplayer/NOTES.md`. That includes the 13 from
  `docs/maintenance-validation.md`, so check a failure against both before chasing it.

## Existing hooks worth knowing

- Player: `0x8C7E0(name hash)` looks up the player's AI (`RsAiPlayer`, vtable `0x2E65E0`) in a 2-slot table;
  for `player0` (`0x660E4490`) it returns the one player. Never register a second player there: once both slots
  are used, lookups of any other name never return. `ai + 0x998` is the human actor (`dev_player_actor.h`);
  `ai + 0x10` is the controlled entity. NPC AIs: `0x66490(name)`. Script names resolve through `0x1EC270`
  (`'player0'` is the human). Details and evidence: NOTES.md. `dev_battle_guest.h` and the crew lookup at
  `dev_spawn.h:457` use the wrong objects (NOTES.md).
- Driving actors: every human or vehicle takes control IDs through `vt[0x1E4](id, value)`. The player's AI sends
  them (`0x90470`), and so does NPC AI. Teleport a human with `vt[0x30](&matrix)` (as respawn does). Movement
  input (`0x1E`/`0x1F`) is relative to the character controller's move frame (`human+0x280`, `+0x1C`), and the
  speed is the gait's walk or run speed: replay all of it, as `puppet_replay.h` does (NOTES.md, "M1 experiment").
- Automated in-game runs with nobody playing: `tools/multiplayer/run_puppet_replay.py` starts a hidden New Game
  (`--show` to watch it), drives the test gamepad file, and leaves logs, screenshots and `summary.json` in
  `artifacts/test-runs/<run>`. `run_two_copies.py` does the same for a connected host and client.
- Multiplayer (M2): `multiplayer_net.c` is the ENet transport on its own thread (no guest access);
  `multiplayer_session.h` is the guest side at the world-update checkpoint; `puppet_drive.h` drives each puppet.
  Settings: `[Multiplayer]` in `mercenaries_recomp.ini`, off by default. Player guide: `docs/multiplayer/TRYING-IT.md`.
- World-update checkpoint: `Patch-Generated.py` inserts `recomp_dev_spawn_tick()`, the boids tick and
  `recomp_multiplayer_tick(ecx, frame seconds)` (`multiplayer_guest.h`) at the top of `sub_001702D0`. Multiplayer
  per-update work goes in `recomp_multiplayer_tick`; keep the `TRANSLATOR_INLINED_PATCHES` text in step.
- Spawning characters and squads: `dev_spawn.h`, `dev_battle.h`, `dev_factions.h`.
- The game's Lua VM: `mod_run_lua(vm, text)` (`mod_gameplay_guest.h`) runs Lua in the game's own script VM.
  `mod_gameplay.lua` uses the game's script API: `Utility_GetActor('player0')`, `Actor_GetPosition`, `Actor_IsAlive`,
  `Ai_GetFaction`, `Utility_GetActorsInRange`, `Utility_LoadUtilityScript`, `follow_Set`.
- Camera: `recomp_freecam_camera(camera, matrix, dt)` in `free_cam.c`; loading the world around a point other than
  the player: `freecam_streaming.h`.
- Input: the guest polls controller ports through `XInputGetState` in `src/input/xinput_device.c`; bindings and
  remapping are in `recomp_controls.c`.
- The official plugin API (`plugin_host.cpp`) runs at input checkpoints, not per frame, and can't call guest
  functions. Multiplayer is built into the port, not as a plugin.
