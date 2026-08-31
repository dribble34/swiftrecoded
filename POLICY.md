# POLICY.md — Project Scope & Operational Boundaries

## What this project is

This repo (`swiftrecoded`, DLL target `swift`) is a Counter-Strike 2 internal cheat, derived from/rebased on the open-sourced **`velocity`** cheat — traces of the original name remain in comments and diagnostic strings (see [CLAUDE.md](CLAUDE.md)) even though the code has otherwise been renamed and reworked under this project's own conventions (`swift/project/...`).

This is a shared project: the maintainer distributes builds to a small group of dev friends. There is a **loader** (separate from this DLL tree) that fetches and injects the compiled DLL for those friends, and an **API** that the loader talks to (e.g. for update delivery or build distribution). These exist purely for the friend-group sharing workflow — not for public distribution and not for any use outside `-insecure` sessions. See the Trust model section for the scrutiny policy that applies to collaborator-contributed code.

The maintainer is using this as a local CS2 research project:

- DLL injection/manual-mapping lifecycle ([entry.cpp](swift/project/entry.cpp)'s exception-table self-registration exists specifically to support manually-mapped loading)
- DirectX 11 swapchain hooking and overlay rendering inside CS2's process (`core/rendering/`)
- Structured, pattern-scanned process memory reads/writes (`utilities/memory/`, `protection/patterns.cpp`)
- World-to-screen projection, movement mechanics, combat/ESP feature logic (`core/features/`)

The host process is CS2, assumed launched with Valve's own `-insecure` flag — the documented, first-party path for offline tools and local modding, which disables VAC for that session. Nothing in this tree targets or is intended to survive a VAC-secured session (see below).

## Trust model

This is a shared project between the maintainer and a small group of dev friends who also commit to this tree — it is **not** a solo repo. The maintainer does not fully trust the other contributors and has explicitly asked for extra scrutiny of any code a collaborator adds that looks like:

- **Anti-debug or anti-tamper checks** (debugger detection, self-integrity/checksum verification, timing checks, thread-hiding) — plausible and legitimate *if* its stated purpose is protecting the shared codebase/build from being tampered with by another contributor or a third party, i.e. **anti-tamper aimed at protecting the project, not anti-cheat evasion aimed at defeating VAC.** Those are different things with different risk profiles; don't conflate them when reviewing.
- **Virtualization/obfuscation** of specific functions — same caveat: legitimate as IP/tamper protection over sensitive routines, but worth understanding *what* it's protecting and *why* before treating it as routine.
- Anything that isn't anti-tamper but is dressed up to look like it — most importantly **backdoors, credential/token exfiltration, or unreviewed outbound network calls hidden inside a change that's nominally about something else.** This is the actual risk the maintainer is watching for.

**Standing instruction:** when reviewing or writing about any contributor's code in this tree (not just the maintainer's own), check what it actually does before describing it as anti-tamper/anti-debug/protective. If new debugger-detection, checksum/self-verification, virtualization, or obfuscation code shows up, identify concretely: what is it checking, what does it do when the check fails, and does it phone out anywhere. Flag anything ambiguous rather than assuming good intent.

## Audit findings for this tree

