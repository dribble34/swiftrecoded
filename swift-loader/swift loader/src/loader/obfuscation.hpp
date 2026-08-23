#pragma once
#include "nt_types.hpp"
#include <cstdint>
#include <cstring>
#include <intrin.h>

#define OBF_KEY_BASE  0xA5B4C3D2E1F09182ULL

namespace obf_detail
{

    template<uint64_t Key, int N>
    struct EncStr
    {
        char data[N]{};

        constexpr explicit EncStr(const char (&src)[N])
        {
            for (int i = 0; i < N; ++i)
            {
                uint8_t k = static_cast<uint8_t>(
                    (Key >> ((i % 8) * 8)) & 0xFF);
                data[i] = src[i] ^ k;
            }
        }


        void decrypt(char* out) const
        {
            for (int i = 0; i < N; ++i)
            {
                uint8_t k = static_cast<uint8_t>(
                    (Key >> ((i % 8) * 8)) & 0xFF);
                out[i] = data[i] ^ k;
            }
        }
    };



    template<uint64_t Key, int N>
    struct DecHolder
    {
        char buf[N]{};

        explicit DecHolder(const EncStr<Key, N>& enc)
        {
            enc.decrypt(buf);
        }

        ~DecHolder()
        {

            for (int i = 0; i < N; ++i) buf[i] = 0;
        }

        const char* str() const { return buf; }
        operator const char*() const { return buf; }
    };
}

#define OBF(s) \
    []() -> ::obf_detail::DecHolder< \
        (OBF_KEY_BASE ^ (static_cast<uint64_t>(__COUNTER__) * 0x9E3779B97F4A7C15ULL)), \
        sizeof(s)> { \
        static constexpr auto enc = ::obf_detail::EncStr< \
            (OBF_KEY_BASE ^ (static_cast<uint64_t>(__COUNTER__-1) * 0x9E3779B97F4A7C15ULL)), \
            sizeof(s)>(s); \
        return ::obf_detail::DecHolder< \
            (OBF_KEY_BASE ^ (static_cast<uint64_t>(__COUNTER__-2) * 0x9E3779B97F4A7C15ULL)), \
            sizeof(s)>(enc); \
    }()

namespace obf_detail
{
    template<uint64_t Key, int N>
    struct EncWStr
    {
        wchar_t data[N]{};
        constexpr explicit EncWStr(const wchar_t (&src)[N])
        {
            for (int i = 0; i < N; ++i)
            {
                uint16_t k = static_cast<uint16_t>((Key >> ((i % 4) * 16)) & 0xFFFF);
                data[i] = static_cast<wchar_t>(src[i] ^ k);
            }
        }
        void decrypt(wchar_t* out) const
        {
            for (int i = 0; i < N; ++i)
            {
                uint16_t k = static_cast<uint16_t>((Key >> ((i % 4) * 16)) & 0xFFFF);
                out[i] = static_cast<wchar_t>(data[i] ^ k);
            }
        }
    };

    template<uint64_t Key, int N>
    struct DecWHolder
    {
        wchar_t buf[N]{};
        explicit DecWHolder(const EncWStr<Key, N>& enc) { enc.decrypt(buf); }
        ~DecWHolder() { for (int i = 0; i < N; ++i) buf[i] = 0; }
        const wchar_t* str() const { return buf; }
        operator const wchar_t*() const { return buf; }
    };
}

#define WOBF(s) \
    []() -> ::obf_detail::DecWHolder< \
        (OBF_KEY_BASE ^ (static_cast<uint64_t>(__COUNTER__) * 0x9E3779B97F4A7C15ULL)), \
        sizeof(s)/sizeof(wchar_t)> { \
        static constexpr auto enc = ::obf_detail::EncWStr< \
            (OBF_KEY_BASE ^ (static_cast<uint64_t>(__COUNTER__-1) * 0x9E3779B97F4A7C15ULL)), \
            sizeof(s)/sizeof(wchar_t)>(s); \
        return ::obf_detail::DecWHolder< \
            (OBF_KEY_BASE ^ (static_cast<uint64_t>(__COUNTER__-2) * 0x9E3779B97F4A7C15ULL)), \
            sizeof(s)/sizeof(wchar_t)>(enc); \
    }()

