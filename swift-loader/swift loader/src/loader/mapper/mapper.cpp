#include "nt_types.hpp"
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include "../protection/syscalls.hpp"
#include "../obfuscation.hpp"
#include "mapper.hpp"

namespace mapper
{

static bool apply_relocs( uint8_t* img, uintptr_t preferred, uintptr_t actual )
{
    const ptrdiff_t delta = static_cast<ptrdiff_t>( actual - preferred );
    if ( !delta ) return true;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>( img );
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>( img + dos->e_lfanew );
    auto& rd  = nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_BASERELOC ];
    if ( !rd.VirtualAddress ) return true;

    auto* blk = reinterpret_cast<IMAGE_BASE_RELOCATION*>( img + rd.VirtualAddress );
    while ( blk->VirtualAddress )
    {
        const DWORD count = ( blk->SizeOfBlock - sizeof(*blk) ) / sizeof(WORD);
        auto* entries     = reinterpret_cast<WORD*>( blk + 1 );
        for ( DWORD i = 0; i < count; ++i )
        {
            const int  type   = entries[i] >> 12;
            const WORD offset = entries[i] & 0xFFF;
            if ( type == IMAGE_REL_BASED_DIR64 )
                *reinterpret_cast<uint64_t*>( img + blk->VirtualAddress + offset ) += delta;
            else if ( type == IMAGE_REL_BASED_HIGHLOW )
                *reinterpret_cast<uint32_t*>( img + blk->VirtualAddress + offset ) += static_cast<uint32_t>( delta );
        }
        blk = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
            reinterpret_cast<uint8_t*>( blk ) + blk->SizeOfBlock );
    }
    return true;
}

static bool resolve_imports( uint8_t* img )
{
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>( img );
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>( img + dos->e_lfanew );
    auto& id  = nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ];
    if ( !id.VirtualAddress ) return true;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>( img + id.VirtualAddress );
    for ( ; desc->Name; ++desc )
    {
        const char* mod_name = reinterpret_cast<const char*>( img + desc->Name );
        wchar_t wname[64]{};
        for ( int i = 0; mod_name[i] && i < 63; ++i )
            wname[i] = static_cast<wchar_t>( mod_name[i] );

        uintptr_t mod = peb_import::find_module( peb_import::fnv1a_w( wname ) );
        if ( !mod )
        {
            using LdrLoadDll_t = NTSTATUS(NTAPI*)( PWSTR, ULONG*, UNICODE_STRING_NT*, HMODULE* );
            const auto ldr = peb_import::get<LdrLoadDll_t>( MOD_HASH("ntdll.dll"), FN_HASH("LdrLoadDll") );
            if ( ldr )
            {
                UNICODE_STRING_NT us{
                    static_cast<USHORT>( wcslen(wname) * 2 ),
                    static_cast<USHORT>( wcslen(wname) * 2 + 2 ),
                    wname };
                HMODULE h = nullptr;
                ldr( nullptr, nullptr, &us, &h );
                mod = reinterpret_cast<uintptr_t>( h );
            }
        }
        if ( !mod ) continue;

        auto* orig = reinterpret_cast<IMAGE_THUNK_DATA*>( img + desc->OriginalFirstThunk );
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>( img + desc->FirstThunk );

        for ( ; orig->u1.AddressOfData; ++orig, ++thunk )
        {
            if ( orig->u1.Ordinal & IMAGE_ORDINAL_FLAG )
            {
                auto* mdos = reinterpret_cast<IMAGE_DOS_HEADER*>( mod );
                auto* mnt  = reinterpret_cast<IMAGE_NT_HEADERS*>( mod + mdos->e_lfanew );
                auto& mex  = mnt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ];
                if ( !mex.VirtualAddress ) continue;
                auto* dir  = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>( mod + mex.VirtualAddress );
                auto* fns  = reinterpret_cast<DWORD*>( mod + dir->AddressOfFunctions );
                const DWORD ord = IMAGE_ORDINAL( orig->u1.Ordinal ) - dir->Base;
                thunk->u1.Function = ( ord < dir->NumberOfFunctions ) ? mod + fns[ord] : 0;
            }
            else
            {
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>( img + orig->u1.AddressOfData );
                thunk->u1.Function = peb_import::find_export( mod, peb_import::fnv1a( ibn->Name ) );
            }
        }
    }
    return true;
}

