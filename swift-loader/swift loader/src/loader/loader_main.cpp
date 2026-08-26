#include "nt_types.hpp"
#include <intrin.h>
#include <string>
#include <vector>

#include "config.hpp"
#include "obfuscation.hpp"
#include "protection/antidebug.hpp"
#include "protection/integrity.hpp"
#include "protection/syscalls.hpp"
#include "protection/hwid.hpp"
#include "crypto/crypto.hpp"
#include "network/network.hpp"
#include "mapper/mapper.hpp"
#include "steam/steam.hpp"
#include "loader_ui/loader_ui.hpp"

static constexpr uint8_t EXPECTED_HASH[32] = CFG_EXPECTED_HASH;

[[noreturn]] static void die()
{
    uintptr_t base = *reinterpret_cast<uintptr_t*>( __readgsqword( 0x60 ) + 0x10 );
    if ( base )
    {
        void*  b  = reinterpret_cast<void*>( base );
        SIZE_T sz = 0x1000;
        ULONG  old = 0;
        syscalls::protect_virtual_memory( reinterpret_cast<HANDLE>(-1), &b, &sz, PAGE_READWRITE, &old );
        memset( b, 0, 0x1000 );
    }
    syscalls::terminate_process( reinterpret_cast<HANDLE>(-1), syscalls::NT_SUCCESS_VAL );
    __assume(0);
}

static bool hash_ok( const std::vector<uint8_t>& data )
{
    for ( auto b : EXPECTED_HASH ) if ( b ) goto check;
    return true;
check:
    return memcmp( crypto::sha256( data.data(), data.size() ).data(), EXPECTED_HASH, 32 ) == 0;
}