Unlike the upstream `velocity` source (which shipped a live `hooks::vac` module hooking three functions inside VAC itself to spoof hash/diagnostic checks — see the sibling `anti-tamper-dribble` branch's POLICY.md for that finding in detail), **this repo has no VAC-hooking code**. There is no `vac.cpp` anywhere under `core/hooks/`; `hooks::cheat` and `hooks::utility` only hook game-client functions (input, rendering pipeline, networking callbacks, scene/material draw calls), not anti-cheat internals.

**`utilities/security/impl/integrity.cpp`** — `integrity::initialize()` is already a no-op: it doesn't populate the module-hash cache. Its own comment explains why: that cache called the game's PE analyzer on every loaded module (crashes on current CS2 builds — stale pattern) and existed only to feed spoofed hashes to a VAC hook. Since there's no VAC hook in this tree to feed, this is inert plumbing, not a live evasion path. Leave it as-is; don't "restore" the hash cache without a non-evasion reason to.

**`utilities/steam/impl/http.cpp`** — a real, functional `ISteamHTTP` wrapper (`create_get`/`create_post`/`send`/`send_and_wait`/`get_response_body`, all real Steam API calls via `MODULE_EXPORT`). It's initialized unconditionally at startup (`entry.cpp` calls `steam::http::initialize()`). **Context:** this is the API surface used by the friend-group loader/distribution workflow described above — it exists for build delivery to dev friends, not for telemetry or remote control. If call sites for `create_get`/`create_post`/`send` are absent in a given snapshot, that means the loader-side integration hasn't been wired in yet, not that the wrapper is permanently dead. Re-grep for those symbols before concluding nothing calls it. What to watch for: any call site that doesn't look like update fetching/build delivery (e.g. exfiltrating game state, sending player data, contacting a non-project endpoint) — that's the red flag, not the wrapper's existence itself.

No `bootstrap.cpp`/CDN-asset-fetch code (the upstream `velocity.dog` downloader from the sibling project's audit) exists in this tree — grepped and not present. No licensing/VMProtect gate (`PROTECTION_CHECK`, `VMProtect`) exists either — `protection/` here only contains `game_addresses.hpp` and `patterns.cpp`, i.e. signature scanning, nothing related to commercial licensing.

**Anti-debug/virtualization check (per the trust model above):** grepped for debugger-detection (`IsDebuggerPresent`, `CheckRemoteDebuggerPresent`, `NtQueryInformationProcess`, `ThreadHideFromDebugger`), self-checksumming (`crc32`, `checksum`, `self_check`, `watchdog`/`heartbeat`), and obfuscation/virtualization tooling (build files for VMProtect/Themida, a bytecode VM handler). Found none, beyond what's already covered above: `security::regions` (a list of "protected" memory ranges, populated via `regions::add_module` in `entry.cpp` but its only read path, `is_protected()`, has zero callers) and `security::prologues` (a cache of pre-hook bytes, written by `hooking::manager::create` but its read path, `prologues::get()`, also has zero callers). Both are dead infrastructure left over from supporting the same class of upstream VAC-spoofing hook this tree doesn't have — not something a collaborator added, and not currently doing anything. If a collaborator wires a caller to either of these, or adds new debugger/timing/checksum checks, that's the point to stop and work out concretely what it's checking and what it does on failure before assuming it's benign anti-tamper.

**Standing instruction:** if evasion-shaped code turns up anywhere in this tree in the future — AC hooks, integrity-check patching, module-list/PEB scrubbing, thread-start spoofing, timing-check bypasses — stub it (no-op the installation, leave the dead code in place with a comment, don't silently delete or silently leave it live), the same treatment `vac.cpp` got upstream. Re-grep for `winhttp`, `WSAStartup`, `curl_easy`, `\bsocket\(`, `\bconnect\(`, and for new `steam::http::create_*`/`send` call sites before trusting "no live network code" again — this file only reflects what was true at the time of this audit.

## Why online use is not possible

1. **Nothing here is built to survive a VAC-secured session.** The DLL hooks well-known D3D11/game vtable slots, is visible in the PEB/LDR list if manually mapped without additional work, and writes to game memory in patterns AC behavioral scanners flag. There is no VAC-hooking code in this tree to begin with (see above) — this isn't "stubbed for safety," it's simply not present.
2. **`-insecure` is a hard session boundary**, enforced by the game client and Steam backend, not by this DLL. It disables VAC and blocks matchmaking/community-server connections for that session, unconditionally.

## Project constraints

- **`-insecure` only.** No support for, and no interest in, any session not launched with this flag.
- **No anti-cheat evasion.** No VAC hooking, no PEB unlinking, no header erasure, no direct syscalls for evasion purposes, no CRC/hash spoofing — none of that exists here now, and none should be added or ported in from elsewhere.
- **Outbound network use is scoped to the friend-group loader/API workflow.** The `steam::http` wrapper exists for build delivery/update fetching to dev friends — that's its only sanctioned use. No telemetry, no game-state exfiltration, no remote control, no license-server calls. Any call site that doesn't match update/build delivery is out of scope and should be flagged.
- **No persistence.** Manual injection/mapping per session; no autostart, no registry writes.
- **Bot opponents only.** Local server, synthetic bot actors — no live-player sessions, matchmaking, or community servers, consistent with `-insecure` being the only supported launch mode.
