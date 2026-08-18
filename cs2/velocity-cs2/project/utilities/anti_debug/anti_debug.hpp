#pragma once

// Anti-debug / anti-tamper watchdog. Portable across projects — this header
// and impl/anti_debug.cpp are self-contained (no dependency on the rest of
// the tree besides <windows.h>) and opt out of the project PCH via
// PrecompiledHeader=NotUsing in the .vcxproj. To port: copy both files,
// add matching <ClCompile>/<ClInclude> entries in the new .vcxproj, and
// call anti_debug::initialize() once from the DLL init path.

namespace anti_debug {

	// Spawn the watchdog thread. Idempotent — subsequent calls are no-ops.
	//
	// There is intentionally no query API ("is a debugger attached?") and no
	// shutdown API. A debugger-equipped attacker who can see a bool result
	// can just patch the caller's branch on it; response has to happen
	// internally, on a randomized delay, so cause and effect aren't adjacent
	// in an execution trace.
	void initialize( );

} // namespace anti_debug