static DWORD find_pid( const wchar_t* name, int name_len )
{
    ULONG  sz  = 512 * 1024;
    void*  buf = nullptr;
    SIZE_T bsz = sz;
    syscalls::allocate_virtual_memory( reinterpret_cast<HANDLE>(-1), &buf, 0, &bsz,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if ( !buf ) return 0;
    ULONG ret = 0;
    DWORD pid = 0;
    if ( syscalls::query_system_information( 5, buf, sz, &ret ) >= 0 )
    {
        auto* e = reinterpret_cast<SYSTEM_PROCESS_INFO*>( buf );
        for ( ;; )
        {
            if ( e->ImageName.Buffer && e->ImageName.Length / 2 == static_cast<USHORT>( name_len ) )
            {
                bool match = true;
                for ( int i = 0; i < name_len && match; ++i )
                {
                    wchar_t ch = e->ImageName.Buffer[i];
                    if ( ch >= L'A' && ch <= L'Z' ) ch |= 32;
                    if ( ch != name[i] ) match = false;
                }
                if ( match )
                {
                    pid = static_cast<DWORD>( reinterpret_cast<ULONG_PTR>( e->UniqueProcessId ) );
                    break;
                }
            }
            if ( !e->NextEntryOffset ) break;
            e = reinterpret_cast<SYSTEM_PROCESS_INFO*>(
                reinterpret_cast<uint8_t*>( e ) + e->NextEntryOffset );
        }
    }
    SIZE_T bsz2 = bsz;
    syscalls::free_virtual_memory( reinterpret_cast<HANDLE>(-1), &buf, &bsz2, MEM_RELEASE );
    return pid;
}

static HANDLE open_game( DWORD pid )
{
    CLIENT_ID_NT         cid{ reinterpret_cast<HANDLE>( static_cast<ULONG_PTR>( pid ) ), nullptr };
    OBJECT_ATTRIBUTES_NT oa{};
    INIT_OA( oa, nullptr, 0 );
    HANDLE h = nullptr;
    syscalls::open_process( &h, PROCESS_ALL_ACCESS, &oa, &cid );
    return h;
}

static bool game_has_module( HANDLE game, uint32_t name_hash )
{
    struct PBI { PVOID r1; PVOID peb; PVOID r2[2]; ULONG_PTR pid, ppid; } pbi{};
    syscalls::query_information_process( game, 0, &pbi, sizeof(pbi), nullptr );
    if ( !pbi.peb ) return false;
    auto rvm = [&]( uintptr_t addr, void* out, SIZE_T len ) -> bool
    {
        SIZE_T rd = 0;
        return syscalls::read_virtual_memory( game, reinterpret_cast<void*>( addr ),
            out, len, &rd ) >= 0 && rd == len;
    };
    uintptr_t ldr = 0;
    if ( !rvm( reinterpret_cast<uintptr_t>( pbi.peb ) + 0x18, &ldr, 8 ) || !ldr ) return false;
    const uintptr_t head = ldr + 0x20;
    uintptr_t cur = 0;
    if ( !rvm( head, &cur, 8 ) ) return false;
    for ( int n = 0; n < 512 && cur && cur != head; ++n )
    {
        uintptr_t name_ptr = 0;
        if ( rvm( cur + 0x50, &name_ptr, 8 ) && name_ptr )
        {
            wchar_t name_buf[64]{};
            SIZE_T  rd = 0;
            syscalls::read_virtual_memory( game, reinterpret_cast<void*>( name_ptr ),
                name_buf, sizeof(name_buf) - 2, &rd );
            if ( peb_import::fnv1a_w( name_buf ) == name_hash ) return true;
        }
        if ( !rvm( cur, &cur, 8 ) ) break;
    }
    return false;
}

static bool wait_module( HANDLE game, uint32_t hash, DWORD ms )
{
    for ( DWORD t = 0; t < ms; t += 500 )
    {
        if ( game_has_module( game, hash ) ) return true;
        syscalls::sleep_ms( 500 );
    }
    return false;
}

static bool on_login( const std::string& user, const std::string& pass, std::string& err )
{
#ifdef _DEBUG
    (void)user; (void)pass;
    return true;
#else
    (void)user; (void)pass;
    err = "Backend not configured";
    return false;
#endif
}

static bool on_inject( bool hwid_spoof, const std::string& token, const std::string& session_key_hex, std::string& status )
{
    status = "Collecting hardware ID";
    const std::string hw       = hwid::get();
    const std::string hw_short = hwid::get_short();

    status = "Killing Steam";
    steam::kill_all();
    syscalls::sleep_ms( 500 );

    if ( hwid_spoof )
    {
        status = "Spoofing hardware identity";
        steam::wipe_steam_machine_id();
        steam::spoof_machine_guid();
        steam::spoof_mac_addresses();
        steam::spoof_steam_identity();
    }

    status = "Launching Steam";
    steam::launch();

    // Steam reads MachineGuid and MAC addresses during its startup.
    // Give it 6 seconds to initialise and cache the spoofed values,
    // then restore the originals so nothing else in the system sees them.
    syscalls::sleep_ms( 6000 );
    if ( hwid_spoof )
    {
        steam::restore_machine_guid();
        steam::restore_mac_addresses();
        steam::restore_steam_identity();
    }

    status = "Patching VAC in Steam";
    {
        DWORD steam_pid = 0;
        for ( int i = 0; i < 20 && !steam_pid; ++i )
        {
            steam_pid = find_pid( L"steam.exe", 9 );
            syscalls::sleep_ms( 500 );
        }
        if ( steam_pid )
        {
            HANDLE hsteam = open_game( steam_pid );
            if ( hsteam )
            {
                steam::patch_vac( hsteam );
                syscalls::close( hsteam );
            }
        }
    }

#ifdef _DEBUG
    status = "Loading cheat from disk";
    std::vector<uint8_t> cheat_data;
    {
        auto* peb    = reinterpret_cast<uint8_t*>( __readgsqword( 0x60 ) );
        auto* params = *reinterpret_cast<uint8_t**>( peb + 0x20 );
        struct US { uint16_t len, max; wchar_t* buf; };
        auto& img = *reinterpret_cast<US*>( params + 0x60 );
        wchar_t dir[512]{};
        int n = img.len / 2;
        if ( n >= 512 ) n = 511;
        memcpy( dir, img.buf, n * 2 );
        dir[n] = 0;
        for ( int i = n - 1; i >= 0; --i )
            if ( dir[i] == L'\\' || dir[i] == L'/' ) { dir[i] = 0; break; }

        wchar_t path[600]{};
        int j = 0;
        const wchar_t* pfx = L"\\??\\";
        while ( pfx[j] ) path[j] = pfx[j++];
        int k = 0;
        while ( dir[k] ) path[j++] = dir[k++];
        const wchar_t* nm = L"\\swift.bin";
        k = 0;
        while ( nm[k] ) path[j++] = nm[k++];

        UNICODE_STRING_NT us{};
        us.Buffer        = path;
        us.Length        = static_cast<USHORT>( j * 2 );
        us.MaximumLength = us.Length + 2;
        OBJECT_ATTRIBUTES_NT oa2{};
        INIT_OA( oa2, &us, OBJ_CASE_INSENSITIVE );

        struct { LONG st; ULONG_PTR info; } isb{};
        HANDLE fh = nullptr;
        syscalls::open_file( &fh, GENERIC_READ | SYNCHRONIZE, &oa2, &isb,
            FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT );
        if ( fh )
        {
            const uintptr_t ntdll = peb_import::find_module( MOD_HASH("ntdll.dll") );
            using NtQIF_t = LONG(NTAPI*)( HANDLE, ISB*, void*, ULONG, ULONG );
            using NtRF_t  = LONG(NTAPI*)( HANDLE, HANDLE, PVOID, PVOID, ISB*, PVOID, ULONG, LARGE_INTEGER*, ULONG* );
            struct { LARGE_INTEGER alloc, eof; ULONG links; BOOLEAN del, dir; } fsi{};
            ISB isb2{};
            auto fn_qif = reinterpret_cast<NtQIF_t>( peb_import::find_export( ntdll, peb_import::fnv1a("NtQueryInformationFile") ) );
            if ( fn_qif ) fn_qif( fh, &isb2, &fsi, sizeof fsi, 5 );
            ULONG fsz = static_cast<ULONG>( fsi.eof.QuadPart );
            if ( fsz )
            {
                cheat_data.resize( fsz );
                ISB isb3{};
                auto fn_rf = reinterpret_cast<NtRF_t>( peb_import::find_export( ntdll, peb_import::fnv1a("NtReadFile") ) );
                if ( fn_rf ) fn_rf( fh, nullptr, nullptr, nullptr, &isb3, cheat_data.data(), fsz, nullptr, nullptr );
            }
            syscalls::close( fh );
        }
    }
    if ( cheat_data.empty() ) { status = "swift.bin not found next to swift.exe"; return false; }
#else
    status = "Downloading cheat";
    auto dl = network::download_payload( token, hw_short );
    if ( !dl.success || dl.encrypted_data.empty() ) { status = dl.error.empty() ? "Download failed" : dl.error; return false; }

    status = "Decrypting";
    std::vector<uint8_t> cheat_data = crypto::decrypt_session_payload( dl.encrypted_data, session_key_hex );
    if ( cheat_data.empty() ) { status = "Decryption failed"; return false; }
    if ( !hash_ok( cheat_data ) ) { status = "Hash mismatch"; return false; }
#endif

    DWORD ep_rva = 0;
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>( cheat_data.data() );
        const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>( cheat_data.data() + dos->e_lfanew );
        ep_rva          = nt->OptionalHeader.AddressOfEntryPoint;
    }

    status = "Waiting for CS2";
    DWORD pid = 0;
    for ( int i = 0; i < 720 && !pid; ++i )
    {
        pid = find_pid( L"cs2.exe", 7 );
        if ( !pid ) syscalls::sleep_ms( 500 );
    }
    if ( !pid ) { status = "CS2 not found"; return false; }

    HANDLE game = open_game( pid );
    if ( !game ) { status = "Failed to open CS2"; return false; }

    status = "Waiting for client.dll";
    if ( !wait_module( game, MOD_HASH("client.dll"), 120'000 ) )
    {
        syscalls::close( game );
        status = "client.dll not found";
        return false;
    }

    status = "Patching VAC in game";
    steam::patch_vac( game );
    syscalls::sleep_ms( 2000 );

    status = "Mapping cheat";
    const uintptr_t remote_base = mapper::map_remote( game, cheat_data );
    if ( !remote_base )
    {
        syscalls::close( game );
        status = "Remote map failed";
        return false;
    }

    mapper::wipe_headers( game, remote_base );
    crypto::secure_zero( cheat_data.data(), cheat_data.size() );
    cheat_data.clear();
    cheat_data.shrink_to_fit();

    status = "Creating game thread";
    HANDLE               rth = nullptr;
    OBJECT_ATTRIBUTES_NT toa{};
    INIT_OA( toa, nullptr, 0 );
    syscalls::create_thread_ex(
        &rth, THREAD_ALL_ACCESS, &toa, game,
        reinterpret_cast<void*>( remote_base + ep_rva ),
        reinterpret_cast<void*>( remote_base ),
        0, 0, 0, 0, nullptr );

    if ( rth ) syscalls::close( rth );
    syscalls::close( game );
    return true;
}

static DWORD WINAPI loader_thread( LPVOID )
{
#ifndef _DEBUG
    integrity::snapshot();
    if ( antidebug::check() ) die();
    if ( integrity::check() ) die();
    antidebug::start_watchdog();
#endif

    loader_ui::callbacks cb;
    cb.on_login  = on_login;
    cb.on_inject = []( bool hwid, const std::string& tk, const std::string& sk, std::string& st ) {
        return on_inject( hwid, tk, sk, st );
    };
    loader_ui::run( cb );

    while ( true ) syscalls::sleep_ms( 60'000 );
    return 0;
}

extern "C" __declspec(dllexport) BOOL WINAPI loader_entry( HINSTANCE, DWORD, LPVOID )
{
    if ( !syscalls::init() ) return FALSE;

    HANDLE th = nullptr;
    OBJECT_ATTRIBUTES_NT oa{};
    INIT_OA( oa, nullptr, 0 );
    syscalls::create_thread_ex(
        &th, THREAD_ALL_ACCESS, &oa, reinterpret_cast<HANDLE>(-1),
        reinterpret_cast<void*>( static_cast<DWORD(WINAPI*)(LPVOID)>( loader_thread ) ),
        nullptr, 0, 0, 0, 0, nullptr );

    if ( th ) syscalls::close( th );
    return TRUE;
}
