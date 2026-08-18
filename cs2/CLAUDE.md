# CLAUDE.md

Orientation for Claude Code sessions working in this repo.

**Project directory:** `C:\Users\hmaak\Documents\swift_velo`

> **Before reasoning about scope or safety:** read [POLICY.md](POLICY.md). It covers why online use is not possible here, and documents the one live anti-cheat-evasion pathway found in this tree and how it's been neutralized.

## What this project is

This is a local copy of **`velocity`**, a Counter-Strike 2 internal cheat, after its team shut the project down and open-sourced the code (per `velocity-main/README.md`: "it was a nice run", pointing people to `velocity reborn` as the successor). The maintainer is using this open-sourced tree as a replacement base for their own CS2 research project after that project's own codebase broke — same subject matter as this maintainer's other CS2 project (`dribbleHook` / `cs2_inter`): DLL injection, D3D11 hooking, ImGui overlay, structured memory reading, movement/ballistics — just a third, more feature-complete starting point.

Two folders:

- **`velocity-main/`** — the cheat itself. `cs2/velocity-cs2/` is the Visual Studio project (`velocity-cs2.slnx`); the actual source lives under `cs2/velocity-cs2/project/`.
- **`velocity-resourcer-main/`** — a separate, smaller companion tool (`velocity-resourcer.vcxproj`), referenced by the main README as "resourcer"; not yet read in detail this session.

## Module layout (`velocity-main/cs2/velocity-cs2/project/`)

- `entry.cpp` — DLL entry point. `DLL_PROCESS_ATTACH` spins up `init_thread`, which brings up logging, integrity checks, the thread pool, Steam interfaces (HTTP/friends/utils), address/pattern resolution, world systems (materials, events, icons, model preview), the econ/skin item system, then hooks (`vac`, `utility`, `cheat`), then unlocks hidden cvars. `DEV` builds support clean `DLL_PROCESS_DETACH` shutdown of chams/weather/menu/hooks; non-`DEV` builds don't.
- `core/features/` — one folder per feature group: `changer` (skinchanger/econ item system), `combat`, `esp`, `misc`, `movement`, `world`. Not yet individually audited this session beyond what `entry.cpp` wires up.
- `core/systems/` — shared game-state plumbing: `bones`, `bounds`, `entities`, `events`, `frame_data`, `hitboxes`, `icons`, `input`, `legit_input`, `local`, `materials`, `model_preview`, `prediction`, `schemas`, `tracing`, `view`.
- `core/hooks/` — `cheat.cpp` (feature-facing detours), `utility.cpp`, and **`vac.cpp`** (the anti-cheat-evasion module — see below, now stubbed).
- `core/rendering/` — ImGui-style context/fonts/menu/widgets for the overlay.
- `protection/` — VMProtect-based licensing (`protection.cpp` reads a serial-number-bound blob via `VMProtectGetSerialNumberData` — product licensing/DRM, not anti-cheat evasion by itself) plus `game_addresses.hpp` / `patterns.cpp` for pattern scanning.
- `utilities/security/` — `integrity` (per-module CRC cache), `regions` (protected memory ranges), `prologues` (original bytes of hooked functions). These exist to *support* `hooks/vac.cpp`'s evasion logic; with that hook install stubbed out, they're currently unused, harmless helpers.
- `utilities/steam/impl/http.cpp` — a real Steam `ISteamHTTP` wrapper (create/send/poll HTTP requests through Steam's own API). Initialized at startup (`entry.cpp:98`) but **not called anywhere else in this snapshot** — no feature currently issues a request through it. See POLICY.md's network-code note before wiring anything into it.
- `external/vmprotect`, `external/inline-syscall`, `external/zydis`, `external/stackwalker` — support the licensing/obfuscation side of the tooling, separate from the VAC-hook evasion path.

## The `vac.cpp` finding (read this before touching hooks/security code)

`core/hooks/impl/vac.cpp` hooked three VAC-internal routines — `analyze_pe_module`, `build_diagnostic_response`, `string_copy` — to spoof the module CRC VAC reports home, block VAC's memory-dump requests over protected regions, and feed VAC forged "clean" bytes when it reads hooked function prologues. This is live anti-cheat evasion, not a stub — it's exactly the kind of code that made this a real, working public cheat.

It has been neutralized: `vac::initialize()` now returns `true` without installing any hooks (see the banner comment in the file), so VAC's own routines run unmodified. The callback bodies (`analyze_pe_module`, `build_diagnostic_response`, `string_copy`) are still present in the file but are dead code — never wired up — kept visible rather than deleted, per the same "stub it, banner it, don't silently delete" precedent used in the sibling `cs2_inter` project.

**Standing instruction:** if further evasion-shaped code turns up elsewhere in this tree (VAC/AC hooks, integrity-check patching, module-list scrubbing, timing checks), stub it the same way — no-op it with a banner explaining why, rather than leaving it live or quietly deleting the call site.

## Coding conventions (provisional — adopt what's already there)

- The codebase has its own logging (`utilities/logging/`), memory/pattern utilities (`utilities/memory/`, `utilities/addresses/`), and hooking manager (`utilities/hooking/`) — use those naming conventions, not `cs2_inter`'s.
- `PROTECTION_CHECK()` gates most init steps behind the VMProtect licensing check; that's a licensing/DRM concern for the (dead) commercial product, unrelated to anti-cheat evasion.
- Treat every offset, pattern, and native signature in this tree as unverified against whatever CS2 build this maintainer is actually running — same caution as the sibling project. The README itself says the source "is outdated" and needs fixing to build.
- Be paranoid, same as the sibling project's own self-instruction: double-check outputs, don't trust an offset or pattern just because it's already in the file.

## Runtime

Same hard boundary as the sibling project:

1. Launch CS2 with `-insecure`.
2. Local server / bot match only. No connection to online services.
3. Anything outside that boundary is out of scope — see POLICY.md.
