// See ../anti_debug.hpp for the full design rationale.
//
// Depends only on <windows.h> + CRT + impl/anti_debug_syscall.asm.

#include "../anti_debug.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <atomic>

// TEMPORARY diagnostic instrumentation to find a false-positive kill.
// Pulls in diag.hpp, which breaks this file's normal self-contained /
// portable-by-copy design (see the header comment) — remove this include
// and the diag::writef calls below once the cause is confirmed.
#include <utilities/diag.hpp>

// -----------------------------------------------------------------------------
// C linkage: the MASM trampoline reads this global and calls out to it.
// Named at file scope so MASM's EXTRN resolves cleanly.
// -----------------------------------------------------------------------------

extern "C" {

	std::uint64_t anti_debug_g_syscall_gadget = 0;

	LONG anti_debug_do_syscall(
		ULONG ssn,
		void* arg0,
		void* arg1,
		void* arg2,
		void* arg3,
		void* arg4 );

} // extern "C"

namespace anti_debug {
namespace {

	// -------------------------------------------------------------- ntdll map

	// Map a clean, on-disk copy of ntdll.dll with SEC_IMAGE. The loader
	// lays sections out at their VirtualAddresses for us, so RVAs (export
	// table, name table, function table) can be added straight to `base`
	// with no manual RVA→file-offset translation. This is both simpler and
	// correct in the presence of section alignment quirks the previous
	// hand-rolled walker didn't handle.
	//
	// It also means what we read is byte-for-byte the on-disk image, not
	// the in-memory copy any usermode hook (ScyllaHide, an EDR product,
	// whatever) may have edited.
	struct clean_ntdll {
		std::uint8_t* base = nullptr;
		bool ok = false;

		clean_ntdll( ) {
			wchar_t path[ MAX_PATH ]{};
			UINT n = GetSystemDirectoryW( path, MAX_PATH );
			if ( !n || n >= MAX_PATH - 12 )
				return;
			std::memcpy( path + n, L"\\ntdll.dll", sizeof( L"\\ntdll.dll" ) );

			HANDLE file = CreateFileW(
				path, GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( file == INVALID_HANDLE_VALUE )
				return;

			HANDLE map = CreateFileMappingW(
				file, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr );
			CloseHandle( file );
			if ( !map )
				return;

			base = static_cast<std::uint8_t*>(
				MapViewOfFile( map, FILE_MAP_READ, 0, 0, 0 ) );
			CloseHandle( map );
			ok = base != nullptr;
		}

		~clean_ntdll( ) {
			if ( base )
				UnmapViewOfFile( base );
		}
	};

	// Read the SSN of `name` out of the clean ntdll image. Every real NT
	// stub starts with `mov r10, rcx; mov eax, imm32`, i.e.
	//     4c 8b d1  b8 XX XX XX XX
	// so we sanity-check that prologue before believing the imm32.
	std::uint32_t extract_ssn( const clean_ntdll& n, const char* name ) {
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
			n.base + dir.VirtualAddress );
		auto* names = reinterpret_cast<std::uint32_t*>( n.base + exp->AddressOfNames );
		auto* ords  = reinterpret_cast<std::uint16_t*>( n.base + exp->AddressOfNameOrdinals );
		auto* funcs = reinterpret_cast<std::uint32_t*>( n.base + exp->AddressOfFunctions );

		for ( std::uint32_t i = 0; i < exp->NumberOfNames; ++i ) {
			auto* fname = reinterpret_cast<const char*>( n.base + names[ i ] );
			if ( std::strcmp( fname, name ) != 0 )
				continue;

			auto* stub = n.base + funcs[ ords[ i ] ];
			if ( stub[ 0 ] == 0x4C && stub[ 1 ] == 0x8B &&
				stub[ 2 ] == 0xD1 && stub[ 3 ] == 0xB8 ) {
				std::uint32_t ssn = 0;
				std::memcpy( &ssn, stub + 4, sizeof( ssn ) );
				return ssn;
			}
			return 0;
		}
		return 0;
	}

	// ---------------------------------------------------------- gadget scan

