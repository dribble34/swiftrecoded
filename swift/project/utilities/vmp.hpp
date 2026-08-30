#pragma once

// Optional VMProtect source-level markers.
//
// Define VMP at compile time (add /D VMP to PreprocessorDefinitions) and
// drop VMProtectSDK.h + VMProtectSDK.lib into the include/lib path. Without
// VMP defined the macros expand to nothing, so debug/dev builds compile
// clean without the SDK.
//
// Putting the choice of what to virtualize in source (rather than the VMP
// project file's function list) means markers survive refactors and stay
// reviewable in diffs.
//
//   VMP_ULTRA -- mutation + virtualization. Strongest, slowest. Use for
//                one-shot init, integrity checks, anti-debug internals.
//   VMP_VIRT  -- virtualization only. Slow. Rarely the right choice.
//   VMP_MUT   -- mutation only. Cheap. Use inside anything called every
//                frame (present, tick, hot loops) where full VM overhead
//                would tank framerate.

#if defined( VMP )
	#include <VMProtectSDK.h>
	#define VMP_ULTRA( tag ) VMProtectBeginUltra( tag )
	#define VMP_VIRT( tag )  VMProtectBeginVirtualization( tag )
	#define VMP_MUT( tag )   VMProtectBeginMutation( tag )
	#define VMP_END( )       VMProtectEnd( )
#else
	#define VMP_ULTRA( tag ) ( ( void ) 0 )
	#define VMP_VIRT( tag )  ( ( void ) 0 )
	#define VMP_MUT( tag )   ( ( void ) 0 )
	#define VMP_END( )       ( ( void ) 0 )
#endif
