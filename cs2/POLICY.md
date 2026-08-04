# POLICY.md — Project Scope & Operational Boundaries

## What this project is

This tree is **`velocity`**, an open-sourced Counter-Strike 2 internal cheat (a Windows DLL, `velocity-cs2.dll`, per `velocity-main/cs2/velocity-cs2/velocity-cs2.vcxproj`). It was a real, previously-active public cheat with a paying user base (see `velocity-main/README.md`'s donation addresses and Discord links) that shut down and released its source rather than continuing development ("velocity reborn" is the stated successor, developed elsewhere — not in this tree).

The maintainer is treating this open-sourced code as a new base for their own local CS2 research, for the same class of study as their other project (`dribbleHook` / `cs2_inter`):

- DLL injection lifecycle
- DirectX 11 swapchain hooking and ImGui overlay rendering inside CS2's process
- Structured, pattern-scanned process memory reads/writes
- World-to-screen projection, movement mechanics, combat/ESP feature logic

The host process is CS2, launched with Valve's own `-insecure` flag — the documented, first-party path for offline tools and local modding, which disables VAC for that session.

## Provenance

Imported as two upstream GitHub releases: `velocity-main` (the cheat) and `velocity-resourcer-main` (a companion tool). Unlike the sibling project's import, this is **not** a pre-sanitized rebuild — it's the actual shipped source, including a live anti-cheat-evasion module.

**Finding from the initial audit:** `velocity-main/cs2/velocity-cs2/project/core/hooks/impl/vac.cpp` hooked three functions internal to VAC itself:

| Hooked function | What the hook did |
|---|---|
| `analyze_pe_module` | Read back the CRC32 VAC computed for a scanned module and overwrote it with a cached "clean" value when the two didn't match — hiding tampering from VAC's own hash check. |
| `build_diagnostic_response` | Inspected VAC's outgoing diagnostic report before it was sent; nulled out the address field when VAC tried to dump memory from a self-registered "protected" region, and logged/chat-printed whenever VAC inspected a hooked module or function. |
| `string_copy` | When VAC tried to read the raw bytes of a hooked function's prologue, fed it the original (pre-hook) bytes from a cache instead of the live, patched bytes — spoofing what VAC sees on disk-vs-memory comparison. |

This is not defensive instrumentation or logging for its own sake — it is anti-cheat evasion, functioning exactly as designed, against a live anti-cheat system. It has been **stubbed**: `vac::initialize()` now returns `true` without calling `hooking::manager::create(...)`, so none of the three hooks are installed and VAC's routines run unmodified. The callback implementations remain in the file (dead code, never invoked) rather than being deleted, matching the precedent already established in the sibling `cs2_inter` project: no-op it, banner it, don't silently delete the call site or silently leave it live.

**Standing instruction, unchanged from the sibling project:** if further evasion-shaped code turns up anywhere else in this tree — more AC hooks, integrity-check patching, module-list/PEB scrubbing, thread-start spoofing, timing-check bypasses — stub it the same way. No un-stubbing `vac.cpp` "to see if it still works." No new evasion code, ported from this tree or written fresh.

## Network code — found twice, now removed both times

The sibling project's POLICY.md claimed "no network code" and asked future sessions to verify that by grepping for socket/WinHTTP/curl calls. That grep found two things here, both now neutralized:

1. **`utilities/bootstrap/bootstrap.cpp`** — on every DLL attach, spun up a background thread that used WinHTTP to fetch a UI asset pack (fonts + a couple of menu textures) from `velocity.dog` (the shut-down project's own dead CDN). This wasn't just a policy violation: connecting to a dead domain was the actual cause of a multi-minute hang on injection (DNS/connect timeout). `on_dll_attach()` is now a no-op; `try_load_rpak()` always reports not-ready. There is no local copy of that asset pack, so the menu currently renders without its custom fonts/icons — `core/rendering/impl/context.cpp`'s ESP/widget rendering was deliberately decoupled from that "assets ready" flag so the rest of the UI isn't held hostage by a permanently-missing download.
2. **`utilities/steam/impl/http.cpp`** — a real `ISteamHTTP` wrapper, initialized at startup but never actually called by any feature in this snapshot. Stubbed (`http::initialize()` now a no-op) rather than left "inert but present."

**Standing instruction:** if a future feature is added or ported in that would call through `steam::http`, or spawn any new outbound connection, treat that as new network code requiring the same scrutiny as everything else in this file — re-grep for `winhttp`, `WSAStartup`, `curl_easy`, `\bconnect\(`, `\bsocket\(` before trusting "no network code" again.

## Why online use is not possible

1. **VAC would detect this on first frame if it were live.** The DLL hooks a well-known D3D11 vtable slot, is visible in the PEB/LDR list, and writes to game memory in patterns AC behavioral scanners flag. The one piece of this codebase that specifically existed to survive a VAC-secured session (`vac.cpp`'s hooks) is now stubbed out. There is no version of this codebase, as it currently stands, that is intended to survive a VAC-secured session.
2. **`-insecure` is a hard session boundary**, enforced by the game client and Steam backend — not by this DLL. It disables VAC and blocks matchmaking/community-server connections for that session, unconditionally.
3. **VMProtect-based licensing (`protection/protection.cpp`) is a dead concern.** It gated features behind a serial-number check for the commercial product; the product is discontinued and this is a local, non-distributed copy. `PROTECTION_CHECK()` gates in `entry.cpp` are inherited plumbing, not something to route around to "unlock" anything — there's nothing behind it worth unlocking in a local-only context.

## Project constraints (non-negotiable, same as `cs2_inter`)

- **`-insecure` only.** No support for, and no interest in, any session not launched with this flag.
- **No anti-cheat evasion.** `vac.cpp`'s hook installation stays stubbed. No un-stubbing it "to see if it still works." No new PEB unlinking, header erasure, direct syscalls for evasion purposes, CRC spoofing, or any other AC-facing tampering, here or ported in from elsewhere.
- **No network code beyond what's already inert.** The Steam HTTP wrapper stays uncalled. No config sync, no telemetry, no remote control, no license-server calls.
- **No persistence.** Manual injection per session; no autostart, no registry writes.
- **Bot opponents only.** Local server, synthetic bot actors, exactly as `cs2_inter`.

These are the same constraints as the sibling project, applied to a codebase that — unlike the sibling's import — arrived with real evasion code still live, which is why the finding above is documented in this much detail: future sessions should not assume "same constraints" means "already satisfied by construction" the way it did for the other project's sanitized import.