static void set_section_perms( HANDLE proc, uint8_t* base, const IMAGE_NT_HEADERS* nt )
{
    const IMAGE_SECTION_HEADER* sec = nt_first_section( nt );
    for ( WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec )
    {
        if ( !sec->VirtualAddress ) continue;
        const bool exec  = ( sec->Characteristics & IMAGE_SCN_MEM_EXECUTE ) != 0;
        const bool read  = ( sec->Characteristics & IMAGE_SCN_MEM_READ    ) != 0;
        const bool write = ( sec->Characteristics & IMAGE_SCN_MEM_WRITE   ) != 0;
        ULONG prot = PAGE_NOACCESS;
        if      ( exec && write ) prot = PAGE_EXECUTE_READWRITE;
        else if ( exec && read  ) prot = PAGE_EXECUTE_READ;
        else if ( exec          ) prot = PAGE_EXECUTE;
        else if ( write         ) prot = PAGE_READWRITE;
        else if ( read          ) prot = PAGE_READONLY;
        void*  va   = base + sec->VirtualAddress;
        SIZE_T size = sec->Misc.VirtualSize;
        ULONG  old  = 0;
        syscalls::protect_virtual_memory( proc, &va, &size, prot, &old );
    }
}

static bool call_remote_entry( HANDLE proc, uintptr_t base, DWORD ep_rva )
{
    if ( !ep_rva ) return false;

    // Shellcode: call entry( base, DLL_PROCESS_ATTACH, 0 )
    // x64 calling convention: RCX=arg1, RDX=arg2, R8=arg3
    //   48 B9 [8 bytes]  = mov rcx, base
    //   BA 01 00 00 00   = mov edx, 1  (DLL_PROCESS_ATTACH)
    //   45 33 C0         = xor r8d, r8d
    //   48 B8 [8 bytes]  = mov rax, entry_addr
    //   FF D0            = call rax
    //   C3               = ret
    uint8_t stub[32]{};
    const uintptr_t entry_addr = base + ep_rva;

    stub[0]  = 0x48; stub[1]  = 0xB9;
    memcpy( &stub[2], &base, 8 );
    stub[10] = 0xBA;
    stub[11] = 0x01; stub[12] = 0x00; stub[13] = 0x00; stub[14] = 0x00;
    stub[15] = 0x45; stub[16] = 0x33; stub[17] = 0xC0;
    stub[18] = 0x48; stub[19] = 0xB8;
    memcpy( &stub[20], &entry_addr, 8 );
    stub[28] = 0xFF; stub[29] = 0xD0;
    stub[30] = 0xC3;

    void*  stub_mem = nullptr;
    SIZE_T stub_sz  = sizeof stub;
    if ( syscalls::allocate_virtual_memory( proc, &stub_mem, 0, &stub_sz,
             MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) < 0 || !stub_mem )
        return false;

    SIZE_T wr = 0;
    syscalls::write_virtual_memory( proc, stub_mem, stub, sizeof stub, &wr );

    HANDLE               th = nullptr;
    OBJECT_ATTRIBUTES_NT oa{};
    INIT_OA( oa, nullptr, 0 );
    syscalls::create_thread_ex(
        &th, THREAD_ALL_ACCESS, &oa, proc,
        stub_mem, nullptr,
        0, 0, 0, 0, nullptr );

    if ( th )
    {
        syscalls::close( th );
        return true;
    }
    return false;
}

