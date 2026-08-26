#include "nt_types.hpp"
#include <intrin.h>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "../protection/syscalls.hpp"
#include "../obfuscation.hpp"
#include "steam.hpp"

namespace steam
{

static uint64_t rng_state = 0;
static wchar_t  g_orig_guid[64]{};
static wchar_t  g_orig_macs[16][32]{};
static int      g_orig_mac_count = 0;
static wchar_t  g_orig_computer_name[64]{};

static uint64_t next_rand()
{
    if ( !rng_state ) rng_state = __rdtsc() ^ ( __rdtsc() << 17 );
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void init_us( UNICODE_STRING_NT& us, const wchar_t* str )
{
    us.Buffer        = const_cast<wchar_t*>( str );
    us.Length        = static_cast<USHORT>( wcslen(str) * sizeof(wchar_t) );
    us.MaximumLength = us.Length + sizeof(wchar_t);
}

static HANDLE open_reg_key( const wchar_t* path, ULONG access )
{
    UNICODE_STRING_NT    us{};
    OBJECT_ATTRIBUTES_NT oa{};
    init_us( us, path );
    INIT_OA( oa, &us, OBJ_CASE_INSENSITIVE );
    HANDLE h = nullptr;
    syscalls::open_key( &h, access, &oa );
    return h;
}

static void write_reg_str( HANDLE key, const wchar_t* value_name, const wchar_t* data )
{
    UNICODE_STRING_NT vn{};
    init_us( vn, value_name );
    syscalls::set_value_key( key, &vn, 0, REG_SZ, data,
        static_cast<ULONG>( ( wcslen(data) + 1 ) * sizeof(wchar_t) ) );
}

static bool read_reg_str( HANDLE key, const wchar_t* value_name, wchar_t* out, ULONG out_chars )
{
    UNICODE_STRING_NT vn{};
    init_us( vn, value_name );
    uint8_t buf[1024]{};
    ULONG   len = 0;
    if ( syscalls::query_value_key( key, &vn, 2, buf, sizeof buf, &len ) < 0 ) return false;
    auto* kv = reinterpret_cast<KEY_VALUE_PARTIAL_INFO*>( buf );
    if ( kv->Type != REG_SZ || !kv->DataLength ) return false;
    const size_t copy = std::min( (size_t)(kv->DataLength / sizeof(wchar_t)), (size_t)(out_chars - 1) );
    memcpy( out, kv->Data, copy * sizeof(wchar_t) );
    out[copy] = L'\0';
    return true;
}

static void format_guid( wchar_t* out )
{
    static const wchar_t hex[] = L"0123456789abcdef";
    uint8_t bytes[16];
    uint64_t a = next_rand(), b = next_rand();
    memcpy( bytes,     &a, 8 );
    memcpy( bytes + 8, &b, 8 );
    bytes[6] = static_cast<uint8_t>( ( bytes[6] & 0x0F ) | 0x40 );
    bytes[8] = static_cast<uint8_t>( ( bytes[8] & 0x3F ) | 0x80 );
    const int groups[] = { 4, 2, 2, 2, 6 };
    int bi = 0;
    for ( int g = 0; g < 5; ++g )
    {
        for ( int j = 0; j < groups[g]; ++j, ++bi )
        {
            *out++ = hex[ bytes[bi] >> 4 ];
            *out++ = hex[ bytes[bi] & 0xF ];
        }
        if ( g < 4 ) *out++ = L'-';
    }
    *out = L'\0';
}

static void format_mac( wchar_t* out )
{
    static const wchar_t hex[] = L"0123456789ABCDEF";
    const uint64_t r = next_rand();
    auto* b = reinterpret_cast<const uint8_t*>( &r );
    for ( int i = 0; i < 6; ++i )
    {
        *out++ = hex[ b[i] >> 4 ];
        *out++ = hex[ b[i] & 0xF ];
    }
    *out = L'\0';
}

static bool kill_by_name( const wchar_t* target, int target_len )
{
    ULONG  sz  = 512 * 1024;
    void*  buf = nullptr;
    SIZE_T bsz = sz;
    syscalls::allocate_virtual_memory( reinterpret_cast<HANDLE>(-1), &buf, 0, &bsz,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if ( !buf ) return false;
    ULONG ret = 0;
    bool  any = false;
    if ( syscalls::query_system_information( 5, buf, sz, &ret ) >= 0 )
    {
        auto* e = reinterpret_cast<SYSTEM_PROCESS_INFO*>( buf );
        for ( ;; )
        {
            if ( e->ImageName.Buffer && e->ImageName.Length / 2 == static_cast<USHORT>( target_len ) )
            {
                bool match = true;
                for ( int i = 0; i < target_len && match; ++i )
                {
                    wchar_t ch = e->ImageName.Buffer[i];
                    if ( ch >= L'A' && ch <= L'Z' ) ch |= 32;
                    if ( ch != target[i] ) match = false;
                }
                if ( match )
                {
                    CLIENT_ID_NT         cid{ e->UniqueProcessId, nullptr };
                    OBJECT_ATTRIBUTES_NT oa{};
                    INIT_OA( oa, nullptr, 0 );
                    HANDLE h = nullptr;
                    if ( syscalls::open_process( &h, PROCESS_TERMINATE, &oa, &cid ) >= 0 && h )
                    {
                        syscalls::terminate_process( h, 0 );
                        syscalls::close( h );
                        any = true;
                    }
                }
            }
            if ( !e->NextEntryOffset ) break;
            e = reinterpret_cast<SYSTEM_PROCESS_INFO*>(
                reinterpret_cast<uint8_t*>( e ) + e->NextEntryOffset );
        }
    }
    SIZE_T bsz2 = bsz;
    syscalls::free_virtual_memory( reinterpret_cast<HANDLE>(-1), &buf, &bsz2, MEM_RELEASE );
    return any;
}

void kill_all()
{
    constexpr struct { const wchar_t* name; int len; } targets[] =
    {
        { L"steam.exe",              9  },
        { L"steamwebhelper.exe",     18 },
        { L"steamservice.exe",       16 },
        { L"gameoverlayrenderer.exe", 24 },
        { L"steamtours.exe",         14 },
    };
    for ( auto& t : targets ) kill_by_name( t.name, t.len );
    syscalls::sleep_ms( 1500 );
}

void spoof_machine_guid()
{
    HANDLE key = open_reg_key(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography",
        KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY );
    if ( !key ) return;

    if ( !g_orig_guid[0] )
        read_reg_str( key, L"MachineGuid", g_orig_guid, 64 );

    wchar_t guid[64]{};
    format_guid( guid );
    write_reg_str( key, L"MachineGuid", guid );
    syscalls::close( key );
}

void restore_machine_guid()
{
    if ( !g_orig_guid[0] ) return;
    HANDLE key = open_reg_key(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography",
        KEY_SET_VALUE | KEY_WOW64_64KEY );
    if ( !key ) return;
    write_reg_str( key, L"MachineGuid", g_orig_guid );
    syscalls::close( key );
    memset( g_orig_guid, 0, sizeof g_orig_guid );
}

void spoof_mac_addresses()
{
    constexpr wchar_t root[] =
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
        L"\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}";

    g_orig_mac_count = 0;
    for ( int i = 0; i < 16; ++i )
    {
        wchar_t subkey[300]{};
        int ri = 0;
        while ( root[ri] ) subkey[ri] = root[ri++];
        subkey[ri++] = L'\\';
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i / 1000 ) % 10 );
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i / 100  ) % 10 );
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i / 10   ) % 10 );
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i        ) % 10 );
        subkey[ri]   = L'\0';

        HANDLE key = open_reg_key( subkey, KEY_QUERY_VALUE | KEY_SET_VALUE );
        if ( !key ) continue;

        read_reg_str( key, L"NetworkAddress", g_orig_macs[i], 32 );
        g_orig_mac_count = i + 1;

        wchar_t mac[16]{};
        format_mac( mac );
        write_reg_str( key, L"NetworkAddress", mac );
        syscalls::close( key );
    }
}

