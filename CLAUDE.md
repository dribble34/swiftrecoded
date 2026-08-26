# CLAUDE.md

Orientation for Claude Code sessions working in this repo.

**Project directory:** `C:\Users\hmajc\Documents\swiftrecoded-master`

> **Before reasoning about scope or safety:** read [POLICY.md](POLICY.md). It covers why online use is not possible here, and what's been checked/removed regarding anti-cheat-evasion code in this tree.

## What this project is

This is `swift` (repo `swiftrecoded`), a Counter-Strike 2 internal cheat: a Windows DLL (`entry.cpp` exports `entry`, called on `DLL_PROCESS_ATTACH`/`DLL_PROCESS_DETACH`) that gets loaded or manually mapped into `cs2.exe`. It's a rename/rebase of the open-sourced **`velocity`** cheat — internal strings and comments still say "velocity" in places (e.g. `diag.hpp`, crash-handler messages in [entry.cpp](swift/project/entry.cpp), `config.hpp`) even though the type/namespace names have been renamed to `swift`/this project's own conventions. Don't be surprised by that mismatch; it's cosmetic, not a sign of two different codebases.

There's no injector/loader in this tree — just the DLL itself. Getting it into the process (manual map vs `LoadLibrary`, from what host process) is out of scope for what's checked into this repo.

**Ownership:** shared project between the maintainer and a small group of dev friends who also commit to this tree. The maintainer doesn't fully trust the other contributors and has asked for extra scrutiny of anything that looks like it could be a backdoor, credential/data exfiltration, or unreviewed obfuscation/anti-debug/virtualization added by a collaborator — see [POLICY.md](POLICY.md)'s "Trust model" section before treating any contributor's code as safe by default.

## Module layout (`swift/project/`)

- `entry.cpp` — DLL entry point. Registers exception handling/SEH tables for manually-mapped modules, spins up `init_thread`, which brings up config, logging (console + popup), integrity checks, the thread pool, Steam interfaces (HTTP/friends/user/utils), address/pattern resolution, world systems (materials, events, icons, model preview), the econ/skin item system, then hooks (`hooks::utility`, `hooks::cheat`), then unlocks hidden cvars, then discovers skyboxes.
- `core/features/` — the cheat's feature set, one subfolder per category, each with a `*.hpp` and an `impl/` folder:
  - `combat/` — `aimbot/`, `autowall/`, `engineprediction/`, `legit.cpp`, `rage/`, `misc.cpp`. Rage and legit aimbot are separate implementations sharing `aimbot/shared.cpp`.
  - `esp/` — `player/` (overlay, glow, chams), `item/` (overlay, glow, chams), `projectile/`, `other/`.
  - `movement/` — bunnyhop, airstrafe, edgejump/edgebug/edgestop, fastladder, jumpbug, pixelsurf, test_strafer.
  - `misc/` — HUD, velocity graph, projectile trajectory, camera, dlight, impacts, removals, scoreboard weapons, other.
  - `changer/` — cosmetic model/skin/agent/knife/glove/gun changers plus `econ_item_system.cpp`.
  - `world/` — scene (skybox discovery) and weather.
- `core/hooks/impl/` — `cheat.cpp` (the large table of game-function hooks: input, rendering pipeline, networking callbacks, scene/material draw calls — see the `hooking::manager::create` table) and `utility.cpp` (present/resize_buffers style host hooks).
- `core/rendering/` — ImGui-based menu (`impl/menu/`) and widget/context plumbing for the D3D11 overlay.
- `core/systems/impl/` — supporting subsystems: entities, bones, bounds, tracing, prediction, frame_data, input, local player, schemas, icons, model_preview.
- `core/settings.hpp` — cheat config/settings definitions (currently has local uncommitted changes alongside the combat files).
- `protection/` — `game_addresses.hpp`, `patterns.cpp`/`.hpp`: signature scanning for game internals used by the hooks above.
- `utilities/` — cross-cutting: `addresses/` (module/global/function resolution), `hooking/` (the detour manager used throughout `core/hooks`), `logging/`, `memory/`, `security/` (`impl/integrity.cpp` — see note below), `steam/`, `threadpool/`, `math/`, `proto/`, `random/`, `tls/`.
- `external/` — vendored dependencies: `zydis`, `lz4`, `nlohmann` (json), `phnt`, `stackwalker`, `xdraw`/`xui` (the ImGui-adjacent rendering toolkit), `inline-syscall`.

## Anti-cheat-evasion surface (short version)

`utilities/security/impl/integrity.cpp`'s `initialize()` is a no-op today; its own comment explains why: the module-hash cache it used to populate is no longer wired up because it called the game's PE analyzer on every loaded module (crashes on current CS2 builds — stale pattern) and the cache existed only to spoof hashes for a VAC hook that isn't present in this tree. There is no `vac.cpp`/VAC-hooking module in this repo (unlike the upstream `velocity` source, which had one) — see [POLICY.md](POLICY.md) for the full audit finding and what that means for scope.

## Working in this repo

- Recent work has been on ragebot/legit performance (see current uncommitted changes in `core/features/combat/impl/{autowall,legit,rage}` and `core/settings.hpp` — hitbox caching, trace reduction, threaded scanning per the last commit message).
- Feature code is organized by `*.hpp` (interface/state) + `impl/*.cpp` (logic) pairs; follow that split when adding new features rather than putting logic in headers.
- Hooks are registered centrally in `hooks::cheat::initialize()` / `hooks::utility::initialize()` via `hooking::manager::create({...})` tables keyed by `patterns::*` signatures resolved in `protection/patterns.cpp`. If a feature needs a new game hook, that's where it gets wired in, not ad hoc inside feature code.
- `DEV` builds get extra crash-capture (minidump hook, forced-termination capture) and clean shutdown paths in `entry.cpp`; non-`DEV` builds skip most of that on detach.