uintptr_t map_remote( HANDLE proc, const std::vector<uint8_t>& pe )
{
    if ( pe.size() < sizeof(IMAGE_DOS_HEADER) ) return 0;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>( pe.data() );
    if ( dos->e_magic != IMAGE_DOS_SIGNATURE ) return 0;
    auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>( pe.data() + dos->e_lfanew );
    if ( nt->Signature != IMAGE_NT_SIGNATURE ) return 0;

    const auto& opt = nt->OptionalHeader;
    void*  remote_base = nullptr;
    SIZE_T image_size  = opt.SizeOfImage;
    if ( syscalls::allocate_virtual_memory( proc, &remote_base, 0, &image_size,
             MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) < 0 || !remote_base )
        return 0;

    std::vector<uint8_t> local_img( opt.SizeOfImage, 0 );
    memcpy( local_img.data(), pe.data(), opt.SizeOfHeaders );

    const IMAGE_SECTION_HEADER* sec = nt_first_section( nt );
    for ( WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec )
    {
        if ( !sec->SizeOfRawData ) continue;
        const SIZE_T csz = std::min( sec->SizeOfRawData,
            static_cast<DWORD>( pe.size() - sec->PointerToRawData ) );
        memcpy( local_img.data() + sec->VirtualAddress,
                pe.data() + sec->PointerToRawData, csz );
    }

    apply_relocs( local_img.data(), opt.ImageBase,
        reinterpret_cast<uintptr_t>( remote_base ) );
    resolve_imports( local_img.data() );

    SIZE_T written = 0;
    if ( syscalls::write_virtual_memory( proc, remote_base,
             local_img.data(), local_img.size(), &written ) < 0 )
        return 0;

    auto* remote_nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        local_img.data() + dos->e_lfanew );
    set_section_perms( proc, reinterpret_cast<uint8_t*>( remote_base ), remote_nt );

    const DWORD ep_rva = remote_nt->OptionalHeader.AddressOfEntryPoint;
    call_remote_entry( proc, reinterpret_cast<uintptr_t>( remote_base ), ep_rva );

    return reinterpret_cast<uintptr_t>( remote_base );
}

void wipe_headers( HANDLE proc, uintptr_t base )
{
    if ( !base ) return;
    void*  addr = reinterpret_cast<void*>( base );
    SIZE_T size = 0x1000;
    ULONG  old  = 0;
    syscalls::protect_virtual_memory( proc, &addr, &size, PAGE_READWRITE, &old );
    std::vector<uint8_t> zeros( 0x1000, 0 );
    SIZE_T wr = 0;
    syscalls::write_virtual_memory( proc, addr, zeros.data(), 0x1000, &wr );
    syscalls::protect_virtual_memory( proc, &addr, &size, PAGE_NOACCESS, &old );
}

uintptr_t map_local( const std::vector<uint8_t>& pe )
{
    if ( pe.size() < sizeof(IMAGE_DOS_HEADER) ) return 0;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>( pe.data() );
    if ( dos->e_magic != IMAGE_DOS_SIGNATURE ) return 0;
    auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>( pe.data() + dos->e_lfanew );
    if ( nt->Signature != IMAGE_NT_SIGNATURE ) return 0;

    const auto& opt = nt->OptionalHeader;
    const HANDLE self = reinterpret_cast<HANDLE>(-1);
    void*  base = nullptr;
    SIZE_T isz  = opt.SizeOfImage;
    if ( syscalls::allocate_virtual_memory( self, &base, 0, &isz,
             MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) < 0 || !base )
        return 0;

    auto* img = reinterpret_cast<uint8_t*>( base );
    memcpy( img, pe.data(), opt.SizeOfHeaders );

    const IMAGE_SECTION_HEADER* sec = nt_first_section( nt );
    for ( WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec )
    {
        if ( !sec->SizeOfRawData ) continue;
        const SIZE_T csz = std::min( sec->SizeOfRawData,
            static_cast<DWORD>( pe.size() - sec->PointerToRawData ) );
        memcpy( img + sec->VirtualAddress, pe.data() + sec->PointerToRawData, csz );
    }

    apply_relocs( img, opt.ImageBase, reinterpret_cast<uintptr_t>( img ) );
    resolve_imports( img );
    set_section_perms( self, img,
        reinterpret_cast<const IMAGE_NT_HEADERS*>( img + dos->e_lfanew ) );
    return reinterpret_cast<uintptr_t>( img );
}

}