namespace peb_import
{

    constexpr uint32_t fnv1a(const char* s)
    {
        uint32_t h = 2166136261u;
        while (*s) { h ^= static_cast<uint8_t>(*s | 0x20); h *= 16777619u; ++s; }
        return h;
    }

    constexpr uint32_t fnv1a_w(const wchar_t* s)
    {
        uint32_t h = 2166136261u;
        while (*s) { h ^= static_cast<uint8_t>(*s & 0xFF) | 0x20; h *= 16777619u; ++s; }
        return h;
    }


    inline uintptr_t find_module(uint32_t name_hash)
    {
#ifdef _WIN64
        auto* peb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
        auto* ldr = *reinterpret_cast<uint8_t**>(peb + 0x18);
        auto* head = reinterpret_cast<uint8_t*>(ldr + 0x20);
#else
        auto* peb = reinterpret_cast<uint8_t*>(__readfsdword(0x30));
        auto* ldr = *reinterpret_cast<uint8_t**>(peb + 0x0C);
        auto* head = reinterpret_cast<uint8_t*>(ldr + 0x14);
#endif
        auto* cur  = *reinterpret_cast<uint8_t**>(head);
        while (cur && cur != head)
        {
#ifdef _WIN64
            auto* name_str = *reinterpret_cast<wchar_t**>(cur + 0x50);
            auto  base     = *reinterpret_cast<uintptr_t*>(cur + 0x20);
#else
            auto* name_str = *reinterpret_cast<wchar_t**>(cur + 0x28);
            auto  base     = *reinterpret_cast<uintptr_t*>(cur + 0x10);
#endif
            if (name_str && base)
            {
                if (fnv1a_w(name_str) == name_hash)
                    return base;
            }
            cur = *reinterpret_cast<uint8_t**>(cur);
        }
        return 0;
    }


    inline uintptr_t find_export(uintptr_t base, uint32_t fn_hash)
    {
        if (!base) return 0;
        auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        auto& exp  = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!exp.VirtualAddress) return 0;

        auto* dir  = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + exp.VirtualAddress);
        auto* names= reinterpret_cast<DWORD*>(base + dir->AddressOfNames);
        auto* ords = reinterpret_cast<WORD*> (base + dir->AddressOfNameOrdinals);
        auto* fns  = reinterpret_cast<DWORD*>(base + dir->AddressOfFunctions);

        for (DWORD i = 0; i < dir->NumberOfNames; ++i)
        {
            auto* name = reinterpret_cast<const char*>(base + names[i]);
            if (fnv1a(name) == fn_hash)
                return base + fns[ords[i]];
        }
        return 0;
    }


    template<typename T>
    inline T get(uint32_t mod_hash, uint32_t fn_hash)
    {
        uintptr_t base = find_module(mod_hash);
        if (!base)
        {


            return nullptr;
        }
        return reinterpret_cast<T>(find_export(base, fn_hash));
    }


    template<typename T>
    inline T get(const char* mod, const char* fn)
    {

        wchar_t wmod[64]{};
        for (int i = 0; mod[i] && i < 63; ++i) wmod[i] = static_cast<wchar_t>(mod[i]);
        return get<T>(fnv1a_w(wmod), fnv1a(fn));
    }
}

#define MOD_HASH(s)  (::peb_import::fnv1a_w(L##s))
#define FN_HASH(s)   (::peb_import::fnv1a(s))

#define PIMPORT(ret, mod, fn) \
    ::peb_import::get<ret(WINAPI*)()>(MOD_HASH(#mod ".dll"), FN_HASH(#fn))
