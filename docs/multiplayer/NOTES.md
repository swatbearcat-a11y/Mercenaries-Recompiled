# Multiplayer notes

Findings for the plan in [PLAN.md](PLAN.md). Every address is for the supported XBE
(`AA08EA21…BE99D7`). **Verified** = seen in code, data or a log; **Hypothesis** = not yet checked in game.

## M0 status (2026-10-12)

### Build

- Toolchain: VS 2022 Build Tools 17.14 (MSVC 14.44.35207, cl 19.44.35229), Windows SDK 10.0.26100.0,
  Python 3.12.10, CMake 3.31.6 from `.venv`. `Build-From-Iso.ps1` ran in 3.7 minutes. The C was regenerated in
  3 analysis passes: 14,083 functions, 0 failed, 1,258,131 lines. The build had 58 warnings and 0 errors.
- Input: the Redump "(USA, Europe)" image (ISO SHA-256 `D01822BA…DD5D3`). Its `default.xbe` matches the
  supported hash, and xdvdfs 0.8.3 reads the full Redump image directly. 434 files (2.335 GiB) were extracted.
- Provenance: source `da5e2ab` (clean tree), generated tree 18 files `0035C271…D642`,
  **`mercenaries_recomp.exe` SHA-256 `905EF6E4D1394283C0C107C9F824B018CCE15239C13A06CA12E5A381C4F696FC`**,
  launcher `EB51D22A…7056`. The repo has no reference hash to compare against.
- The lifter warns `abi not found at tools/abi_analysis/output/abi_functions.json`. That directory isn't in the
  public repo, so every public build shows this warning.

### First run

Played on keyboard and mouse. `developer_menu=1` and `logging=1` both worked.

- The log header carries the exe hash `905EF6E4…`.
- Two F9 Troops spawns succeeded (`template_china_heavy`, `template_nk_elite`).
- The F8 marker (`[USER-MARKER-F8]`) appears at 727.9 s, followed by a clean shutdown.
- Defaults: 640×480, 30 FPS cap, steady 33.3 ms frames.
- F11 free cam was not tried.

### Tests

The baseline run is in `artifacts/test-run-20261012-baseline`. These points affect comparing it with the recorded
baseline:

- The runner now finds 488 modules, not 471. Sixteen test files were added in `6e217ac` and `f513ae4`, after the
  baseline was recorded.
- The 458/471 baseline needs an x64 MSVC developer shell (MAINTENANCE.md). Without one, the result is 455.
- 202 test files hardcode `C:/MinGW/bin/gcc.exe`. An x86_64 mingw-w64 GCC is needed, because the fixtures
  include `d3d11.h`. GCC 14.3 is installed there now.
- Two tests hardcode the VS 2022 **BuildTools** path.
- Some diagnostics tests import `numpy`, which isn't in `requirements.txt`. They fail at collection.

### Player-state logger: design inputs

- **Logging API.** `xbox_preview_log_event` drops a line if the same format string was logged in the last second.
  Use `xbox_preview_log_sample` instead. It is limited only by the shared cap of 64 non-priority lines per
  second and a 256-record queue (`src/kernel/preview_log.c`). Ordinary play logs about 8 lines per second.
- **World-update hook.** `Patch-Generated.py` lines 14521–14570 do three things:
  - insert the boids tick into `sub_001702D0`;
  - insert `recomp_dev_spawn_tick` before the boids tick;
  - list the combined text in `TRANSLATOR_INLINED_PATCHES["Boid simulation world tick"]`, so a second patch run
    still recognises it.

  A new tick must update that inline text too, or a second patch run fails.
  `ports/mercenaries/data/generated-patch-inventory.json` is a stale record; nothing reads it.
- **Developer settings** are read by `developer_setting()` in `dev_menu.c` (`[Developer]`, value must be `1`).

## Addresses and layouts

| Item | Value | Source / check |
| --- | --- | --- |
| Name hash | FNV-1a 32-bit over bytes `\|32`; `player0`=`0x660E4490`, `player1`=`0x670E4623` | `dev_hash` in `dev_spawn.h`; recomputed. **Verified** |
| `0x8C7E0` | One stack arg (name hash) → registered object. Also called with crew names (`dev_spawn.h:457`), so it's a by-name lookup, not a player-only getter | `dev_spawn.h`, `dev_battle_guest.h`. **Verified** (callers) |
| What `0x8C7E0('player0')` returns | **Conflict.** `dev_player_actor.h` says a controller (vtable `0x2E65E0`) with the human at `+0x998`. `dev_battle_guest.h` treats the result as the human itself (`+4` name hash, `+0x768` seat, `+0x200` AI) | Open; M1 question 2 |
| Actor health | `+0x98` current HP (float), `+0x170` max HP | Lifted `sub_0003F950` in `tools/recomp/test_actor_health_native.py`. **Hypothesis** for the player |
| Actor spore/GUID | `actor+0x8` spore, `spore+0x28` GUID | `dev_spawn.h:448`, health fixture. **Verified** |
| Human position | `+0xE0/+0xE4/+0xE8` floats | `dev_spawn.h` dev-inspect log |
| Actor rotation | written at `actor+0xB0` | `boids_guest.h:216`. Row layout is a **Hypothesis** |
| Vehicle | `human+0x768` seat → `*seat` manager → `*manager` vehicle actor; vehicle `+0x58` model hash | `dev_spawn.h` `dev_occupied_radius` |
| Gameplay state | `u32[0x413F6C]==0x4249D707 && u32[0x413F68]==0xC2CBD863` | `dev_spawn.h`; the F8 log showed `state=C2CBD863` during play |
| Cameras | `u32[0x414104]`, position at `+0x40` (this is the F8 `pos`); `u32[0x41410C]`, forward at `+0x30/+0x38` | `main.c` `preview_snapshot`; `dev_spawn.h` |
| Streaming centre | floats `0x641D70..78` | `main.c` (`world-loading` log) |
| Faction standing to player | floats `0x323358 + 0x20*i` | `main.c` (`faction-standing` log) |

## M1 research (unfinished)

Research agents were stopped early because the usage limit was reached. Their scripts are in
`artifacts/m1-research/`. These partial results are **unverified**:

- About 208 mission scripts decode from the DSKs. `'player0'` appears 869 times in 126 of them; `'player1'` never
  appears. ("AttackPlayer1..4" looked like attack-wave names.)
- `0x8C7E0` lookups resolve `tRsActor` objects through a name registry (keys near `0x63FD70`).

Missing evidence documents: `docs/runtime/*.md` and `retail-player-actor-evidence.md` are referenced by tools and
headers but are not in the public repo.

## Corrections to make in CLAUDE.md / PLAN.md

- Describe `0x8C7E0` as a by-name lookup and record the `+0x998` conflict.
- Tests: run from the x64 VS developer shell, with GCC at `C:\MinGW\bin`, `pytest`, and probably `numpy`.
- There are now 488 modules: 471 baseline plus 16 newer test files, with one extra module still unexplained.