	// Locate the tail of any real syscall stub in the loaded ntdll —
	// three bytes: 0f 05 c3  (syscall; ret). Jumping here from our own
	// trampoline puts the actual syscall RIP inside ntdll's .text, which
	// is what any EDR/AC scanner will see. No unbacked RX in the loop.
	std::uint8_t* find_syscall_ret_gadget( ) {
		HMODULE h = GetModuleHandleW( L"ntdll.dll" );
		if ( !h )
			return nullptr;

		auto* base = reinterpret_cast<std::uint8_t*>( h );
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>( base );
		if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
			return nullptr;
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>( base + dos->e_lfanew );
		if ( nt->Signature != IMAGE_NT_SIGNATURE )
			return nullptr;

		auto* sec = IMAGE_FIRST_SECTION( nt );
		for ( int i = 0; i < nt->FileHeader.NumberOfSections; ++i ) {
			if ( std::memcmp( sec[ i ].Name, ".text", 5 ) != 0 )
				continue;

			std::uint8_t* p = base + sec[ i ].VirtualAddress;
			std::uint8_t* end = p + sec[ i ].Misc.VirtualSize - 3;
			for ( ; p < end; ++p ) {
				if ( p[ 0 ] == 0x0F && p[ 1 ] == 0x05 && p[ 2 ] == 0xC3 )
					return p;
			}
			break;
		}
		return nullptr;
	}

	// --------------------------------------------------------------- syscalls

	std::uint32_t g_ssn_nt_query_information_process = 0;
	std::uint32_t g_ssn_nt_set_information_thread = 0;
	std::uint32_t g_ssn_nt_query_system_information = 0;
	std::uint32_t g_ssn_nt_get_context_thread = 0;

	bool resolve_syscalls( ) {
		clean_ntdll n;
		if ( !n.ok )
			return false;

		g_ssn_nt_query_information_process = extract_ssn( n, "NtQueryInformationProcess" );
		g_ssn_nt_set_information_thread    = extract_ssn( n, "NtSetInformationThread" );
		g_ssn_nt_query_system_information  = extract_ssn( n, "NtQuerySystemInformation" );
		g_ssn_nt_get_context_thread        = extract_ssn( n, "NtGetContextThread" );
		if ( !g_ssn_nt_query_information_process ||
			!g_ssn_nt_set_information_thread ||
			!g_ssn_nt_query_system_information ||
			!g_ssn_nt_get_context_thread )
			return false;

		auto* gadget = find_syscall_ret_gadget( );
		if ( !gadget )
			return false;

		anti_debug_g_syscall_gadget = reinterpret_cast<std::uint64_t>( gadget );
		return true;
	}

	// Verify the ntdll `syscall; ret` gadget we jump into hasn't been
	// overwritten (e.g. `0f 05 c3` replaced by `e9 XX XX XX XX` to hijack
	// the transition). Cheap enough to run before every direct syscall.
	inline bool verify_gadget( ) {
		if ( !anti_debug_g_syscall_gadget )
			return false;
		auto* p = reinterpret_cast<const std::uint8_t*>( anti_debug_g_syscall_gadget );
		return p[ 0 ] == 0x0F && p[ 1 ] == 0x05 && p[ 2 ] == 0xC3;
	}

	// PROCESSINFOCLASS values (not all in winternl.h).
	constexpr ULONG PROCESS_DEBUG_PORT          = 7;
	constexpr ULONG PROCESS_DEBUG_OBJECT_HANDLE = 30;
	constexpr ULONG PROCESS_DEBUG_FLAGS         = 31;
	constexpr ULONG THREAD_HIDE_FROM_DEBUGGER   = 17;

	// SYSTEM_INFORMATION_CLASS.
	constexpr ULONG SystemKernelDebuggerInformation     = 0x23;
	constexpr ULONG SystemExtendedHandleInformation     = 0x40;

	LONG nt_query_information_process( ULONG cls, void* buf, ULONG len, PULONG ret_len ) {
		if ( !verify_gadget( ) )
			TerminateProcess( GetCurrentProcess( ), 0xC0000005 );
		return anti_debug_do_syscall(
			g_ssn_nt_query_information_process,
			GetCurrentProcess( ),
			reinterpret_cast<void*>( static_cast<std::uintptr_t>( cls ) ),
			buf,
			reinterpret_cast<void*>( static_cast<std::uintptr_t>( len ) ),
			ret_len );
	}

