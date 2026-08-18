// Self-contained: opts out of the project PCH (see .vcxproj) so this file
// depends only on <windows.h> and the CRT. Keeping it standalone lets it
// drop into a new project without dragging phnt / inline-syscall along.

#include "../anti_debug.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <atomic>

// Themida SDK markers were tried here (VM_START/VM_END, then
// MUTATE_START/MUTATE_END wrapping initialize() and the watchdog check
// block) — clang-cl's whole-program optimization on Ship reorganizes the
// code between markers in ways Themida's marker scanner rejects with
// "One of your protection macros contains an error". Dropped them:
// Themida's whole-file protection (Anti-Debugger Detection, section
// encryption, Advanced API-Wrapping) already covers this code equally
// to everything else in the DLL.
// #include <external/themida-sdk/themida_wrap.hpp>

namespace anti_debug {
namespace {

	// ---------------------------------------------------------------- SSN

	// Manually map ntdll.dll from disk once, so extracted syscall numbers
	// come from the clean on-disk bytes, not from a copy a usermode hook
	// (ScyllaHide, an EDR product, whatever) may have edited in memory.
	struct fresh_ntdll {
		std::uint8_t* base = nullptr;
		bool ok = false;

		fresh_ntdll( ) {
			wchar_t path[ MAX_PATH ]{};
			UINT n = GetSystemDirectoryW( path, MAX_PATH );
			if ( !n || n >= MAX_PATH - 12 )
				return;
			std::memcpy( path + n, L"\\ntdll.dll", sizeof( L"\\ntdll.dll" ) );

			HANDLE file = CreateFileW(
				path, GENERIC_READ, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( file == INVALID_HANDLE_VALUE )
				return;

			HANDLE map = CreateFileMappingW( file, nullptr, PAGE_READONLY, 0, 0, nullptr );
			CloseHandle( file );
			if ( !map )
				return;

			base = static_cast<std::uint8_t*>( MapViewOfFile( map, FILE_MAP_READ, 0, 0, 0 ) );
			CloseHandle( map );
			ok = base != nullptr;
		}

		~fresh_ntdll( ) {
			if ( base )
				UnmapViewOfFile( base );
		}
	};

	std::uint32_t rva_to_offset( IMAGE_NT_HEADERS* nt, std::uint32_t rva ) {
		auto* sec = IMAGE_FIRST_SECTION( nt );
		for ( int i = 0; i < nt->FileHeader.NumberOfSections; ++i ) {
			if ( rva >= sec[ i ].VirtualAddress &&
				rva <  sec[ i ].VirtualAddress + sec[ i ].Misc.VirtualSize )
				return rva - sec[ i ].VirtualAddress + sec[ i ].PointerToRawData;
		}
		return 0;
	}

	// Parse the export table, find `name`, and pull the SSN out of the
	// canonical stub prologue: 4c 8b d1  b8 XX XX 00 00  0f 05  c3.
	std::uint32_t extract_ssn( const fresh_ntdll& n, const char* name ) {
		if ( !n.ok )
			return 0;
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>( n.base );
		if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
			return 0;
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>( n.base + dos->e_lfanew );
		if ( nt->Signature != IMAGE_NT_SIGNATURE )
			return 0;

		auto& dir = nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ];
		if ( !dir.VirtualAddress )
			return 0;

		auto* exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(
			n.base + rva_to_offset( nt, dir.VirtualAddress ) );
		auto* names = reinterpret_cast<std::uint32_t*>(
			n.base + rva_to_offset( nt, exp->AddressOfNames ) );
		auto* ords = reinterpret_cast<std::uint16_t*>(
			n.base + rva_to_offset( nt, exp->AddressOfNameOrdinals ) );
		auto* funcs = reinterpret_cast<std::uint32_t*>(
			n.base + rva_to_offset( nt, exp->AddressOfFunctions ) );

		for ( std::uint32_t i = 0; i < exp->NumberOfNames; ++i ) {
			auto* fname = reinterpret_cast<const char*>(
				n.base + rva_to_offset( nt, names[ i ] ) );
			if ( std::strcmp( fname, name ) != 0 )
				continue;

			std::uint32_t rva = funcs[ ords[ i ] ];
			auto* stub = n.base + rva_to_offset( nt, rva );
			if ( stub[ 0 ] == 0x4C && stub[ 1 ] == 0x8B && stub[ 2 ] == 0xD1 && stub[ 3 ] == 0xB8 ) {
				std::uint32_t ssn = 0;
				std::memcpy( &ssn, stub + 4, sizeof( ssn ) );
				return ssn;
			}
			return 0;
		}
		return 0;
	}

	// ------------------------------------------------------------ stub gen

