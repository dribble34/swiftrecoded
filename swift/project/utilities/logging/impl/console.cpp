#include <windows.h>
#include <cstdio>

#include <utilities/diag.hpp>
#include "../logging.hpp"

namespace logging::console {

#if defined( DEV )
	static HANDLE g_console_handle{};
#endif

	bool initialize () {
#if defined( DEV )
		
		if ( AllocConsole() )
		{
			
			FILE* dummy{};
			freopen_s( &dummy, "CONOUT$", "w", stdout );
			freopen_s( &dummy, "CONOUT$", "w", stderr );
			freopen_s( &dummy, "CONIN$", "r", stdin );

			
			g_console_handle = GetStdHandle( STD_OUTPUT_HANDLE );

			
			SetConsoleTitleA( "swift.fly - debug console" );

			
			DWORD mode{};
			if ( GetConsoleMode( g_console_handle, &mode ) )
			{
				mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
				SetConsoleMode( g_console_handle, mode );
			}

			diag::write( diag::level::info, "debug console initialized" );
			return true;
		}
		else
		{
			diag::write( diag::level::warning, "failed to allocate console" );
			return false;
		}
#else
		
		
		return true;
#endif
	}

	void print_raw (const char* text) {
		if ( !text ) {
			return;
		}

		const bool was_emitting = emitting;
		emitting = true;
		diag::write( diag::level::info, text );

#if defined( DEV )
		
		if ( g_console_handle )
		{
			DWORD written{};
			WriteConsoleA( g_console_handle, text, static_cast<DWORD>( strlen( text ) ), &written, nullptr );
			WriteConsoleA( g_console_handle, "\n", 1, &written, nullptr );
		}
#endif

		emitting = was_emitting;
	}

} 