	LONG nt_set_information_thread( ULONG cls, void* buf, ULONG len ) {
		if ( !verify_gadget( ) )
			TerminateProcess( GetCurrentProcess( ), 0xC0000005 );
		return anti_debug_do_syscall(
			g_ssn_nt_set_information_thread,
			GetCurrentThread( ),
			reinterpret_cast<void*>( static_cast<std::uintptr_t>( cls ) ),
			buf,
			reinterpret_cast<void*>( static_cast<std::uintptr_t>( len ) ),
			nullptr );
	}

	// Six-parameter direct syscall wrapper; ret_len is optional.
	// Signature matches NtQuerySystemInformation ABI:
	//   (SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength).
	LONG nt_query_system_information( ULONG cls, void* buf, ULONG len, PULONG ret_len ) {
		if ( !verify_gadget( ) )
			TerminateProcess( GetCurrentProcess( ), 0xC0000005 );
		return anti_debug_do_syscall(
			g_ssn_nt_query_system_information,
			reinterpret_cast<void*>( static_cast<std::uintptr_t>( cls ) ),
			buf,
			reinterpret_cast<void*>( static_cast<std::uintptr_t>( len ) ),
			ret_len,
			nullptr );
	}

	// NtGetContextThread(HANDLE, PCONTEXT). Goes straight to the syscall
	// so ScyllaHide / other usermode hooks on the ntdll stub can't zero
	// out the DR fields before returning them to us. The kernel populates
	// whichever registers the ContextFlags field asks for — CONTEXT_DEBUG_REGISTERS
	// for our DR reads. Accepts the (HANDLE)-2 GetCurrentThread pseudo
	// handle just like the usermode variant.
	LONG nt_get_context_thread( HANDLE thread, PCONTEXT ctx ) {
		if ( !verify_gadget( ) )
			TerminateProcess( GetCurrentProcess( ), 0xC0000005 );
		return anti_debug_do_syscall(
			g_ssn_nt_get_context_thread,
			thread,
			ctx,
			nullptr,
			nullptr,
			nullptr );
	}

	// ---------------------------------------------------------------- checks

	bool check_debug_port( ) {
		if ( !anti_debug_g_syscall_gadget )
			return false;
		DWORD_PTR port = 0;
		if ( nt_query_information_process(
				PROCESS_DEBUG_PORT, &port, sizeof( port ), nullptr ) >= 0 )
			return port != 0;
		return false;
	}

	bool check_debug_object( ) {
		if ( !anti_debug_g_syscall_gadget )
			return false;
		HANDLE h = nullptr;
		if ( nt_query_information_process(
				PROCESS_DEBUG_OBJECT_HANDLE, &h, sizeof( h ), nullptr ) >= 0 )
			return h != nullptr;
		return false;
	}

	// ProcessDebugFlags returns the NoDebugInherit bit: 0 = process is
	// being debugged, 1 = not being debugged.
	bool check_debug_flags( ) {
		if ( !anti_debug_g_syscall_gadget )
			return false;
		ULONG flags = 1;
		if ( nt_query_information_process(
				PROCESS_DEBUG_FLAGS, &flags, sizeof( flags ), nullptr ) >= 0 )
			return flags == 0;
		return false;
	}

	bool check_peb_flags( ) {
		auto* peb = reinterpret_cast<std::uint8_t*>( __readgsqword( 0x60 ) );
		if ( peb[ 0x02 ] != 0 )                                       // BeingDebugged
			return true;
		std::uint32_t nt_global = 0;
		std::memcpy( &nt_global, peb + 0xBC, sizeof( nt_global ) );   // NtGlobalFlag
		if ( nt_global & 0x70 )                                       // heap-tail / free / param
			return true;
		return false;
	}

