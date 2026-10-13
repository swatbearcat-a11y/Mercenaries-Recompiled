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

## M0 status (2026-10-12)

- [x] Build from the ISO; exe hash above.
- [x] First run (keyboard and mouse): `developer_menu=1` and `logging=1` work. Two F9 Troops spawns succeeded
  (`template_china_heavy`, `template_nk_elite`), the F8 marker was logged at 727.9 s, and the session ended with a
  clean shutdown. Defaults were 640×480 at a 30 FPS cap, with steady 33.3 ms frames. F11 free cam not yet tried.
- [x] Test run recorded and compared (below).
- [ ] Player-state logger: implemented and unit-tested; in-game check pending (below).

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
- **In-game checks still to do:**
  - health drops when hit;
  - `target` switches to the vehicle when driving;
  - `yaw` follows turning;
  - `weapon` / `wtype` change with weapon switches;
  - how many checkpoint calls happen per frame.

## Addresses and layouts

| Item | Value | Source / check |
| --- | --- | --- |
| Name hash | FNV-1a 32-bit over bytes `\|32`; `player0`=`0x660E4490`, `player1`=`0x670E4623` | `dev_hash` in `dev_spawn.h`; recomputed. **Verified** |
| `0x8C7E0(hash)` | cdecl lookup by name hash. Returns the `player0` controller from a registry entry (hash at `0x322DC0`, object at `0x322DB8`, `0x322DB4`=0 in the test); unknown hash → 0. Also called with spawned crew names in `dev_spawn.h` | `tools/recomp/test_player_actor_retail.py::test_lookup_returns_controller_and_separate_human` (retail code under Unicorn). **Verified** |
| Controller class | vtable `0x2E65E0`, installed at `0x8E955`. Slot `+0x34` = `0x8C050` returns `*(this+0x998)`, the human. Slot `+0x3C` = `0x673F0` (GetPosition) returns the position of `*(this+0x10)`, not of the human | same test file. **Verified** |
| `controller+0x10` | The entity the controller reports. Expected to be the human on foot and the vehicle when driving | **Hypothesis**; the logger's `target` field checks it |
| Player human class | vtable `0x2E32B8` | Player filter of `recomp_human_weapon_update_checkpoint` (`recomp_manual.c`). **Derived** |
| Other actor tables | `0x2E2CE0` (human), `0x2E0468` (static actor) | retail oracle test. **Verified** as tables; class names unknown |
| Actor matrix | `0x1EA470` (vtable slot `+0x58`) returns −(`+0xD0..+0xD8`) as the forward axis; position `+0xE0..+0xE8` with `+0xEC`=1.0; static setter `0x1EA510` (slot `+0x64` of `0x2E0468`) writes `+0xE0` | `test_forward_axis_and_static_position_setter`. **Verified**. Up axis is y: spawn rays drop along y |
| Seat chain | `human+0x768` seat → `*seat` manager → `*manager` vehicle | Retail `0x590AA..0x590BC`, `test_seat_manager_vehicle_chain`. **Verified** |
| Vehicle model | `vehicle+0x58` model hash | `dev_spawn.h` `dev_occupied_radius`. **Derived** |
| Actor health | `+0x98` current HP (float), `+0x170` max HP | Lifted damage routine `sub_0003F950` in `test_actor_health_native.py`. **Hypothesis** for the player |
| Current weapon | `0x4CEE0` (ecx = human) calls `human->vt[0x38C]()`, then `->vt[0x1B8]()`, then `weapon->vt[0x1C4](human)`; returns the weapon or 0. `weapon+0x1A0` is a type (8/9/10 handled specially by the fire path `sub_0004DDB0`). `human+0x6C4` shoot state, `+0x6C8` shoot animation | Generated `sub_0004CEE0` and `sub_0004DDB0` (`recomp_0001.c`), `recomp_manual.c` trace. **Verified** (code reading) |
| GUID lookup | `0x1EC220(guid)`: slot `guid & 0x3FFF`, GUIDs at `0x624070`, spores at `0x614070`; returns `spore+0x24` (the actor) if spore flag bit 0 is set and bit 2 clear | `test_guid_lookup_lifetime_flags`. **Verified** |
| Actor/spore | `actor+0x8` spore, `spore+0x28` GUID, `spore+0x30` flags (bits 9..11 cleared by `0x1EAA90`) | oracle tests, `dev_spawn.h`. **Verified** |
| Animation | `actor+0x11C` animation object; dispatch `0x15340` | `test_static_animation_dispatch_and_allocation`. **Verified** |
| Property list | ctor `0x1EB010`, insert `0x1EB080` (used by developer spawns) | `test_property_list_constructor_and_insertion`. **Verified** |
| World-update checkpoint | `sub_001702D0`, thiscall: adds arg 0 (frame seconds, float) to `this+4`; above the constant at `0x2DD414` it calls `sub_001EE610` | Generated `recomp_0007.c`. **Verified** (code reading). Calls per frame: unknown |
| Gameplay state | `u32[0x413F6C]==0x4249D707` (gameplay), `u32[0x413F68]` sub-state (`0xC2CBD863` in normal play) | `dev_spawn.h`; F8 log. **Verified** |
| Cameras | `u32[0x414104]` with position at `+0x40` (the F8 `pos`); `u32[0x41410C]` with forward at `+0x30/+0x38` | `main.c` `preview_snapshot`; `dev_spawn.h` |
| Streaming centre | floats `0x641D70..78` | `main.c` (`world-loading` log) |
| Faction standing to player | floats `0x323358 + 0x20*i` | `main.c` (`faction-standing` log) |

### Discrepancy in existing code

`dev_battle_guest.h` treats `0x8C7E0('player0')` as the human: it checks `+4` against the name hash and uses
`+0x768` as the seat and `+0x200` as the AI. The retail oracle shows it is the controller, whose human is at
`+0x998`. The Untargetable/Passive developer modes may not work as intended. Not changed; check before relying on
it.

## M1 research (partial, unverified)

The first research agents were stopped early; their scripts are in `artifacts/m1-research/`. Partial results:

- About 208 mission scripts decode from the DSKs. `'player0'` appears 869 times in 126 of them; `'player1'` never
  appears. ("AttackPlayer1..4" looked like attack-wave names.)
- `0x8C7E0` lookups resolve `tRsActor` objects through a name registry (keys near `0x63FD70`).

Missing evidence documents: `docs/runtime/*.md` and `retail-player-actor-evidence.md` are referenced by tools and
headers but are not in the public repo.
