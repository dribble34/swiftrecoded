#include <external/xorstr.hpp>


#include <cstdio>

#include <utilities/logging/logging.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/steam/steam.hpp>

#include <core/hooks/hooks.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/rendering/rendering.hpp>

#include <utilities/diag.hpp>

// tlhelp32.h needs windows.h to already be visible -- included last, after
// the utility headers pull it in transitively, otherwise the WINAPI /
// DWORD / HANDLE typedefs it uses don't exist yet.
#include <tlhelp32.h>

extern "C" BOOL WINAPI _CRT_INIT( HMODULE module_handle, DWORD reason, LPVOID reserved );

namespace {

	std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_previous_exception_filter{};
	PVOID g_vectored_exception_handler{};

	PRUNTIME_FUNCTION g_exception_table{};
	DWORD g_exception_table_count{};
	bool g_exception_table_registered{};

	LONG WINAPI diag_unhandled_exception_filter( EXCEPTION_POINTERS* info );

	// Registers this module's own .pdata with the process so x64 SEH
	// (__try/__except, vectored/unhandled exception filters) works inside
	// it even when the module never went through the Windows loader --
	// RtlLookupFunctionEntry has no way to find unwind info for a
	// manually mapped module otherwise. Must run before any code in this
	// module relies on __try/__except, including the CRT init below and
	// any C++ static initializer it runs, so it's the very first thing
	// `entry` does. Calling this on a normally LoadLibrary'd module is
	// harmless: it just adds a redundant dynamic table entry alongside
	// the loader's own static registration.
	void register_exception_table( HMODULE module_handle )
	{
		const auto base = reinterpret_cast<std::uintptr_t>( module_handle );
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>( base );
		if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
		{
			return;
		}

		const auto* nt =
			reinterpret_cast<const IMAGE_NT_HEADERS64*>( base + dos->e_lfanew );
		if ( nt->Signature != IMAGE_NT_SIGNATURE )
		{
			return;
		}

		const auto& directory =
			nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXCEPTION ];
		if ( !directory.VirtualAddress || !directory.Size )
		{
			return;
		}

		g_exception_table =
			reinterpret_cast<PRUNTIME_FUNCTION>( base + directory.VirtualAddress );
		g_exception_table_count =
			directory.Size / sizeof( RUNTIME_FUNCTION );

		g_exception_table_registered = RtlAddFunctionTable(
			g_exception_table, g_exception_table_count, base ) != FALSE;
	}

#if defined( DEV )
	hooking::jmp g_minidump_hook{};
	hooking::jmp g_terminate_process_hook{};

	BOOL WINAPI diag_minidump_write_detour(
		HANDLE process,
		DWORD process_id,
		HANDLE file,
		unsigned long dump_type,
		diag::minidump_exception_information* exception,
		void* user_stream,
		void* callback )
	{
		const auto original =
			g_minidump_hook.original<diag::minidump_write_fn>( );
		if ( !diag::g_writing_minidump.get( ) &&
			exception &&
			!exception->client_pointers &&
			exception->exception_pointers )
		{
			diag::record_crash(
				exception->exception_pointers,
				"game fatal handler (Source 2 caught the exception)" );
		}

		return original
			? original(
				process,
				process_id,
				file,
				dump_type,
				exception,
				user_stream,
				callback )
			: FALSE;
	}

	bool install_game_crash_capture( )
	{
		if ( !diag::g_minidump_write )
		{
			return false;
		}

		if ( !hooking::manager::create( {
				{
					&g_minidump_hook,
					reinterpret_cast<void*>( diag_minidump_write_detour ),
					"dbghelp!MiniDumpWriteDump",
					reinterpret_cast<std::uintptr_t>( diag::g_minidump_write )
				}
			} ) )
		{
			diag::write(
				diag::level::warning,
				"failed to hook the Source 2 minidump path; "
				"only self/unhandled crashes will be captured" );
			return false;
		}

		diag::write(
			diag::level::info,
			"Source 2 fatal minidump path hooked" );
		return true;
	}

	BOOL WINAPI diag_terminate_process_detour(
		HANDLE process,
		UINT exit_code )
	{
		const auto original =
			g_terminate_process_hook.original<decltype( &TerminateProcess )>( );

		if ( GetProcessId( process ) == GetCurrentProcessId( ) )
		{
			char stage[ 96 ]{};
			_snprintf_s(
				stage,
				sizeof( stage ),
				_TRUNCATE,
				"TerminateProcess requested for CS2; exit_code=0x%08X",
				exit_code );
			diag::capture_snapshot( stage );
		}

		return original ? original( process, exit_code ) : FALSE;
	}

	bool install_termination_capture( )
	{
		const auto kernel32 = GetModuleHandleW( L"kernel32.dll" );
		const auto terminate_process = kernel32
			? GetProcAddress( kernel32, "TerminateProcess" )
			: nullptr;
		if ( !terminate_process ||
			!hooking::manager::create( {
				{
					&g_terminate_process_hook,
					reinterpret_cast<void*>( diag_terminate_process_detour ),
					"kernel32!TerminateProcess",
					reinterpret_cast<std::uintptr_t>( terminate_process )
				}
			} ) )
		{
			diag::write(
				diag::level::warning,
				"failed to hook forced process termination" );
			return false;
		}

		diag::write(
			diag::level::info,
			"forced process termination capture hooked" );
		return true;
	}