void restore_mac_addresses()
{
    if ( !g_orig_mac_count ) return;
    constexpr wchar_t root[] =
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
        L"\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}";

    for ( int i = 0; i < g_orig_mac_count; ++i )
    {
        wchar_t subkey[300]{};
        int ri = 0;
        while ( root[ri] ) subkey[ri] = root[ri++];
        subkey[ri++] = L'\\';
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i / 1000 ) % 10 );
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i / 100  ) % 10 );
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i / 10   ) % 10 );
        subkey[ri++] = L'0' + static_cast<wchar_t>( ( i        ) % 10 );
        subkey[ri]   = L'\0';

        HANDLE key = open_reg_key( subkey, KEY_SET_VALUE );
        if ( !key ) continue;
        write_reg_str( key, L"NetworkAddress", g_orig_macs[i][0] ? g_orig_macs[i] : L"" );
        syscalls::close( key );
    }
    g_orig_mac_count = 0;
}

void spoof_steam_identity()
{
    HANDLE key = open_reg_key(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
        L"\\Control\\ComputerName\\ComputerName",
        KEY_QUERY_VALUE | KEY_SET_VALUE );
    if ( !key ) return;

    if ( !g_orig_computer_name[0] )
        read_reg_str( key, L"ComputerName", g_orig_computer_name, 64 );

    wchar_t fake[16] = L"DESKTOP-";
    const uint64_t rng = __rdtsc() ^ reinterpret_cast<uint64_t>( key );
    constexpr wchar_t chars[] = L"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for ( int i = 0; i < 7; ++i )
        fake[8 + i] = chars[ ( rng >> ( i * 5 ) ) & 31 ];
    fake[15] = L'\0';
    write_reg_str( key, L"ComputerName", fake );
    syscalls::close( key );
}