	// Emit a minimal syscall trampoline in freshly-allocated RX memory:
	//     mov r10, rcx
	//     mov eax, <ssn>
	//     syscall
	//     ret
	void* emit_stub( std::uint32_t ssn ) {
		auto* p = static_cast<std::uint8_t*>(
			VirtualAlloc( nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ) );
		if ( !p )
			return nullptr;

		p[ 0 ] = 0x4C; p[ 1 ] = 0x8B; p[ 2 ] = 0xD1;
		p[ 3 ] = 0xB8; std::memcpy( p + 4, &ssn, sizeof( ssn ) );
		p[ 8 ] = 0x0F; p[ 9 ] = 0x05;
		p[ 10 ] = 0xC3;

		DWORD old = 0;
		if ( !VirtualProtect( p, 32, PAGE_EXECUTE_READ, &old ) ) {
			VirtualFree( p, 0, MEM_RELEASE );
			return nullptr;
		}
		FlushInstructionCache( GetCurrentProcess( ), p, 32 );
		return p;
	}

	using nt_query_information_process_t = LONG( NTAPI* )( HANDLE, ULONG, PVOID, ULONG, PULONG );
	using nt_set_information_thread_t    = LONG( NTAPI* )( HANDLE, ULONG, PVOID, ULONG );

	nt_query_information_process_t g_nt_query_information_process = nullptr;
	nt_set_information_thread_t    g_nt_set_information_thread    = nullptr;

	bool resolve_syscalls( ) {
		fresh_ntdll n;
		if ( !n.ok )
			return false;

		std::uint32_t ssn_qip = extract_ssn( n, "NtQueryInformationProcess" );
		std::uint32_t ssn_sit = extract_ssn( n, "NtSetInformationThread" );
		if ( !ssn_qip || !ssn_sit )
			return false;

		g_nt_query_information_process =
			reinterpret_cast<nt_query_information_process_t>( emit_stub( ssn_qip ) );
		g_nt_set_information_thread =
			reinterpret_cast<nt_set_information_thread_t>( emit_stub( ssn_sit ) );
		return g_nt_query_information_process && g_nt_set_information_thread;
	}

	// ---------------------------------------------------------------- checks

	// PROCESSINFOCLASS values (not all in winternl.h).
	constexpr ULONG PROCESS_DEBUG_PORT          = 7;
	constexpr ULONG PROCESS_DEBUG_OBJECT_HANDLE = 30;
	constexpr ULONG PROCESS_DEBUG_FLAGS         = 31;
	constexpr ULONG THREAD_HIDE_FROM_DEBUGGER   = 17;

	bool check_debug_port_syscall( ) {
		if ( !g_nt_query_information_process )
			return false;
		DWORD_PTR port = 0;
		if ( g_nt_query_information_process(
			GetCurrentProcess( ), PROCESS_DEBUG_PORT,
			&port, sizeof( port ), nullptr ) >= 0 ) {
			return port != 0;
		}
		return false;
	}

	bool check_debug_object_syscall( ) {
		if ( !g_nt_query_information_process )
			return false;
		HANDLE h = nullptr;
		if ( g_nt_query_information_process(
			GetCurrentProcess( ), PROCESS_DEBUG_OBJECT_HANDLE,
			&h, sizeof( h ), nullptr ) >= 0 ) {
			return h != nullptr;
		}
		return false;
	}

	// ProcessDebugFlags returns the NoDebugInherit bit: 0 = debug flag set
	// (i.e. process is being debugged), 1 = not being debugged.
	bool check_debug_flags_syscall( ) {
		if ( !g_nt_query_information_process )
			return false;
		ULONG flags = 1;
		if ( g_nt_query_information_process(
			GetCurrentProcess( ), PROCESS_DEBUG_FLAGS,
			&flags, sizeof( flags ), nullptr ) >= 0 ) {
			return flags == 0;
		}
		return false;
	}

	bool check_peb_flags( ) {
		auto* peb = reinterpret_cast<std::uint8_t*>( __readgsqword( 0x60 ) );
		if ( peb[ 0x02 ] != 0 )                                       // BeingDebugged
			return true;
		std::uint32_t nt_global = 0;
		std::memcpy( &nt_global, peb + 0xBC, sizeof( nt_global ) );   // NtGlobalFlag
		if ( nt_global & 0x70 )                                       // heap-tail/free/param checks
			return true;
		return false;
	}

	bool check_hardware_breakpoints( ) {
		CONTEXT ctx{};
		ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		if ( !GetThreadContext( GetCurrentThread( ), &ctx ) )
			return false;
		return ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3;
	}

