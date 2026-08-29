#pragma once

// Anti-debug / anti-tamper / anti-VM watchdog.
//
// Self-contained (only <windows.h> + CRT + one MASM file at
// impl/anti_debug_syscall.asm). Drop-in port to another project by copying
// the three files and adding the ClCompile / ClInclude / MASM entries.
//
// Design notes:
//   * No unbacked RX allocation. Direct syscalls jump to an existing
//     `syscall; ret` gadget in ntdll's real .text — RIP stays in a backed,
//     signed section for the actual kernel transition.
//   * SEC_IMAGE mapping of a clean on-disk ntdll for SSN extraction, so
//     RVAs are already relocated by the loader and no manual RVA→file-offset
//     walk is needed.
//   * Bidirectional heartbeat. Hot paths call `tick()`; the watchdog and
//     main thread both write their own atomic timestamp and read the
//     other's. If either goes stale, the process is terminated — so
//     suspending only the watchdog thread doesn't disable protection.
//   * Anti-VM is conservative on purpose. It only fires on unambiguous
//     desktop-VM vendor strings (VMware / VirtualBox / KVM / QEMU / Xen /
//     Parallels). It does NOT fire on "Microsoft Hv" alone, which is
//     present on every Windows 11 box with VBS / Memory Integrity /
//     Hyper-V role — a common bare-metal false positive.

namespace anti_debug {

	// Spawn the watchdog thread. Idempotent.
	//
	// No shutdown or query API — see the top-of-file rationale. Cause and
	// effect stay non-adjacent so a debugger trace can't correlate "this
	// check returned true" with "process died".
	void initialize( );

	// Cheap in-thread check + main-thread heartbeat bump. Call from hot
	// paths in whatever thread carries the game/frame loop. Runs only
	// hardware-breakpoint and PEB flag checks (both single-instruction,
	// no syscall) and updates the main-thread heartbeat used by the
	// watchdog liveness check. Safe to call before initialize() returned
	// or even before it was called at all (no-ops).
	void tick( );

} // namespace anti_debug
