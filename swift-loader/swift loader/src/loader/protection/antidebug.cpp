#include "antidebug.hpp"
#include "syscalls.hpp"
#include "integrity.hpp"
#include "../obfuscation.hpp"
#include "../config.hpp"
#include "nt_types.hpp"
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <string>

static uintptr_t own_base()
{
    return *reinterpret_cast<uintptr_t*>(__readgsqword(0x60) + 0x10);
}

[[noreturn]] static void self_destruct()
{
    uintptr_t base = own_base();
    if (base)
    {
        void* b = (void*)base; SIZE_T sz = 0x1000; ULONG old = 0;
        syscalls::protect_virtual_memory((HANDLE)-1, &b, &sz, PAGE_READWRITE, &old);
        memset(b, 0, 0x1000);
    }
    syscalls::terminate_process((HANDLE)-1, syscalls::NT_SUCCESS_VAL);
    __assume(0);
}

static bool peb_being_debugged()
{
    return reinterpret_cast<uint8_t*>(__readgsqword(0x60))[2] != 0;
}

static bool nt_global_flag()
{
    uint32_t f = *reinterpret_cast<uint32_t*>(__readgsqword(0x60) + 0xBC);
    return (f & 0x70) != 0;
}

static bool heap_flags()
{
    auto* peb  = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    auto* heap = *reinterpret_cast<uint8_t**>(peb + 0x30);
    if (!heap) return false;
    uint32_t fl = *reinterpret_cast<uint32_t*>(heap + 0x70);
    uint32_t ff = *reinterpret_cast<uint32_t*>(heap + 0x74);
    if (fl & 0x40000000 || ff & 0x40000000) return true;
    if (fl != 2 && fl != 0x1000002 && fl != 0x8000002) return true;
    return false;
}

static bool debug_port()
{
    HANDLE p = nullptr;
    return syscalls::query_information_process((HANDLE)-1, 7, &p, sizeof(p), nullptr) >= 0 && p;
}

static bool debug_object_handle()
{
    HANDLE h = nullptr; ULONG r = 0;
    auto st = syscalls::query_information_process((HANDLE)-1, 30, &h, sizeof(h), &r);
    return st >= 0 && h;
}

static bool debug_flags()
{
    ULONG f = 0;
    return syscalls::query_information_process((HANDLE)-1, 31, &f, sizeof(f), nullptr) >= 0 && f == 0;
}

static bool hw_breakpoints()
{
    CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    return syscalls::get_context_thread((HANDLE)-2, &ctx) >= 0 &&
           (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3);
}

static bool ntdll_hooks()
{
    uintptr_t ntdll = peb_import::find_module(MOD_HASH("ntdll.dll"));
    if (!ntdll) return false;
    uint32_t targets[] = {
        FN_HASH("NtQueryInformationProcess"),
        FN_HASH("NtSetInformationThread"),
        FN_HASH("NtQuerySystemInformation"),
        FN_HASH("NtQueryObject"),
        FN_HASH("NtClose"),
        FN_HASH("NtGetContextThread"),
        0
    };
    for (int i = 0; targets[i]; ++i)
    {
        auto* fn = reinterpret_cast<const uint8_t*>(peb_import::find_export(ntdll, targets[i]));
        if (!fn) continue;
        if (fn[0] == 0xE9 || fn[0] == 0xEB)        return true;
        if (fn[0] == 0xFF && fn[1] == 0x25)         return true;
        if (fn[0] == 0x68)                           return true;
    }
    return false;
}

static bool parent_process()
{
    struct PBI { PVOID r1; PVOID peb; PVOID r2[2]; ULONG_PTR pid, ppid; } pbi{};
    syscalls::query_information_process((HANDLE)-1, 0, &pbi, sizeof(pbi), nullptr);
    if (!pbi.ppid) return false;

    CLIENT_ID_NT cid{ (HANDLE)pbi.ppid, nullptr };
    OBJECT_ATTRIBUTES_NT oa{}; INIT_OA(oa, nullptr, 0);
    HANDLE ph = nullptr;
    if (syscalls::open_process(&ph, PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid) < 0 || !ph)
        return false;

    uint8_t buf[1024]{}; ULONG r = 0;
    syscalls::query_information_process(ph, 27, buf, sizeof(buf)-2, &r);
    syscalls::close(ph);

    auto* us = reinterpret_cast<UNICODE_STRING_NT*>(buf);
    if (!us->Buffer || !us->Length) return false;

    int len = us->Length / 2;
    wchar_t* name = us->Buffer + len;
    while (name > us->Buffer && *(name-1) != L'\\') --name;

    const wchar_t* bad[] = {
        L"x64dbg.exe", L"x32dbg.exe", L"ollydbg.exe",
        L"ida.exe", L"ida64.exe", L"windbg.exe",
        L"cheatengine-x86_64.exe", L"cheatengine-i386.exe", nullptr
    };
    for (int i = 0; bad[i]; ++i)
    {
        const wchar_t* p = bad[i]; const wchar_t* n = name;
        bool ok = true;
        while (*p && *n) {
            wchar_t a = *p, b = *n;
            if (a>='A'&&a<='Z') a|=32; if (b>='A'&&b<='Z') b|=32;
            if (a!=b) { ok=false; break; }
            ++p; ++n;
        }
        if (ok && !*p && !*n) return true;
    }
    return false;
}