#endif

	LONG CALLBACK diag_vectored_exception_filter( EXCEPTION_POINTERS* info )
	{
		if ( !info || !info->ExceptionRecord ||
			!diag::is_serious_exception( info->ExceptionRecord->ExceptionCode ) )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		if ( diag::probe_active( ) )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		
		
		
		const auto displaced_filter =
			SetUnhandledExceptionFilter( diag_unhandled_exception_filter );
		if ( displaced_filter != diag_unhandled_exception_filter )
		{
			g_previous_exception_filter.store(
				displaced_filter,
				std::memory_order_release );
		}

		if ( diag::is_module_address( info->ExceptionRecord->ExceptionAddress ) )
		{
			diag::record_crash(
				info,
				diag::g_exception_scope_depth.get( )
					? diag::g_exception_phase.get( )
					: "first-chance fault in swift.fly DLL" );
			return EXCEPTION_CONTINUE_SEARCH;
		}

		if ( diag::g_exception_scope_depth.get( ) == 0 )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const auto code = info->ExceptionRecord->ExceptionCode;
		const auto instruction =
			reinterpret_cast<std::uintptr_t>( info->ExceptionRecord->ExceptionAddress );
		const auto accessed =
			info->ExceptionRecord->NumberParameters > 1
				? info->ExceptionRecord->ExceptionInformation[ 1 ]
				: 0;

		HMODULE fault_module{};
		char module_path[ MAX_PATH ]{ "unknown" };
		std::uintptr_t module_base{};
		if ( instruction &&
			GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>( instruction ), &fault_module ) )
		{
			module_base = reinterpret_cast<std::uintptr_t>( fault_module );
			GetModuleFileNameA( fault_module, module_path, MAX_PATH );
		}

		const auto* module_name = strrchr( module_path, '\\' );
		module_name = module_name ? module_name + 1 : module_path;

		char buf[ 384 ]{};
		_snprintf_s(
			buf, sizeof( buf ), _TRUNCATE,
			"FEATURE EXCEPTION [%s] 0x%08lX at %s+0x%llX (0x%p), accessed 0x%p",
			diag::g_exception_phase.get( ),
			code,
			module_name,
			module_base
				? static_cast<unsigned long long>( instruction - module_base )
				: 0ull,
			info->ExceptionRecord->ExceptionAddress,
			reinterpret_cast<void*>( accessed ) );
		diag::step( buf );

		return EXCEPTION_CONTINUE_SEARCH;
	}

	LONG WINAPI diag_unhandled_exception_filter( EXCEPTION_POINTERS* info )
	{
		if ( !info || !info->ExceptionRecord )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const auto instruction =
			reinterpret_cast<std::uintptr_t>( info->ExceptionRecord->ExceptionAddress );
		const auto accessed =
			info->ExceptionRecord->NumberParameters > 1
				? info->ExceptionRecord->ExceptionInformation[ 1 ]
				: 0;

		HMODULE fault_module{};
		char module_path[ MAX_PATH ]{ "unknown" };
		std::uintptr_t module_base{};
		if ( instruction &&
			GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>( instruction ), &fault_module ) )
		{
			module_base = reinterpret_cast<std::uintptr_t>( fault_module );
			GetModuleFileNameA( fault_module, module_path, MAX_PATH );
		}

		const auto* module_name = strrchr( module_path, '\\' );
		module_name = module_name ? module_name + 1 : module_path;

		char buf[ 384 ]{};
		_snprintf_s(
			buf, sizeof( buf ), _TRUNCATE,
			"UNHANDLED EXCEPTION 0x%08lX at %s+0x%llX (0x%p), accessed 0x%p",
			info->ExceptionRecord->ExceptionCode,
			module_name,
			module_base
				? static_cast<unsigned long long>( instruction - module_base )
				: 0ull,
			info->ExceptionRecord->ExceptionAddress,
			reinterpret_cast<void*>( accessed ) );
		diag::write( diag::level::fatal, buf );
		diag::record_crash( info, "unhandled exception" );

		const auto previous_filter =
			g_previous_exception_filter.load( std::memory_order_acquire );
		if ( previous_filter &&
			previous_filter != diag_unhandled_exception_filter )
		{
			return previous_filter( info );
		}

		return EXCEPTION_CONTINUE_SEARCH;
	}

