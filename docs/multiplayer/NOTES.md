# Multiplayer notes

Findings for the plan in [PLAN.md](PLAN.md). Every address is for the supported XBE (`AA08EA21…BE99D7`).
**Verified** = seen in retail code/data, a retail-execution test or a log; **Derived** = concluded from verified
facts; **Hypothesis** = not yet checked in game.

## Environment and reproducibility

- Toolchain: VS 2022 Build Tools 17.14 (MSVC 14.44.35207, cl 19.44.35229), Windows SDK 10.0.26100.0,
  Python 3.12.10, CMake 3.31.6 from `.venv`. `Build-From-Iso.ps1` takes about 4 minutes on an i7-12700K.
  Discovery converges in 3 passes: 14,083 functions, 0 failed, 1,258,131 lines of C. 58 warnings, 0 errors.
- Input: the Redump "(USA, Europe)" image (ISO SHA-256 `D01822BA…DD5D3`). Its `default.xbe` matches the
  supported hash, and xdvdfs 0.8.3 reads the full Redump image directly. 434 files (2.335 GiB) were extracted.
- **Line endings.** Git for Windows sets `core.autocrlf=true` system-wide, which checked out 1,111 files with CRLF.
  Several tests pin file hashes, so the clone needs `git config core.autocrlf false` and a fresh checkout. The
  game is unaffected: builds from a CRLF and an LF checkout of `da5e2ab`/`b838706` produced the same
  `mercenaries_recomp.exe` (`905EF6E4D1394283C0C107C9F824B018CCE15239C13A06CA12E5A381C4F696FC`), generated tree
  (`0035C271…D642`, 18 files) and launcher (`EB51D22A…7056`). Only the source-tree hashes in the provenance
  differ. So the build is deterministic on this machine, and two players can compare exe hashes to confirm they
  run the same build.
- The lifter warns `abi not found at tools/abi_analysis/output/abi_functions.json`. That directory isn't in the
  public repo, so every public build shows this warning.
- `mercenaries-build-provenance.json` is written only by `Build-From-Iso.ps1`. After `Recompile.ps1` and
  `Build.ps1` it describes the previous exe.

## M0 status (2026-10-12)

- [x] Build from the ISO; exe hash above.
- [x] First run (keyboard and mouse): `developer_menu=1` and `logging=1` work. Two F9 Troops spawns succeeded
  (`template_china_heavy`, `template_nk_elite`), the F8 marker was logged at 727.9 s, and the session ended with a
  clean shutdown. Defaults were 640×480 at a 30 FPS cap, with steady 33.3 ms frames. F11 free cam not yet tried.
- [x] Test run recorded and compared (below).
- [x] Player-state logger: implemented, unit-tested, and its key fields confirmed against retail code. The
  remaining field checks happen in the first M1 play session.

### Tests: comparison with the 458/471 baseline

Local baseline, run `artifacts/test-run-20261012-m0` (with the M0 code): **463 of 489** modules exit 0. That is
462 of the 488 existing modules plus the new `test_player_state_log_native`. The runner finds 488 existing modules
because 16 test files were added after the 471-module baseline, in `6e217ac` and `f513ae4`. Every nonzero module
is accounted for:

| Count | Cause | Modules |
| --- | --- | --- |
| 13 | The recorded baseline failures, unchanged | as listed in `docs/maintenance-validation.md` |
| 3 | Fixtures broken upstream by `f513ae4` (production now calls `recomp_mod_c4_for_template`, `recomp_mod_gameplay_script`, `mod_script_contains`; the tests weren't updated) | `mod_compatibility_native`, `wmd_inspectors`, `merc_heap_alignment_native` |
| 1 | Checks text that has never been in the public `Patch-Generated.py` | `traffic_ambient_path_capacity` |
| 3 | GCC 14 makes implicit declarations and int conversions errors; older GCC warned | `crash_guest_capture_native`, `save_completion_epilogue_native`, `scope_controls_native` |
| 1 | mingw-w64 headers define `INIT_ONCE`; the fixture expects classic MinGW | `kernel_recent_read_cache_native` |
| 4 | Inputs only the original developer had (`references/xemu`, a PS2 reference DSK, `artifacts/user-preview`) | `apu_dsp56300_integration`, `apu_hrtf_default`, `ps2_retail_mix`, `xaudio_pcm_capture` |
| 1 | New module that links a `launcher.res` from the developer's private `build/mercenaries-perf20260930` | `mod_selector_native` |

What the tests need, reconstructed from the fixtures:

- **An x64 MSVC developer shell** (vcvars64, toolset 14.44.35207). Without it the original runs got 455 instead
  of 458 (MAINTENANCE.md).
- **A 32-bit classic (mingw.org) GCC at `C:\MinGW`, also first on PATH.**
  - 202 test files hardcode `C:/MinGW/bin/gcc.exe`.
  - `test_crt_memmove_native` and `test_lua_snapshot_retail_abi` assert `sizeof(void*)==4`.
  - 52 fixtures use `static __forceinline` from `recomp_types.h`, which mingw-w64 headers reject: they define
    `__forceinline` as `extern __inline__`. `test_extended_graphics_memory` works around this explicitly.
  - mingw.org can no longer be downloaded by script (SourceForge returns 403, OSDN is gone). Instead, this machine
    has WinLibs GCC 14.3 (x86_64 mingw-w64) at `C:\MinGW`, plus a test-only header shim.
- **MSYS2 mingw64 GCC at `C:\msys64\mingw64\bin`.** About 20 tests hardcode it. Installed here: GCC 16.2.0.
- **Python packages in `.venv`:** `pytest`, `numpy`, and `unicorn==2.1.4` (the version recorded in the
  enemy-memory fixture manifest). None of these is in `requirements.txt`.

Local helper (git-ignored, recreate if needed): `.tools\run-tests.cmd artifacts\<new dir> [--match name]`. It
does three things:

1. loads vcvars64 for 14.44.35207;
2. appends `C:\MinGW\bin` to PATH;
3. sets `C_INCLUDE_PATH=.tools\test-shim`. That `_mingw.h` includes the real header, pins mingw-w64's
   `FORCEINLINE` to its original extern gnu_inline form, and redefines the bare `__forceinline` as a plain
   always-inline.

## Player-state logger (M0)

Enable it in `developer.ini` beside the exe:

```ini
[Developer]
logging=1
player_state_log=1
; optional, 1-30 samples per second (default 5)
player_state_log_hz=5
```

Each sample writes one `player-state` line to `preview-logs`:

```text
seq ticks dt world state ctrl human hvt pos back yaw vel vel_ok target tvt tpos seat vehicle vvt vmodel hp weapon wvt wtype
```

| Field | Meaning |
| --- | --- |
| `ticks` | Calls of the world-update checkpoint since the previous sample |
| `dt` | Guest frame time summed over those calls |
| `world` | The checkpoint's object (`ecx`) |
| `state` | `u32[0x413F68]` |
| `back` | Matrix row `+0xD0`; `yaw` is `atan2` of forward = −back in the x/z plane, in degrees |
| `vel` | Position change over `dt`; `vel_ok=0` on the first sample, after the human changes, and when `dt` is 0 |
| `target` | `controller+0x10`, the entity the controller's position accessor reports |
| `seat` / `vehicle` | The seat chain |
| `hp` | `+0x98 / +0x170` |
| `weapon` | From the retail getter `0x4CEE0`, plus its vtable and `+0x1A0` type |

Outside gameplay (`u32[0x413F6C] != 0x4249D707`) the line only says `gameplay=0` and no guest function is called.

- **Code:**
  - `ports/mercenaries/src/player_state_log.h` is the logger.
  - `multiplayer_guest.h` holds `recomp_multiplayer_tick`, the one guest-thread entry for multiplayer work.
  - `recomp_dev_player_state_log_hz` in `dev_menu.c` reads the option.
  - The hook is the `Multiplayer world-thread checkpoint` patch in `Patch-Generated.py`: after the developer and
    boids ticks at the top of `sub_001702D0`, it calls `recomp_multiplayer_tick(ecx, MEM32(esp+4))`. The
    `TRANSLATOR_INLINED_PATCHES` text was extended so that rerunning the patcher on a patched tree still works.
- **Off by default.** With `player_state_log` absent or 0, or `logging=0`, the tick returns before reading guest
  memory. The new code path is one call and two comparisons per world update.
- **Guest calls.** Per sample there are two: the `player0` lookup and the weapon getter. The getter runs only for
  the player human class `0x2E32B8`. Both go through `dev_call` under a saved and restored CPU context, as the
  developer tools already do every tick.
- **Unit test:** `tools/recomp/test_player_state_log_native.py`, with fixture `fixtures/player_state_log.c`. It
  compiles the real `dev_spawn.h`, the logger and the tick against stubbed guest memory, and checks:
  - the off, disabled and no-gameplay paths;
  - the rate limit;
  - position, heading and velocity;
  - the vehicle chain and the controller's entity;
  - the weapon-getter gate;
  - human changes and a missing controller;
  - invalid frame times;
  - CPU-context restoration.

  It also checks the patch text.
- **Static checks since:**
  - health `+0x98` is what `Actor_GetHitPoints` reads;
  - `target` (`controller+0x10`) is what `Utility_GetActorsVehicle` returns.
- **Still to observe in play** (during the first M1 session, no separate test):
  - `target` switching to the vehicle;
  - `yaw` sign;
  - `weapon` / `wtype` on weapon switches;
  - how many checkpoint calls happen per frame.

## M1 answers (2026-10-13)

Full reports, with evidence scripts, are in `artifacts/m1-research/{script-api,players,input}/REPORT.md` (local
only). The main session spot-checked each report's central claims against the XBE and the generated C before
recording them here.

### 1. What the script API offers

- **Size.** 360 engine functions in two registration tables:
  - 307 globals in table `0x2ED308`, registered by `0x112830`;
  - 53 `Actor_*` functions in table `0x2EDCA8`, also registered as `tRsActor` methods by `0x120DA0`.
- **Scripts.** 208 Lua 5.0 source scripts are stored in `assets.dsk`.
- **Name resolution.** An actor name in a script resolves through `0x1EC270`: keys at `0x63FD70`, GUIDs at
  `0x63DD70`, 2048 slots. `'player0'` there is the player **human**.
- **Puppet toolkit.** Each line below is the Lua function, then its native address.
  - **Spawn:** `Mission_Spawn` `0x11D560`. It calls `0x174480(this=0x4031B0, fnv(template), name, &matrix,
    NULL, 0)`, the same spawner as `dev_spawn.h`.
  - **Teleport:**
    - `Actor_SetPosition` `0x116600` uses human vt+0x64, which is `0x4C980`. It moves the physics body at
      `+0x280`.
    - `Actor_SetYaw` `0x116840` uses vt+0x68, which is `0x4C8C0`. For player0 it also turns the camera.
  - **Steer:** `Ai_MoveToPositionAndHeading` `0x11E2E0` and `Ai_Command*` with 14 verbs (`Follow`,
    `MoveToActor`, `Operate`, `Exit` …).
  - **Animate:** `Actor_PlayHumanAnimation` `0x11DD80` and `Actor_SetAnimationSet`.
  - **Seats:** `ActorVehicle_ReceiveRider` `0x117830` and `ActorVehicle_KickoutPassenger`.
  - **Protect:** `Actor_SetInvincible` `0x115EF0` sets bit 0 of `+0x124`, which the damage routine tests.
    `Ai_SetHostileRange` `0x11E4A0` writes `brain+0x518`.
  - **Faction:** `Ai_SetFaction` `0x11E440` (ids: prokat 1, nk 2, sk 3, mafia 4, china 5, allies 6, civ 7).
  - **Hibernation:** `Utility_SetSporeAllowHibernation`.
  - **Despawn:** `Actor_Remove`.
- **Gaps.**
  - No API makes an actor untargetable or switches its AI off. Candidates: `Actor_EnableCinematicControl`,
    `Actor_SetIdleInPuppetState` (the byte at `+0x809`).
  - No control over an NPC's aim, speed or gait.
  - All `Player_*` and camera functions are player0-only.

### 2. Does the engine know about more than one player?

**No.**
- **Player AI table.** `0x8C7E0` looks up player AIs in a 2-slot table:
  - count `0x322DB0`, miss value `0x322DB4`, values `0x322DB8`, keys `0x322DC0`;
  - linear probing (`0x1F2A20`/`0x1F2A60`) with no full check.

  Under Unicorn a second registration works. But once both slots are used, looking up any other name, and any
  third insert, **loops forever**.
- **One creator.** `0x184890` → `0x183C80` spawns `"player0"` once (string at `0x2E7890`), with the template hash
  from `[0x3C04A0]`. The factory `0x173A60` case `objecttype "Player"` is the only creator of the player AI class
  `RsAiPlayer`.
- **No trace of a second player.** No `player1` string or hash exists anywhere. All 212 player lookups use
  player0.
- **A second `RsAiPlayer` would take over the local game.** Its init `0x8EAC0`, deinit `0x8C180`, update `0x917C0`
  and control-scheme switch `0x8E280` write globals with no "am I local?" check:
  - the camera `0x4140E8`;
  - the HUD player pointer `0x35CA58` and HUD level;
  - the global control scheme `0x323608`;
  - several global settings blocks.

### 3. How the player controller gets its input

Input is pushed through a fixed chain:

1. The main joystick `[0x413F90]` (vtable `0x2E7158`) reads its port.
2. The single global mapper `0x323608` turns pad events into control IDs, which depend on the control state.
   - States: 4 on foot, 2 car, 1 helicopter, 3 tank, 5 scope, 12 PDA, 15 none.
   - The mapper's target is set once at gameplay start by `0xA2EF0`, from `0x184890`.
3. `RsAiPlayer::ProcessControl` `0x90470(id, value)` receives the ID.
4. Most IDs go on to `entity->vt[0x1E4](id, value)`. Aim IDs `0x28`/`0x29` go into the global camera instead.

**The control slot `vt[0x1E4]`.**
- The same slot and IDs are used by NPC AI: 94 call sites.
- NPC human: `0x53D20`. Movement IDs go to the locomotion object `vt[0x3BC]()` at `+0x30` (strafe) and `+0x34`
  (forward), clamped to ±1 by `0x13C540`/`0x13C570`.
- Player human: `0x5D5C0`, which is camera-dependent.

**Frame order.** World update (`recomp_multiplayer_tick` runs at its start), then joystick → mapper →
ProcessControl, then the port poll, then render.

**A second pad on a virtual port** works at the input-chain level (under Unicorn). It is still not usable, because
the mapper state, the PDA routing, the enable flag `0x30EBA5` and the camera are global.

## M1 approach (decided 2026-10-13)

**Option (a), revised: the puppet is an NPC-class human driven through its control slot `vt[0x1E4]`.**
- **Input.** Feed it the remote player's actor-level control IDs: movement `0x1E`/`0x1F`, fire, vehicle controls.
  The game's own locomotion, animation, weapons and vehicles then do the rest. It is the channel NPC AI already
  uses, so there can be many puppets.
- **Correction.** Teleport the puppet when it drifts past a threshold: human `vt+0x30(&matrix)` (`0x4C920`, the
  teleport used by respawn `0x183D40`), or `vt+0x64`. Take facing and aim from the snapshots.
- **Rejected.**
  - A second `RsAiPlayer`: the 2-slot table can hang, and it takes over the camera, HUD and control scheme.
  - A virtual pad port: it needs that second controller, and the mapper state is shared.
- **Fallbacks.**
  - (b) AI orders: `Ai_MoveToPositionAndHeading`, or `MoveToActor` toward a hidden proxy actor.
  - (c) Direct writes, with `Actor_EnableCinematicControl` and looping animations.

Questions for the first experiment:
1. Does an NPC human walk and run with the right animations when fed `0x1E`/`0x1F`? The local player's values are
   camera-relative (`0x5D5C0`), so the puppet may need world-space movement converted into its own frame instead
   of raw stick values.
2. Does the puppet's own AI override the controls? Options to try, in order:
   - `Ai_CommandClearAll` plus a long `Wait`;
   - a spawn with `aiType=none`;
   - SetAI(0) through vt+0x1B4, which is risky because the update may assume an AI exists.
3. How to set facing: vt+0x68 every tick, or the matrix.
4. Can the puppet keep the player's character model? It is spawned from an NPC template; the player templates are
   `template_hero_<x>`.

## Addresses and layouts

| Item | Value | Source / check |
| --- | --- | --- |
| Name hash | FNV-1a 32-bit over bytes `\|32`; `player0`=`0x660E4490` (210 immediates in the XBE), `player1`=`0x670E4623` (none) | `dev_hash`; recomputed; byte count. **Verified** |
| Script name registry | `0x1EC270(hash)` → GUID; keys `0x63FD70`, values `0x63DD70`, 2048 slots. Used by every Lua actor argument | script-api report. **Derived** from code reading |
| Player AI table (`0x8C7E0`) | 2 slots: count `0x322DB0`, miss `0x322DB4` (0), values `0x322DB8[2]`, keys `0x322DC0[2]`; insert only in `0x8EAC0`, keyed by `ai+0xC`. A miss with both slots full never returns | `test_player_actor_retail.py`; Unicorn (`players/r3/uc_verify.py`); generated C. **Verified** |
| NPC AI table | `0x66490(name)` → AI; 256 slots: count `0x321E90`, miss `0x321E94`, values `0x321E98`, keys `0x322298`; filled by `RsAi::Init 0x6BA70` | players report; generated C. **Verified** |
| `RsAiPlayer` (the "controller") | 0xD80 bytes, vtable `0x2E65E0` (set at `0x8E955` in ctor `0x8E930`), allocated by `0x170630` (only called from the factory) | generated C, Unicorn. **Verified** |
| `RsAiPlayer` slots | +0x14 init `0x8EAC0`, +0x18 deinit `0x8C180`, +0x20 update `0x917C0`, +0x2C `0x78190` (returns `+0x10` if vehicle-class), +0x34 `0x8C050` (`+0x998`), +0x3C `0x673F0` (position of `+0x10`), +0x4C is-player = 1, +0x154 `0x8E280` (switches the control scheme) | XBE vtable reads. **Verified** |
| `RsAiPlayer` fields | +0xC name hash; +0x10 controlled entity (the human on foot is **Verified**; the vehicle while driving is **Derived**); +0x18 state; +0x998 human; +0x99C flag copied to human+0x124 bit 0 | players report, `test_player_actor_retail.py` |
| Player creation | `0x183C80`: `0x174480(0x4031B0, [0x3C04A0], "player0", &matrix, 0, 0)`; factory `0x173A60` case "Player" → `0x170630` + human `0x5CB60` (vtable `0x2E32B8`, 0xEE0 bytes); `ai+0x998 = human`; SetAI via human vt+0x1B4 | generated C. **Verified** |
| Human classes | player `0x2E32B8` (type 0x1D, class id `[0x30D888]`), NPC `0x2E2CE0` (type 0x1C, `[0x30D4DC]`). `human+4` name hash; `human+0x200` its AI (vt+0xBC `0x3EDC0`) | players report. **Verified** |
| Human slots | +0x30 `0x4C920` teleport (matrix); +0x64 `0x4C980` set position (physics body `+0x280`); +0x68 `0x4C8C0` set yaw; +0x1B4 SetAI; +0x1E4 control (`0x53D20` NPC, `0x5D5C0` player); +0x3BC locomotion object (`+0x30` strafe, `+0x34` forward) | XBE vtable reads; generated C. **Verified** |
| Actor matrix | `0x1EA470` (vtable slot `+0x58`) returns −(`+0xD0..+0xD8`) as forward; position `+0xE0..+0xE8` with `+0xEC`=1.0; setter `0x1EA510` writes `+0xE0`. Up axis is y | `test_forward_axis_and_static_position_setter`. **Verified** |
| Seat chain | `human+0x768` seat → `*seat` manager → `*manager` vehicle | retail `0x590AA..0x590BC`. **Verified** |
| Vehicle model | `vehicle+0x58` model hash | `dev_spawn.h`. **Derived** |
| Health | `+0x98` current HP: `Actor_GetHitPoints 0x115B00` reads it, and the damage routine `0x3F950` changes it unless bit 0 of `+0x124` (invincible) is set. `+0x170` is max HP | generated C. **Verified** (`+0x170` **Derived**) |
| Current weapon | `0x4CEE0` (ecx = human) calls `human->vt[0x38C]()`, then `->vt[0x1B8]()`, then `weapon->vt[0x1C4](human)`, and returns the weapon or 0. `weapon+0x1A0` is a type. `human+0x6C4` shoot state | generated C. **Verified** |
| GUID lookup | `0x1EC220(guid)`: slot `guid & 0x3FFF`, GUIDs at `0x624070`, spores at `0x614070`; returns `spore+0x24` if spore flag bit 0 is set and bit 2 clear | `test_guid_lookup_lifetime_flags`. **Verified** |
| Actor/spore | `actor+0x8` spore, `spore+0x28` GUID, `spore+0x30` flags | oracle tests. **Verified** |
| Animation | `actor+0x11C` animation object; dispatch `0x15340`; `Actor_PlayHumanAnimation` uses the controller at `human+0x6B0` | oracle test; script-api report |
| Property list | ctor `0x1EB010`, insert `0x1EB080` | oracle test. **Verified** |
| Input chain | joystick `[0x413F90]` → mapper `0x323608` (target `+0x1C0`, state `+0x4C4`, `OnAnalog 0xA3AD0` needs `[0x30EBA5]==1`) → `0x90470(id, value)` → `entity->vt[0x1E4]`. On-foot IDs: LX `0x1E`, LY `0x1F`, RT `1`, LT `2`, A `6`, B `0x2F`, X `0x31`, Y `0x2B`; aim `0x28`/`0x29` | input report (Unicorn `s20`, `s26`); generated C. **Verified** |
| Host XInput bridge | `recomp_xinput_*` in `recomp_manual.c` (~7493–7842) replace the XAPI thunks `sub_002D54AC` etc. Port 0 merges keyboard/mouse; the test hook is `MERCENARIES_TEST_GAMEPAD_FILE` | input report. **Verified** (source) |
| World-update checkpoint | `sub_001702D0`, thiscall: adds arg 0 (frame seconds) to `this+4`; calls `sub_001EE610` past `0x2DD414`. Once per frame, before the task list and render | generated C; input report `s28`. Once per frame is **Derived** |
| Gameplay state | `u32[0x413F6C]==0x4249D707` (gameplay), `u32[0x413F68]` sub-state (`0xC2CBD863` in normal play) | `dev_spawn.h`; F8 log. **Verified** |
| Camera | system object `0x4140E8`; `u32[0x414104]` with position at `+0x40` (the F8 `pos`); `u32[0x41410C]` with forward at `+0x30/+0x38` | `main.c`, `dev_spawn.h`, reports |
| HUD player pointer | `0x35CA58` (written by `RsAiPlayer` init) | generated C. **Verified** |
| Streaming centre | floats `0x641D70..78` | `main.c` (`world-loading` log) |
| Faction standing to player | floats `0x323358 + 0x20*i` (faction manager `0x323338`) | `main.c`; players report |

### Bugs found in existing developer code (not changed)

- **`dev_battle_guest.h`** treats `0x8C7E0('player0')`, which is the player's AI, as the human: it checks `+4`
  against the name hash and uses `+0x768` and `+0x200`. Nothing writes AI `+4`, so `dev_battle_player` probably
  stays 0. The Untargetable/Passive modes then protect only targets whose `+4` is player0 (the player human, not
  the player's vehicle), and the player-AI exemption never applies. Fix: use `*(ai+0x998)`.
- **`dev_spawn.h:457`** looks up crew members with `0x8C7E0`, the player AI table, so the lookup always returns 0
  today. If a second player were ever registered it would hang the game. It should use `0x66490`.

Missing evidence documents: `docs/runtime/*.md` and `retail-player-actor-evidence.md` are referenced by tools and
headers but are not in the public repo.