	// Hardware breakpoints are per-thread. Reading DR on the calling
	// thread (the watchdog) only catches HW BPs set on the watchdog
	// itself, which no attacker would do. Enumerate every thread in
	// our process and read DR on each.
	//
	// The variant that only checks GetCurrentThread() is still used
	// inline from tick(), where it's already on the right thread and
	// cheaper than a full snapshot.
	bool check_hardware_breakpoints_all_threads( ) {
		DWORD our_pid = GetCurrentProcessId( );
		DWORD our_tid = GetCurrentThreadId( );

		HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
		if ( snap == INVALID_HANDLE_VALUE )
			return false;

		THREADENTRY32 te{};
		te.dwSize = sizeof( te );
		bool hit = false;

		if ( Thread32First( snap, &te ) ) {
			do {
				if ( te.th32OwnerProcessID != our_pid ||
					te.th32ThreadID == our_tid )
					continue;

				HANDLE h = OpenThread(
					THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
					FALSE, te.th32ThreadID );
				if ( !h )
					continue;

				CONTEXT ctx{};
				ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				if ( nt_get_context_thread( h, &ctx ) >= 0 ) {
					if ( ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 ) {
						hit = true;
					}
				}
				CloseHandle( h );

				if ( hit )
					break;
			} while ( Thread32Next( snap, &te ) );
		}

		CloseHandle( snap );
		return hit;
	}

	bool check_hardware_breakpoints_this_thread( ) {
		CONTEXT ctx{};
		ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		if ( nt_get_context_thread( GetCurrentThread( ), &ctx ) < 0 )
			return false;
		return ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3;
	}

	// SystemKernelDebuggerInformation returns two BOOLEANs:
	//   KernelDebuggerEnabled  — /debug set in the boot config
	//   KernelDebuggerNotPresent — no debugger currently attached
	// A kd/WinDbg kernel debugger attached to the local machine sets
	// Enabled && !NotPresent. Cheap two-byte read that catches the
	// entire class of kernel-debugging attackers that all our other
	// checks (usermode PEB / debug port / DR) miss.
	bool check_kernel_debugger( ) {
		struct kdbg_info {
			BOOLEAN KernelDebuggerEnabled;
			BOOLEAN KernelDebuggerNotPresent;
		} info{};

		if ( nt_query_system_information(
				SystemKernelDebuggerInformation,
				&info, sizeof( info ), nullptr ) < 0 )
			return false;
		return info.KernelDebuggerEnabled && !info.KernelDebuggerNotPresent;
	}

	// SystemExtendedHandleInformation returns every open handle on the
	// system. We scan for handles opened to *our process* by any other
	// process — the fingerprint of a memory scanner / debugger / dumper
	// (Cheat Engine, x64dbg attached as reader, Scylla, PE-sieve, ReClass,
	// Process Hacker). Real anti-cheats do exactly this.
	//
	// Runtime cost: system-wide handle enum is heavy (100k+ entries on a
	// busy box). Callers throttle to ~30 s in the watchdog.
	//
	// Filter: only handles whose GrantedAccess includes bits that only
	// mean anything on a Process object and only matter for tampering
	// (VM_READ, VM_WRITE, VM_OPERATION, CREATE_THREAD). Skips 99% of
	// non-process handles for free.
	struct handle_entry {
		PVOID     Object;
		ULONG_PTR UniqueProcessId;
		ULONG_PTR HandleValue;
		ULONG     GrantedAccess;
		USHORT    CreatorBackTraceIndex;
		USHORT    ObjectTypeIndex;
		ULONG     HandleAttributes;
		ULONG     Reserved;
	};
	struct handle_info_ex {
		ULONG_PTR    NumberOfHandles;
		ULONG_PTR    Reserved;
		handle_entry Handles[ 1 ];
	};