void restore_steam_identity()
{
    if ( !g_orig_computer_name[0] ) return;
    HANDLE key = open_reg_key(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
        L"\\Control\\ComputerName\\ComputerName",
        KEY_SET_VALUE );
    if ( !key ) return;
    write_reg_str( key, L"ComputerName", g_orig_computer_name );
    syscalls::close( key );
    memset( g_orig_computer_name, 0, sizeof g_orig_computer_name );
}

bool launch(const wchar_t* args)
{
    HANDLE steam_key = open_reg_key(
        L"\\Registry\\Machine\\SOFTWARE\\Valve\\Steam",
        KEY_QUERY_VALUE | KEY_WOW64_32KEY );
    if ( !steam_key ) return false;

    wchar_t steam_dir[512]{};
    const bool found = read_reg_str( steam_key, L"SteamPath", steam_dir, 512 );
    syscalls::close( steam_key );
    if ( !found ) return false;

    wchar_t exe_path[640]{};
    int di = 0;
    while ( steam_dir[di] ) { exe_path[di] = steam_dir[di]; ++di; }
    const wchar_t* suffix = L"\\steam.exe";
    int si = 0;
    while ( suffix[si] ) exe_path[di++] = suffix[si++];
    exe_path[di] = L'\0';

    wchar_t nt_path[700]{};
    const wchar_t* prefix = L"\\??\\";
    int pi = 0, ei = 0;
    while ( prefix[pi] ) nt_path[ei++] = prefix[pi++];
    int ti = 0;
    while ( exe_path[ti] ) nt_path[ei++] = exe_path[ti++];
    nt_path[ei] = L'\0';

    UNICODE_STRING_NT img_path{};
    init_us( img_path, nt_path );

    wchar_t cmd_line[1024]{};
    if (args && args[0]) {
        swprintf_s(cmd_line, L"\"%s\" %s", exe_path, args);
    } else {
        swprintf_s(cmd_line, L"\"%s\"", exe_path);
    }
    UNICODE_STRING_NT cmd_us{};
    init_us( cmd_us, cmd_line );

    using RtlCreateProcessParametersEx_t = NTSTATUS(NTAPI*)(
        PVOID*, UNICODE_STRING_NT*, UNICODE_STRING_NT*,
        UNICODE_STRING_NT*, UNICODE_STRING_NT*, PVOID,
        UNICODE_STRING_NT*, UNICODE_STRING_NT*,
        PVOID, PVOID, ULONG );
    using RtlDestroyProcessParameters_t = void(NTAPI*)( PVOID );
    struct SteamProcessInfo
    {
        ULONG  Length;
        HANDLE ProcessHandle;
        HANDLE ThreadHandle;
        uint8_t ClientId[16];
        uint8_t ImageInfo[0x40];
    };
    using RtlCreateUserProcess_t = NTSTATUS(NTAPI*)(
        UNICODE_STRING_NT*, ULONG, PVOID, PVOID, PVOID,
        HANDLE, BOOLEAN, HANDLE, HANDLE, SteamProcessInfo* );

    const uintptr_t ntdll = peb_import::find_module( MOD_HASH("ntdll.dll") );
    if ( !ntdll ) return false;

    const auto fn_cpp  = reinterpret_cast<RtlCreateProcessParametersEx_t>(
        peb_import::find_export( ntdll, FN_HASH("RtlCreateProcessParametersEx") ) );
    const auto fn_dpp  = reinterpret_cast<RtlDestroyProcessParameters_t>(
        peb_import::find_export( ntdll, FN_HASH("RtlDestroyProcessParameters") ) );
    const auto fn_cup  = reinterpret_cast<RtlCreateUserProcess_t>(
        peb_import::find_export( ntdll, FN_HASH("RtlCreateUserProcess") ) );
    if ( !fn_cpp || !fn_cup ) return false;

    UNICODE_STRING_NT img2{};
    init_us( img2, nt_path );

    PVOID params = nullptr;
    if ( fn_cpp( &params, &img2, nullptr, nullptr, &cmd_us,
                 nullptr, nullptr, nullptr, nullptr, nullptr, 1 ) < 0 )
        return false;

    SteamProcessInfo proc_info{};
    proc_info.Length = sizeof proc_info;
    const LONG st = fn_cup( &img_path, 0, params, nullptr, nullptr,
                             reinterpret_cast<HANDLE>(-1),
                             FALSE, nullptr, nullptr, &proc_info );
    if ( fn_dpp ) fn_dpp( params );
    if ( st < 0 ) return false;

    syscalls::resume_thread( proc_info.ThreadHandle );
    syscalls::close( proc_info.ThreadHandle );
    syscalls::close( proc_info.ProcessHandle );
    return true;
}