static bool invalid_handle()
{



    auto st = syscalls::close((HANDLE)0xDEAD0000DEAD0000ULL);
    return st == syscalls::NT_SUCCESS_VAL;
}

static bool timing()
{
    uint64_t t0 = __rdtsc();
    volatile uint64_t s = 0; for (int i=0;i<100;++i) s+=i;
    uint64_t t1 = __rdtsc();
    uint64_t t2 = __rdtsc();
    for (int i=0;i<100;++i) s+=i;
    uint64_t t3 = __rdtsc();
    uint64_t d1 = t1-t0, d2 = t3-t2;
    if (d1 > CFG_TIMING_LIMIT || d2 > CFG_TIMING_LIMIT) return true;
    if (d1>0 && d2>0) { uint64_t r = d1>d2?d1/d2:d2/d1; if (r>5) return true; }
    return false;
}

static bool blacklisted_processes()
{
    ULONG sz = 512*1024;
    void* buf = nullptr; SIZE_T bsz = sz;
    syscalls::allocate_virtual_memory((HANDLE)-1, &buf, 0, &bsz,
        MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!buf) return false;

    static const uint32_t bad[] = {
        peb_import::fnv1a("x64dbg.exe"),
        peb_import::fnv1a("x32dbg.exe"),
        peb_import::fnv1a("ollydbg.exe"),
        peb_import::fnv1a("ida.exe"),
        peb_import::fnv1a("ida64.exe"),
        peb_import::fnv1a("idag.exe"),
        peb_import::fnv1a("idag64.exe"),
        peb_import::fnv1a("windbg.exe"),
        peb_import::fnv1a("cheatengine-x86_64.exe"),
        peb_import::fnv1a("cheatengine-i386.exe"),
        peb_import::fnv1a("processhacker.exe"),
        peb_import::fnv1a("wireshark.exe"),
        peb_import::fnv1a("dnspy.exe"),
        peb_import::fnv1a("scylla_x64.exe"),
        peb_import::fnv1a("die.exe"),
        0
    };

    ULONG ret = 0; bool found = false;
    if (syscalls::query_system_information(5, buf, sz, &ret) >= 0)
    {
        auto* e = reinterpret_cast<SYSTEM_PROCESS_INFO*>(buf);
        for (;;)
        {
            if (e->ImageName.Buffer && e->ImageName.Length)
            {
                uint32_t h = 2166136261u;
                int n = e->ImageName.Length / 2;
                for (int i=0;i<n&&!found;++i) {
                    uint8_t c = (uint8_t)(e->ImageName.Buffer[i]&0xFF);
                    if (c>='A'&&c<='Z') c|=0x20;
                    h^=c; h*=16777619u;
                }
                for (int i=0; bad[i]; ++i) if (h==bad[i]) { found=true; break; }
            }
            if (!e->NextEntryOffset || found) break;
            e = reinterpret_cast<SYSTEM_PROCESS_INFO*>((uint8_t*)e + e->NextEntryOffset);
        }
    }

    SIZE_T bsz2 = bsz;
    syscalls::free_virtual_memory((HANDLE)-1, &buf, &bsz2, MEM_RELEASE);
    return found;
}

static bool vm_detected()
{
    int i[4]{}; __cpuid(i, 1);
    if (i[2] & (1<<31)) return true;
    __cpuid(i, 0x40000000);
    char v[13]{};
    memcpy(v,&i[1],4); memcpy(v+4,&i[2],4); memcpy(v+8,&i[3],4);
    const char* vms[]={"VMwareVMware","Microsoft Hv","VBoxVBoxVBox",
                       "XenVMMXenVMM","prl hyperv  ","KVMKVMKVM   ",nullptr};
    for (int j=0;vms[j];++j) if (!memcmp(v,vms[j],12)) return true;
    return false;
}

static void hide_thread()
{
    syscalls::set_information_thread((HANDLE)-2, 0x11, nullptr, 0);
}

namespace antidebug
{

bool check()
{
    hide_thread();
    if (peb_being_debugged())   return true;
    if (nt_global_flag())       return true;
    if (heap_flags())           return true;
    if (debug_port())           return true;
    if (debug_object_handle())  return true;
    if (debug_flags())          return true;
    if (hw_breakpoints())       return true;
    if (ntdll_hooks())          return true;
    if (parent_process())       return true;
    if (invalid_handle())       return true;
    if (timing())               return true;
    if (blacklisted_processes()) return true;
    if (vm_detected())          return true;
    return false;
}

void start_watchdog()
{
    HANDLE th = nullptr;
    OBJECT_ATTRIBUTES_NT oa{}; INIT_OA(oa, nullptr, 0);
    syscalls::create_thread_ex(
        &th, THREAD_ALL_ACCESS, &oa, (HANDLE)-1,
        reinterpret_cast<void*>(static_cast<DWORD(__stdcall*)(void*)>(+[](void*) -> DWORD {
            uint64_t seed = __rdtsc();
            auto rng = [&]() -> DWORD {
                seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
                return 8 + (DWORD)((seed >> 33) % 8);
            };
            syscalls::sleep_ms(rng());
            while (true)
            {
                if (check() || integrity::check())
                    self_destruct();
                syscalls::sleep_ms(rng());
            }
            return 0;
        })),
        nullptr, 0, 0, 0, 0, nullptr);
    if (th) syscalls::close(th);
}

}