	bool check_foreign_process_handles( ) {
		constexpr ULONG interesting_access =
			PROCESS_VM_READ | PROCESS_VM_WRITE |
			PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD;

		// Grow the buffer until the syscall stops complaining. Start
		// generous so the common case is one syscall.
		ULONG len = 4 * 1024 * 1024;
		void* buf = nullptr;
		for ( int attempt = 0; attempt < 6; ++attempt ) {
			buf = VirtualAlloc( nullptr, len,
				MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
			if ( !buf )
				return false;

			ULONG ret_len = 0;
			LONG status = nt_query_system_information(
				SystemExtendedHandleInformation,
				buf, len, &ret_len );

			if ( status >= 0 )
				break;

			// STATUS_INFO_LENGTH_MISMATCH = 0xC0000004. Grow and retry.
			VirtualFree( buf, 0, MEM_RELEASE );
			buf = nullptr;

			if ( static_cast<ULONG>( status ) != 0xC0000004u )
				return false;

			len = ret_len ? ( ret_len + ret_len / 2 ) : ( len * 2 );
			if ( len > 128 * 1024 * 1024 )   // safety cap
				return false;
		}
		if ( !buf )
			return false;

		auto* info = static_cast<handle_info_ex*>( buf );
		DWORD our_pid = GetCurrentProcessId( );
		bool hit = false;

		for ( ULONG_PTR i = 0; i < info->NumberOfHandles && !hit; ++i ) {
			const auto& h = info->Handles[ i ];

			if ( h.UniqueProcessId == our_pid )
				continue;
			if ( h.UniqueProcessId == 0 || h.UniqueProcessId == 4 )
				continue;                          // System / idle
			if ( ( h.GrantedAccess & interesting_access ) == 0 )
				continue;

			HANDLE src = OpenProcess(
				PROCESS_DUP_HANDLE, FALSE,
				static_cast<DWORD>( h.UniqueProcessId ) );
			if ( !src )
				continue;

			HANDLE dup = nullptr;
			BOOL ok = DuplicateHandle(
				src,
				reinterpret_cast<HANDLE>( h.HandleValue ),
				GetCurrentProcess( ),
				&dup,
				PROCESS_QUERY_LIMITED_INFORMATION,
				FALSE, 0 );
			CloseHandle( src );

			if ( !ok || !dup )
				continue;

			DWORD target = GetProcessId( dup );
			CloseHandle( dup );

			if ( target == our_pid )
				hit = true;
		}

		VirtualFree( buf, 0, MEM_RELEASE );
		return hit;
	}

	// Small deterministic RDTSC/QPC loop. Software breakpoints or active
	// single-step blow the elapsed well past the nominal <1ms, so a stall
	// past 100ms is a signal. Generous threshold because we insta-kill on
	// any single hit.
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

	// ------------------------------------------------------------ anti-VM

	// Read a 12-byte hypervisor vendor string from CPUID leaf 0x40000000,
	// EBX:ECX:EDX. Only meaningful when leaf 1 ECX bit 31 (hypervisor
	// present) is set — otherwise this leaf is unspecified.
	void read_hv_vendor( char out[ 13 ] ) {
		int regs[ 4 ]{};
		__cpuid( regs, 0x40000000 );
		std::memcpy( out + 0, &regs[ 1 ], 4 );   // EBX
		std::memcpy( out + 4, &regs[ 2 ], 4 );   // ECX
		std::memcpy( out + 8, &regs[ 3 ], 4 );   // EDX
		out[ 12 ] = '\0';
	}

	bool check_vm( ) {
		int regs[ 4 ]{};
		__cpuid( regs, 1 );
		bool hv_bit = ( static_cast<std::uint32_t>( regs[ 2 ] ) & ( 1u << 31 ) ) != 0;
		if ( !hv_bit )
			return false;

		// The HV bit being set is NOT a reliable "you're in a VM" signal
		// on modern Windows: VBS, Memory Integrity, WSL2, Windows Sandbox,
		// or just the Hyper-V role being installed all report as
		// "Microsoft Hv" on bare metal. Nuking those users would be a
		// serious false positive. Only match hypervisors that in practice
		// only host guests — never a bare-metal desktop signature.
		char vendor[ 13 ]{};
		read_hv_vendor( vendor );

		static const char* const desktop_vm_vendors[] = {
			"VMwareVMware",   // VMware Workstation / Fusion / ESXi
			"VBoxVBoxVBox",   // VirtualBox
			"KVMKVMKVM\0\0\0",// KVM (padded to 12 chars in the register triple)
			"TCGTCGTCGTCG",   // QEMU without KVM acceleration
			"XenVMMXenVMM",   // Xen HVM
			"prl hyperv  ",   // Parallels Desktop
			"bhyve bhyve ",   // FreeBSD bhyve
			"ACRNACRNACRN",   // ACRN
		};

		for ( auto* v : desktop_vm_vendors ) {
			if ( std::memcmp( vendor, v, 12 ) == 0 )
				return true;
		}
		return false;
	}

	// ------------------------------------------------------------ anti-dump

	// Zero the DOS header, NT headers, and section table of *our own*
	// mapped module. The OS loader is done reading them by the time we
	// run, so this is safe — exports and TLS were resolved at load time
	// and the LDR entry keeps the loader satisfied. What it breaks:
	//   * Scylla / PE-sieve / Process Hacker "dump module" — they scan
	//     process memory for "MZ" and can no longer find our base.
	//   * A dumper that finds us via the LDR list gets an image with no
	//     section table, unreconstructable without heavy manual work
	//     (where does .text end, where does .rdata start?).
	// Doesn't stop kernel-mode dumping or an attacker who snapshotted the
	// file on disk before injection. Cheap layer, not a silver bullet.
	//
	// Compiled in only when DEV is not defined. DEV builds keep the
	// minidump/diagnostics path — see diag::record_crash — which reads
	// headers as part of dump generation.
#if !defined( DEV )
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

		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>( base + dos->e_lfanew );
		if ( nt->Signature != IMAGE_NT_SIGNATURE )
			return;

		// Wipe range: start of image through end of section table. Covers
		// DOS stub, rich header, NT headers, data directories, and every
		// IMAGE_SECTION_HEADER entry.
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
#endif

	// ------------------------------------------------------------- heartbeat

	// Bidirectional heartbeat. The watchdog thread bumps its own TSC into
	// g_watchdog_beat and reads g_main_beat; the main thread does the
	// mirror via tick(). If either stops moving for longer than the
	// staleness threshold, the observer terminates the process — so
	// suspending only the watchdog doesn't disable protection.
	//
	// Timestamps are raw RDTSC. TSC frequency varies by CPU (typically
	// 2–4 GHz on x64), so the "cycles per second" estimate below is a
	// rough upper bound intended to over-approximate the number of
	// cycles in the threshold. Being early is fine; being late is the
	// failure mode we care about.
	constexpr std::uint64_t cycles_per_second_upper = 6ull * 1000ull * 1000ull * 1000ull;

	std::atomic<std::uint64_t> g_watchdog_beat{ 0 };
	std::atomic<std::uint64_t> g_main_beat{ 0 };

	// ---------------------------------------------------------- watchdog

	std::uint32_t xorshift32( std::uint32_t& s ) {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}

	DWORD WINAPI watchdog( LPVOID ) {
		// Hide this thread from an attached debugger via direct syscall
		// (bypasses any usermode ntdll hook, including ScyllaHide).
		nt_set_information_thread(
			THREAD_HIDE_FROM_DEBUGGER, nullptr, 0 );

		// One-shot VM check on startup. VM state doesn't change at runtime
		// and repeatedly hammering CPUID would just add a fingerprint.
		bool vm_hit = check_vm( );

		// Handle scan is O(all handles on the system) — much too heavy
		// to run every 6–25 ms. Throttle to ~30 s.
		std::uint64_t last_handle_scan = 0;
		constexpr std::uint64_t handle_scan_interval =
			30ull * cycles_per_second_upper;

		std::uint32_t seed = static_cast<std::uint32_t>( __rdtsc( ) ) | 1u;
		diag::write( diag::level::info, "anti_debug: watchdog loop starting" );
		for ( ;; ) {
			std::uint64_t now = __rdtsc( );
			g_watchdog_beat.store( now, std::memory_order_relaxed );

			// Run the cheap checks every pass, never short-circuiting, so
			// the number of ticks / order of execution doesn't fingerprint
			// which check fired.
			// TEMP: named instead of OR'd blind so we can log the culprit.
			const bool r_vm = vm_hit;
			const bool r_debug_port = check_debug_port( );
			const bool r_debug_object = check_debug_object( );
			const bool r_debug_flags = check_debug_flags( );
			const bool r_kernel_debugger = check_kernel_debugger( );
			const bool r_peb_flags = check_peb_flags( );
			const bool r_hwbp = check_hardware_breakpoints_all_threads( );
			const bool r_timing_stall = check_timing_stall( );

			bool hit = r_vm || r_debug_port || r_debug_object || r_debug_flags ||
				r_kernel_debugger || r_peb_flags || r_hwbp || r_timing_stall;

			// Handle scan runs on its own schedule.
			bool r_foreign_handle = false;
			if ( now - last_handle_scan > handle_scan_interval ) {
				last_handle_scan = now;
				r_foreign_handle = check_foreign_process_handles( );
				if ( r_foreign_handle )
					hit = true;
			}

			// Bidirectional-heartbeat side of the deal: only start
			// enforcing main-thread liveness once tick() has been called
			// at least once, so this doesn't nuke the process during
			// early init before hot paths are running.
			bool r_main_stall = false;
			std::uint64_t main_last = g_main_beat.load( std::memory_order_relaxed );
			if ( main_last != 0 ) {
				std::uint64_t elapsed = now - main_last;
				// 15 seconds — game/frame threads can stall on I/O, so
				// keep the threshold loose. Anything past this is an
				// attacker-suspended main thread.
				if ( elapsed > 15ull * cycles_per_second_upper ) {
					r_main_stall = true;
					hit = true;
				}
			}

			if ( hit ) {
				diag::writef(
					diag::level::fatal,
					"anti_debug: WATCHDOG KILL vm=%d debug_port=%d debug_object=%d "
					"debug_flags=%d kernel_debugger=%d peb_flags=%d hwbp=%d "
					"timing_stall=%d foreign_handle=%d main_stall=%d",
					r_vm ? 1 : 0, r_debug_port ? 1 : 0, r_debug_object ? 1 : 0,
					r_debug_flags ? 1 : 0, r_kernel_debugger ? 1 : 0,
					r_peb_flags ? 1 : 0, r_hwbp ? 1 : 0, r_timing_stall ? 1 : 0,
					r_foreign_handle ? 1 : 0, r_main_stall ? 1 : 0 );
				TerminateProcess( GetCurrentProcess( ), 0xC0000005 );
			}

			// Randomised 6–25 ms sleep — a fixed period is itself a
			// fingerprint.
			DWORD sleep_ms = 6 + ( xorshift32( seed ) % 20u );
			Sleep( sleep_ms );
		}
	}

	std::atomic<bool> g_initialized{ false };
	std::atomic<bool> g_ready{ false };

} // namespace

void initialize( ) {
	bool expected = false;
	if ( !g_initialized.compare_exchange_strong(
			expected, true, std::memory_order_acq_rel ) )
		return;

	if ( !resolve_syscalls( ) ) {
		diag::write( diag::level::warning, "anti_debug: resolve_syscalls failed, watchdog not started" );
		return;
	}

	HANDLE h = CreateThread( nullptr, 0, watchdog, nullptr, 0, nullptr );
	if ( !h ) {
		diag::write( diag::level::warning, "anti_debug: CreateThread failed, watchdog not started" );
		return;
	}
	diag::write( diag::level::info, "anti_debug: watchdog thread created" );
	CloseHandle( h );

	g_ready.store( true, std::memory_order_release );

#if !defined( DEV )
	// Do this last, after the watchdog is running and everything that
	// needed to read our headers at boot has already read them. In DEV
	// this is a no-op so minidump generation keeps working.
	erase_pe_headers( );
#endif
}

void tick( ) {
	if ( !g_ready.load( std::memory_order_acquire ) )
		return;

	std::uint64_t now = __rdtsc( );
	g_main_beat.store( now, std::memory_order_relaxed );

	// Cheap per-thread checks with no syscall cost — safe on hot paths.
	// The this-thread DR variant is deliberate here: HW BPs are per-thread
	// and this runs on whatever hot-path thread called tick().
	const bool r_hwbp_this = check_hardware_breakpoints_this_thread( );
	const bool r_peb = check_peb_flags( );
	if ( r_hwbp_this || r_peb ) {
		diag::writef(
			diag::level::fatal,
			"anti_debug: TICK KILL hwbp_this=%d peb_flags=%d",
			r_hwbp_this ? 1 : 0, r_peb ? 1 : 0 );
		TerminateProcess( GetCurrentProcess( ), 0xC0000005 );
	}

	// Enforce watchdog liveness. If it's been suspended, we terminate
	// here from the main thread. Tight threshold (2 seconds) — the
	// watchdog's own loop period is only 6–25 ms, so anything anywhere
	// near a second of silence is an attack, not scheduling jitter.
	std::uint64_t wd_last = g_watchdog_beat.load( std::memory_order_relaxed );
	if ( wd_last != 0 ) {
		std::uint64_t elapsed = now - wd_last;
		if ( elapsed > 2ull * cycles_per_second_upper ) {
			diag::writef(
				diag::level::fatal,
				"anti_debug: TICK KILL watchdog_stale elapsed_cycles=%llu",
				static_cast<unsigned long long>( elapsed ) );
			TerminateProcess( GetCurrentProcess( ), 0xC0000005 );
		}
	}
}

} // namespace anti_debug
