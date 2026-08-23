#include <pch/pch.hpp>
#include <utilities/diag.hpp>
#include "../logging.hpp"

namespace logging::console {

#if defined(DEV)
	bool initialize () {
		if ( !AllocConsole() )
		{
			return false;
		}

		FILE* f;
		freopen_s( &f, "CONOUT$", "w", stdout );
		freopen_s( &f, "CONOUT$", "w", stderr );
		freopen_s( &f, "CONIN$", "r", stdin );

		SetConsoleTitleA( "swift debug console" );
		SetConsoleOutputCP( CP_UTF8 );

		return true;
	}
#else
	bool initialize () {
		return true;
	}
#endif

	void print_raw (const char* text) {
		if ( !text ) {
			return;
		}

		const bool was_emitting = emitting.get( );
		emitting.get( ) = true;
		diag::write( diag::level::info, text );
		emitting.get( ) = was_emitting;
	}

} // namespace logging::console