	// Measure a small deterministic chunk of work with QPC. A hit software
	// breakpoint / active single-step blows the elapsed time well past the
	// nominal <1ms this loop takes, so a stall past the threshold is a signal.
	// Threshold is generous (100 ms) because we insta-kill on any single hit
	// now — false positives cost more than they used to.
	bool check_timing_stall( ) {
		LARGE_INTEGER freq{};
		if ( !QueryPerformanceFrequency( &freq ) || freq.QuadPart == 0 )
			return false;

		LARGE_INTEGER s{}, e{};
		QueryPerformanceCounter( &s );
		volatile std::uint64_t acc = 0;
		for ( int i = 0; i < 1024; ++i )
			acc += __rdtsc( );
		( void ) acc;
		QueryPerformanceCounter( &e );

		double ms = static_cast<double>( e.QuadPart - s.QuadPart ) * 1000.0
			/ static_cast<double>( freq.QuadPart );
		return ms > 100.0;
	}

	// ------------------------------------------------------------ anti-dump

	// Zero the DOS header, NT headers, and section table of *our own* mapped
	// module. The OS loader is done reading them by the time we run, so this
	// is safe — execution keeps working because exports and TLS were resolved
	// at load time. What it breaks:
	//   * Scylla / PE-sieve / Process Hacker "dump module" — they scan
	//     process memory for "MZ" and can no longer find our base.
	//   * Even a dumper that finds us via the LDR list gets an image with
	//     no section table, which is unreconstructable without heavy manual
	//     work (where does .text end, where does .rdata start?).
	// Doesn't stop kernel-mode dumping or someone who already snapshotted
	// the file on disk. Cheap layer, not a silver bullet.
	void erase_pe_headers( ) {
		HMODULE self = nullptr;
		if ( !GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>( &erase_pe_headers ),
				&self ) || !self )
			return;

		auto* base = reinterpret_cast<std::uint8_t*>( self );
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>( base );
		if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
			return;

		// Wipe range = start of image through end of section table. This
		// covers the DOS stub, rich header, NT headers, data directories,
		// and every IMAGE_SECTION_HEADER entry.
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>( base + dos->e_lfanew );
		if ( nt->Signature != IMAGE_NT_SIGNATURE )
			return;

		std::size_t wipe_len =
			static_cast<std::size_t>( dos->e_lfanew )
			+ sizeof( IMAGE_NT_HEADERS )
			+ nt->FileHeader.NumberOfSections * sizeof( IMAGE_SECTION_HEADER );

		DWORD old = 0;
		if ( !VirtualProtect( base, wipe_len, PAGE_READWRITE, &old ) )
			return;
		std::memset( base, 0, wipe_len );
		VirtualProtect( base, wipe_len, old, &old );
	}

	// ------------------------------------------------------------- watchdog

	std::uint32_t xorshift32( std::uint32_t& s ) {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}

	DWORD WINAPI watchdog( LPVOID ) {
		// Hide this thread from an attached debugger. Requires a working
		// direct syscall (bypasses any usermode ntdll hook, incl. ScyllaHide).
		if ( g_nt_set_information_thread ) {
			g_nt_set_information_thread(
				GetCurrentThread( ), THREAD_HIDE_FROM_DEBUGGER, nullptr, 0 );
		}

		std::uint32_t seed = static_cast<std::uint32_t>( __rdtsc( ) ) | 1u;
		for ( ;; ) {
			// Run every check every pass — never short-circuit — so the
			// number of ticks / order of execution doesn't fingerprint
			// which check was the one that fired.
			bool hit = false;
			hit |= check_debug_port_syscall( );
			hit |= check_debug_object_syscall( );
			hit |= check_debug_flags_syscall( );
			hit |= check_peb_flags( );
			hit |= check_hardware_breakpoints( );
			hit |= check_timing_stall( );

			// Single-hit instant kill. No delay, no threshold. False positives
			// cost the whole process, which is why the timing check above uses
			// a generous 100 ms threshold — the other five checks are all
			// deterministic OS/hardware state, no FP risk.
			if ( hit )
				TerminateProcess( GetCurrentProcess( ), 0xC0000005 );

			// Randomized 6-25 ms sleep — a fixed period is itself a fingerprint.
			DWORD sleep_ms = 6 + ( xorshift32( seed ) % 20u );
			Sleep( sleep_ms );
		}
	}

	std::atomic<bool> g_initialized{ false };

} // namespace

void initialize( ) {
	bool expected = false;
	if ( !g_initialized.compare_exchange_strong(
			expected, true, std::memory_order_acq_rel ) )
		return;

	if ( !resolve_syscalls( ) )
		return;

	HANDLE h = CreateThread( nullptr, 0, watchdog, nullptr, 0, nullptr );
	if ( h )
		CloseHandle( h );

	// NOTE: erase_pe_headers() is intentionally NOT called here in this
	// codebase — utilities/security/integrity.cpp runs later in the init
	// sequence and reads our own PE headers to CRC the module, which
	// crashes if we've already wiped them. Themida's SecureEngine handles
	// anti-dump with correct timing anyway. The function is kept above for
	// drop-in use in a codebase that doesn't self-CRC.
}

} // namespace anti_debug
