#include "integrity.hpp"
#include "syscalls.hpp"
#include "../obfuscation.hpp"
#include "../crypto/crypto.hpp"
#include "nt_types.hpp"
#include <intrin.h>
#include <cstring>

namespace integrity
{

static uint64_t    s_scramble_key = 0;
static uint8_t     s_stored[32]{};
static uint8_t*    s_text_base  = nullptr;
static size_t      s_text_size  = 0;
static bool        s_ready      = false;

static bool find_text_section(uint8_t*& base, size_t& size)
{

    uintptr_t img = *reinterpret_cast<uintptr_t*>(__readgsqword(0x60) + 0x10);
    if (!img) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(img);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(img + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
    {

        if (memcmp(sec->Name, ".text", 5) == 0)
        {
            base = reinterpret_cast<uint8_t*>(img + sec->VirtualAddress);
            size = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

static std::array<uint8_t, 32> hash_range(const uint8_t* data, size_t len)
{
    return crypto::sha256(data, len);
}

static void scramble(const uint8_t* in, uint8_t* out, uint64_t key)
{
    for (int i = 0; i < 32; ++i)
    {
        out[i] = in[i] ^ static_cast<uint8_t>((key >> ((i % 8) * 8)) & 0xFF);
    }
}

static bool check_hook_patches()
{
    uintptr_t img = *reinterpret_cast<uintptr_t*>(__readgsqword(0x60) + 0x10);
    if (!img) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(img);
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(img + dos->e_lfanew);




    auto& exp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exp_dir.VirtualAddress && exp_dir.Size)
    {
        auto* exp  = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(img + exp_dir.VirtualAddress);
        auto* fns  = reinterpret_cast<DWORD*>(img + exp->AddressOfFunctions);

        for (DWORD i = 0; i < exp->NumberOfFunctions; ++i)
        {
            if (!fns[i]) continue;
            auto* fn = reinterpret_cast<const uint8_t*>(img + fns[i]);

            if (fn[0] == 0xCC)              return true;
            if (fn[0] == 0xE9)              return true;
            if (fn[0] == 0xFF && fn[1]==0x25) return true;
        }
    }


    if (s_text_base && s_text_size)
    {
        int consecutive_cc = 0;
        for (size_t i = 0; i < s_text_size; ++i)
        {
            if (s_text_base[i] == 0xCC)
            {
                if (++consecutive_cc >= 2) return true;
            }
            else
            {
                consecutive_cc = 0;
            }
        }
    }
    return false;
}

static bool check_iat_integrity()
{
    uintptr_t img = *reinterpret_cast<uintptr_t*>(__readgsqword(0x60) + 0x10);
    if (!img) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(img);
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(img + dos->e_lfanew);
    auto& imp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imp_dir.VirtualAddress) return false;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(img + imp_dir.VirtualAddress);
    for (; desc->Name; ++desc)
    {

        const char* mod_name = reinterpret_cast<const char*>(img + desc->Name);
        wchar_t wname[64]{};
        for (int i = 0; mod_name[i] && i < 63; ++i)
            wname[i] = static_cast<wchar_t>(mod_name[i]);

        uintptr_t mod_base = peb_import::find_module(peb_import::fnv1a_w(wname));
        if (!mod_base) continue;


        auto* mdos = reinterpret_cast<IMAGE_DOS_HEADER*>(mod_base);
        auto* mnt  = reinterpret_cast<IMAGE_NT_HEADERS*>(mod_base + mdos->e_lfanew);
        uintptr_t mod_end = mod_base + mnt->OptionalHeader.SizeOfImage;


        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(img + desc->FirstThunk);
        for (; thunk->u1.Function; ++thunk)
        {
            uintptr_t fn = thunk->u1.Function;

            if (fn < mod_base || fn >= mod_end)
                return true;
        }
    }
    return false;
}

void snapshot()
{
    if (!find_text_section(s_text_base, s_text_size))
        return;


    auto h = hash_range(s_text_base, s_text_size);


    s_scramble_key = __rdtsc();
    scramble(h.data(), s_stored, s_scramble_key);

    s_ready = true;
}

bool check()
{
    if (s_ready && s_text_base && s_text_size)
    {

        auto current = hash_range(s_text_base, s_text_size);


        uint8_t stored_clear[32]{};
        scramble(s_stored, stored_clear, s_scramble_key);

        if (memcmp(current.data(), stored_clear, 32) != 0)
            return true;
    }

    if (check_hook_patches())
        return true;

    if (check_iat_integrity())
        return true;

    return false;
}

}
