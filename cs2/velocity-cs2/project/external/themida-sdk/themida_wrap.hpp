#pragma once

// Small wrapper around Themida's SDK so anti_debug.cpp can hold VM_START /
// VM_END / MUTATE_* / STR_ENCRYPT_* markers unconditionally without also
// dragging in a dependency on SecureEngineSDK64.dll for builds that never
// go through Themida (Development, or a Ship build you want to iterate
// against without the packer in the loop).
//
// Define THEMIDA_PROTECTED_BUILD in the .vcxproj for configurations whose
// output *will* be run through Themida — the real SDK gets pulled in and
// #pragma comment(lib, "SecureEngineSDK64.lib") kicks the import in.
//
// Everywhere else the markers no-op cleanly.

#ifdef THEMIDA_PROTECTED_BUILD

#include "ThemidaSDK.h"

#else

// Statement-position no-ops.
#define VM_START           ( ( void ) 0 )
#define VM_END             ( ( void ) 0 )
#define MUTATE_START       ( ( void ) 0 )
#define MUTATE_END         ( ( void ) 0 )
#define STR_ENCRYPT_START  ( ( void ) 0 )
#define STR_ENCRYPT_END    ( ( void ) 0 )

#endif
