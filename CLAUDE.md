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

## Commands (PowerShell, from the repo root)

- First build from the ISO: `& .\ports\mercenaries\scripts\Build-From-Iso.ps1 -IsoPath '<path to the ISO>'`
  (extracts to `game_files\mercenaries-retail`, regenerates the C, builds `build\mercenaries\bin\Release\`)
- After changing `Patch-Generated.py` or anything the translation reads: `& .\ports\mercenaries\scripts\Recompile.ps1`,
  then `& .\ports\mercenaries\scripts\Build.ps1`
- After changing only port or runtime C: `& .\ports\mercenaries\scripts\Build.ps1`
- Run: `& .\ports\mercenaries\scripts\Run.ps1` (starts `mercenaries_recomp.exe` with the extracted game)
- Developer settings: `developer.ini` beside the exe. `developer_menu=1` gives F9 developer tools and F11 free cam;
  `logging=1` writes logs to `preview-logs` (F8 marks a moment in them). Restart after changing it.
- Tests: `& .\.venv\Scripts\python.exe -X utf8 -B tools/recomp/run_tests.py --match <name> --output artifacts/<new dir>`
  (`--list` lists them; use a new output folder each run). The recorded baseline is 458 of 471 modules passing
  (`docs/maintenance-validation.md`), so check a failure against it before chasing it.

## Existing hooks worth knowing

- Player: `0x8C7E0` returns the player controller (its constructor installs vtable `0x2E65E0`); `controller + 0x998`
  is the player's human actor (`dev_player_actor.h`).
- World-update checkpoint: `Patch-Generated.py` inserts `recomp_dev_spawn_tick()` (and the boids tick) at the top of
  `sub_001702D0`. Per-update multiplayer work can hook in beside it.
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