#if defined( DEV )
	#define INIT_FAIL( msg ) \
		do { \
			diag::write( diag::level::error, msg ); \
			return 0; \
		} while ( 0 )

	#define INIT_WARN( msg ) diag::write( diag::level::warning, msg )
#else
	#define INIT_FAIL( msg ) \
		do { \
			diag::write( diag::level::error, msg ); \
			MessageBoxA( nullptr, xs( msg ), xs( "..." ), MB_ICONERROR ); \
			return 0; \
		} while ( 0 )

	#define INIT_WARN( msg ) INIT_FAIL( msg )
#endif

	DWORD WINAPI init_thread_impl( LPVOID /*param*/ )
	{
		diag::step( "stage: thread start" );
		diag::initialize_crash_dumps( );

		g_previous_exception_filter.store(
			SetUnhandledExceptionFilter( diag_unhandled_exception_filter ),
			std::memory_order_release );
		g_vectored_exception_handler =
			AddVectoredExceptionHandler( 1, diag_vectored_exception_filter );
		if ( !g_vectored_exception_handler )
		{
			diag::writef(
				diag::level::error,
				"failed to install vectored exception handler; win32_error=%lu",
				GetLastError( ) );
		}
		else
		{
			diag::write( diag::level::info, "crash handlers installed" );
		}

		diag::step( "stage: coinit" );
		const auto coinit_result =
			CoInitializeEx( nullptr, COINIT_MULTITHREADED );
		if ( FAILED( coinit_result ) )
		{
			diag::writef(
				diag::level::warning,
				"CoInitializeEx failed; hresult=0x%08lX",
				coinit_result );
		}

		diag::step( "stage: settings" );
		config::initialize( );
		settings::finalize_binds( );

		diag::step( "stage: logging" );
		{
			if ( !logging::console::initialize( ) )
			{
#if defined( DEV )
				INIT_WARN( "failed to initialize console logging." );
#else
				INIT_FAIL( "failed to initialize console logging." );
#endif
			}

			if ( !logging::popup::initialize( ) )
			{
#if defined( DEV )
				INIT_WARN( "failed to initialize popup logging." );
#else
				INIT_FAIL( "failed to initialize popup logging." );
#endif
			}
		}

		diag::step( "stage: crash capture" );
		{
#if defined( DEV )
			install_game_crash_capture( );
			install_termination_capture( );
#endif

			if ( !threadpool::initialize( ) )
			{
				INIT_FAIL( "failed to initialize thread pool." );
			}

			if ( !steam::http::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam http." );
			}

			if ( !steam::friends::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam friends." );
			}

			if ( !steam::user::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam user." );
			}

			if ( !steam::utils::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam utils." );
			}
		}

		diag::step( "stage: addresses" );
		{
			if ( !addresses::modules::initialize( ) )
			{
				INIT_FAIL( "failed to initialize module addresses." );
			}

			if ( !addresses::globals::initialize( ) )
			{
				INIT_FAIL( "failed to initialize global addresses." );
			}

			if ( !addresses::functions::initialize( ) )
			{
				INIT_FAIL( "failed to initialize function addresses." );
			}
		}

		diag::step( "stage: systems" );
		{
			if ( !systems::materials::initialize( ) )
			{
				INIT_FAIL( "failed to initialize materials system." );
			}

			if ( !systems::events::initialize( ) )
			{
				INIT_FAIL( "failed to initialize event system." );
			}

			if ( !systems::g_icons.initialize( ) )
			{
				INIT_FAIL( "failed to initialize vpk parse system." );
			}

			if ( !systems::g_model_preview.initialize( ) )
			{
				INIT_FAIL( "failed to initialize model preview system." );
			}
		}

		diag::step( "stage: econ" );
		{
			if ( !features::changer::g_econ_item_system.initialize( ) )
			{
				INIT_FAIL( "failed to initialize econ item system." );
			}
		}

		diag::step( "stage: hooks" );
		{
			if ( !hooks::utility::initialize( ) )
			{
				INIT_FAIL( "failed to initialize utility hooks." );
			}

			if ( !hooks::cheat::initialize( ) )
			{
				INIT_FAIL( "failed to initialize cheat hooks." );
			}
		}

		diag::step( "stage: cvars" );
		{
			if ( !addresses::globals::cvar->unlock_all( ) )
			{
				INIT_FAIL( "failed to unlock hidden cvars." );
			}
		}

		diag::step( "stage: skyboxes" );
		features::world::g_scene.discover_skyboxes( );

		// --- STUBBED (see POLICY.md "Audit findings" / standing instruction) ---
		// The "stage: anti-debug" PEB.BeingDebugged check and the
		// "stage: analysis tool scan" CreateToolhelp32Snapshot process-list
		// walk (27-entry debugger/disassembler/memory-tool blocklist, both
		// with TerminateProcess( ..., 0xC0000005 ) on hit in non-DEV builds)
		// were reintroduced inline in commit e42d6df, despite that commit's
		// message stating utilities/anti_debug was "dropped".
		//
		// This is anti-analysis code, not project anti-tamper, so per the
		// standing instruction it is no-op'd rather than deleted. It also
		// hung manual-map injection: CreateToolhelp32Snapshot this early on
		// the mapped init thread can block on the loader lock / CSRSS RPC,
		// and the non-DEV TerminateProcess paths killed cs2 outright when a
		// listed tool happened to be running.
		//
		// Original blocklist retained here as a dead reference only:
		//   x64dbg/x32dbg, windbg(x), ollydbg, ida(64/q/w), binaryninja,
		//   dnspy(-x86/-x64), cheatengine(-i386/-x86_64), reclass(.net),
		//   scylla(_x64/_x86), pe-sieve(64), processhacker, SystemInformer.
		// Do not re-enable without a non-evasion reason and a review.
		// ---------------------------------------------------------------------

		diag::step( "stage: done" );
		return 1;
	}

	DWORD diag_exception_filter( EXCEPTION_POINTERS* info )
	{
		char buf[ 128 ]{};
		_snprintf_s( buf, sizeof( buf ), _TRUNCATE, "EXCEPTION 0x%08lX at 0x%p", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress );
		diag::write( diag::level::fatal, buf );
		diag::record_crash( info, "initialization thread" );
		return EXCEPTION_EXECUTE_HANDLER;
	}

	DWORD WINAPI init_thread( LPVOID param )
	{
		__try
		{
			return init_thread_impl( param );
		}
		__except ( diag_exception_filter( GetExceptionInformation( ) ) )
		{
			return 0;
		}
	}

} 