void patch_vac( HANDLE game )
{
    ULONG  sz  = 512 * 1024;
    void*  buf = nullptr;
    SIZE_T bsz = sz;
    syscalls::allocate_virtual_memory( reinterpret_cast<HANDLE>(-1), &buf, 0, &bsz,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if ( !buf ) return;

    auto rvm = [&]( uintptr_t addr, void* out, SIZE_T len ) -> bool
    {
        SIZE_T rd = 0;
        return syscalls::read_virtual_memory( game, reinterpret_cast<void*>( addr ),
                   out, len, &rd ) >= 0 && rd == len;
    };

    struct PBI { PVOID r1; PVOID peb; PVOID r2[2]; ULONG_PTR pid, ppid; } pbi{};
    syscalls::query_information_process( game, 0, &pbi, sizeof pbi, nullptr );
    if ( !pbi.peb ) goto done;

    {
        uintptr_t ldr = 0;
        if ( !rvm( reinterpret_cast<uintptr_t>( pbi.peb ) + 0x18, &ldr, 8 ) || !ldr ) goto done;

        const uintptr_t head = ldr + 0x20;
        uintptr_t cur = 0;
        if ( !rvm( head, &cur, 8 ) || !cur ) goto done;

        static const uint8_t nop_ret[] = { 0x48, 0x31, 0xC0, 0xC3 };

        for ( int n = 0; n < 512 && cur && cur != head; ++n )
        {
            uintptr_t name_ptr = 0, mod_base = 0;
            rvm( cur + 0x50, &name_ptr, 8 );
            rvm( cur + 0x20, &mod_base, 8 );

            if ( name_ptr && mod_base )
            {
                wchar_t name_buf[64]{};
                SIZE_T rd = 0;
                syscalls::read_virtual_memory( game, reinterpret_cast<void*>( name_ptr ),
                    name_buf, sizeof(name_buf) - 2, &rd );

                bool is_vac = false;
                for ( int ci = 0; name_buf[ci]; ++ci )
                {
                    wchar_t a = name_buf[ci], b = name_buf[ci+1], c = name_buf[ci+2];
                    if ( a >= L'A' && a <= L'Z' ) a |= 32;
                    if ( b >= L'A' && b <= L'Z' ) b |= 32;
                    if ( c >= L'A' && c <= L'Z' ) c |= 32;
                    if ( a == L'v' && b == L'a' && c == L'c' ) { is_vac = true; break; }
                }

                if ( is_vac )
                {
                    uint8_t dos_buf[0x40]{};
                    if ( rvm( mod_base, dos_buf, sizeof dos_buf ) )
                    {
                        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>( dos_buf );
                        if ( dos->e_magic == IMAGE_DOS_SIGNATURE )
                        {
                            uint8_t nt_buf[0x100]{};
                            if ( rvm( mod_base + dos->e_lfanew, nt_buf, sizeof nt_buf ) )
                            {
                                auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>( nt_buf );
                                const DWORD ep_rva = nt->OptionalHeader.AddressOfEntryPoint;
                                auto& exp = nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ];

                                if ( ep_rva )
                                {
                                    void*  va = reinterpret_cast<void*>( mod_base + ep_rva );
                                    SIZE_T ps = sizeof nop_ret;
                                    ULONG  op = 0;
                                    if ( syscalls::protect_virtual_memory( game, &va, &ps, PAGE_EXECUTE_READWRITE, &op ) >= 0 )
                                    {
                                        SIZE_T wr = 0;
                                        syscalls::write_virtual_memory( game, va, nop_ret, sizeof nop_ret, &wr );
                                        syscalls::protect_virtual_memory( game, &va, &ps, op, &op );
                                    }
                                }

                                if ( exp.VirtualAddress && exp.Size )
                                {
                                    uint8_t exp_buf[ sizeof(IMAGE_EXPORT_DIRECTORY) ]{}; 
                                    if ( rvm( mod_base + exp.VirtualAddress, exp_buf, sizeof exp_buf ) )
                                    {
                                        auto* dir = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>( exp_buf );
                                        const DWORD fn_count = std::min<DWORD>( dir->NumberOfFunctions, 256u );
                                        std::vector<DWORD> fn_rvas( fn_count );
                                        SIZE_T fn_rd = 0;
                                        syscalls::read_virtual_memory( game,
                                            reinterpret_cast<void*>( mod_base + dir->AddressOfFunctions ),
                                            fn_rvas.data(), fn_count * sizeof(DWORD), &fn_rd );
                                        for ( DWORD fi = 0; fi < fn_count; ++fi )
                                        {
                                            if ( !fn_rvas[fi] ) continue;
                                            void*  fva = reinterpret_cast<void*>( mod_base + fn_rvas[fi] );
                                            SIZE_T fz  = sizeof nop_ret;
                                            ULONG  fp  = 0;
                                            if ( syscalls::protect_virtual_memory( game, &fva, &fz, PAGE_EXECUTE_READWRITE, &fp ) >= 0 )
                                            {
                                                SIZE_T wr = 0;
                                                syscalls::write_virtual_memory( game, fva, nop_ret, sizeof nop_ret, &wr );
                                                syscalls::protect_virtual_memory( game, &fva, &fz, fp, &fp );
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if ( !rvm( cur, &cur, 8 ) ) break;
        }
    }

done:
    SIZE_T bsz2 = bsz;
    syscalls::free_virtual_memory( reinterpret_cast<HANDLE>(-1), &buf, &bsz2, MEM_RELEASE );
}

void wipe_steam_machine_id()
{
    HANDLE key = open_reg_key(
        L"\\Registry\\Machine\\SOFTWARE\\Valve\\Steam",
        KEY_SET_VALUE | KEY_WOW64_32KEY );
    if ( !key ) return;
    ULONG fake_id = static_cast<ULONG>( next_rand() & 0xFFFFFFFF );
    UNICODE_STRING_NT vn{};
    init_us( vn, L"machineID" );
    syscalls::set_value_key( key, &vn, 0, REG_DWORD, &fake_id, sizeof fake_id );
    syscalls::close( key );
}

} // namespace steam