extern "C" int __stdcall entry( HMODULE module_handle, DWORD reason, LPVOID reserved )
{
	if ( reason == DLL_PROCESS_ATTACH )
	{
		register_exception_table( module_handle );

		_CRT_INIT( module_handle, reason, reserved );
		DisableThreadLibraryCalls( module_handle );

		diag::set_module( module_handle );
		diag::step( "stage: dll attach" );
		diag::writef(
			diag::level::info,
			"exception table self-registration: %s",
			g_exception_table_registered ? "ok" : "failed" );
		diag::step( "build: development diagnostics" );

		diag::step( "stage: crt done, spawning thread" );

		const auto thread = CreateThread( nullptr, 0, init_thread, module_handle, 0, nullptr );
		if ( !thread )
		{
			diag::writef(
				diag::level::error,
				"failed to create initialization thread; win32_error=%lu",
				GetLastError( ) );
			return 0;
		}

		CloseHandle( thread );
		return 1;
	}
	else if ( reason == DLL_PROCESS_DETACH )
	{
		if ( g_exception_table_registered )
		{
			RtlDeleteFunctionTable( g_exception_table );
			g_exception_table_registered = false;
		}

#if defined( DEV )
		if ( g_vectored_exception_handler )
		{
			RemoveVectoredExceptionHandler( g_vectored_exception_handler );
			g_vectored_exception_handler = nullptr;
		}

		const auto previous_filter =
			g_previous_exception_filter.exchange(
				nullptr,
				std::memory_order_acq_rel );
		const auto current_filter =
			SetUnhandledExceptionFilter( previous_filter );
		if ( current_filter != diag_unhandled_exception_filter )
		{
			SetUnhandledExceptionFilter( current_filter );
		}

		g_terminate_process_hook.reset( );
		g_minidump_hook.reset( );

		systems::g_model_preview.shutdown( );
		features::world::g_weather.release( );
		rendering::g_menu.shutdown( );

		systems::events::shutdown( );
		hooks::utility::shutdown( );
		hooks::cheat::shutdown( );
		CoUninitialize( );

		
		FreeConsole( );
#endif

		diag::shutdown( );

#if defined( DEV )
		_CRT_INIT( module_handle, reason, reserved );
#endif
	}

	return 1;
}
