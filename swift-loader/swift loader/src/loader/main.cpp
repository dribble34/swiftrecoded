#include <intrin.h>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include "nt_types.hpp"
#include "config.hpp"
#include "obfuscation.hpp"
#include "protection/syscalls.hpp"
#include "protection/antidebug.hpp"
#include "protection/integrity.hpp"
#include "protection/hwid.hpp"
#include "crypto/crypto.hpp"
#include "network/network.hpp"
#include "mapper/mapper.hpp"
#include "steam/steam.hpp"
#include "loader_ui/loader_ui.hpp"

static constexpr uint8_t EXPECTED_HASH[32] = CFG_EXPECTED_HASH;

[[noreturn]] static void abort_silently()
{
    syscalls::terminate_process(reinterpret_cast<HANDLE>(-1), 0);
    for (;;);
}

static bool hyperv_or_vm_present()
{
    int cpu[4]{};
    __cpuid(cpu, 1);
    if (!(cpu[2] & (1 << 31))) return false;
    __cpuid(cpu, 0x40000000);
    char v[13]{};
    memcpy(v, &cpu[1], 4);
    memcpy(v + 4, &cpu[2], 4);
    memcpy(v + 8, &cpu[3], 4);
    constexpr const char* known[] = {
        "Microsoft Hv","VMwareVMware","VBoxVBoxVBox",
        "XenVMMXenVMM","KVMKVMKVM   ","prl hyperv  ",
    };
    for (auto* s : known) if (!memcmp(v, s, 12)) return true;
    return false;
}

static bool debugger_or_tool_present()
{
    auto* peb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    if (peb[2]) return true;
    if (*reinterpret_cast<uint32_t*>(peb + 0xBC) & 0x70) return true;
    LONG port = 0;
    syscalls::query_information_process(reinterpret_cast<HANDLE>(-1), 7, &port, sizeof port, nullptr);
    if (port) return true;

    ULONG  sz = 1024 * 1024;
    void* buf = nullptr;
    SIZE_T bsz = sz;
    syscalls::allocate_virtual_memory(reinterpret_cast<HANDLE>(-1), &buf, 0, &bsz,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) return false;

    constexpr uint32_t bad[] = {
        peb_import::fnv1a("x64dbg.exe"),
        peb_import::fnv1a("x32dbg.exe"),
        peb_import::fnv1a("ollydbg.exe"),
        peb_import::fnv1a("ida.exe"),
        peb_import::fnv1a("ida64.exe"),
        peb_import::fnv1a("cheatengine-x86_64.exe"),
        peb_import::fnv1a("cheatengine.exe"),
        peb_import::fnv1a("processhacker.exe"),
        peb_import::fnv1a("procmon64.exe"),
        peb_import::fnv1a("wireshark.exe"),
        peb_import::fnv1a("fiddler.exe"),
        0
    };

    bool found = false;
    ULONG ret = 0;
    if (syscalls::query_system_information(5, buf, sz, &ret) >= 0)
    {
        auto* e = reinterpret_cast<SYSTEM_PROCESS_INFO*>(buf);
        for (;; )
        {
            if (e->ImageName.Buffer && e->ImageName.Length)
            {
                uint32_t h = 2166136261u;
                int len = e->ImageName.Length / 2;
                for (int i = 0; i < len; ++i)
                {
                    uint8_t ch = static_cast<uint8_t>(e->ImageName.Buffer[i] & 0xFF);
                    if (ch >= 'A' && ch <= 'Z') ch |= 0x20;
                    h = (h ^ ch) * 16777619u;
                }
                for (int i = 0; bad[i]; ++i)
                    if (h == bad[i]) { found = true; break; }
            }
            if (!e->NextEntryOffset || found) break;
            e = reinterpret_cast<SYSTEM_PROCESS_INFO*>(
                reinterpret_cast<uint8_t*>(e) + e->NextEntryOffset);
        }
    }

    SIZE_T bsz2 = bsz;
    syscalls::free_virtual_memory(reinterpret_cast<HANDLE>(-1), &buf, &bsz2, MEM_RELEASE);
    return found;
}

static bool ssl_intercept_active()
{
    constexpr const wchar_t* keys[] = {
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\WinDivert",
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\npcap",
    };
    for (auto* p : keys)
    {
        UNICODE_STRING_NT us{ static_cast<USHORT>(wcslen(p) * 2),
                              static_cast<USHORT>(wcslen(p) * 2 + 2),
                              const_cast<wchar_t*>(p) };
        OBJECT_ATTRIBUTES_NT oa{};
        INIT_OA(oa, &us, OBJ_CASE_INSENSITIVE);
        HANDLE key = nullptr;
        if (syscalls::open_key(&key, KEY_READ, &oa) >= 0 && key)
        {
            syscalls::close(key);
            return true;
        }
    }
    return false;
}

static void disable_hyperv_and_notify()
{
    const uintptr_t ntdll = peb_import::find_module(MOD_HASH("ntdll.dll"));

    using RtlCreateProcessParametersEx_t = LONG(NTAPI*)(
        PVOID*, UNICODE_STRING_NT*, UNICODE_STRING_NT*, UNICODE_STRING_NT*,
        UNICODE_STRING_NT*, PVOID, UNICODE_STRING_NT*, UNICODE_STRING_NT*,
        PVOID, PVOID, ULONG);
    using RtlCreateUserProcess_t = LONG(NTAPI*)(
        UNICODE_STRING_NT*, ULONG, PVOID, PVOID, PVOID,
        HANDLE, BOOLEAN, HANDLE, HANDLE, void*);
    using RtlDestroyProcessParameters_t = void(NTAPI*)(PVOID);

    const auto fn_cpp = reinterpret_cast<RtlCreateProcessParametersEx_t>(
        peb_import::find_export(ntdll, FN_HASH("RtlCreateProcessParametersEx")));
    const auto fn_cup = reinterpret_cast<RtlCreateUserProcess_t>(
        peb_import::find_export(ntdll, FN_HASH("RtlCreateUserProcess")));
    const auto fn_dpp = reinterpret_cast<RtlDestroyProcessParameters_t>(
        peb_import::find_export(ntdll, FN_HASH("RtlDestroyProcessParameters")));

    if (fn_cpp && fn_cup)
    {
        const wchar_t* img_s = L"\\??\\C:\\Windows\\System32\\bcdedit.exe";
        const wchar_t* cmd_s = L"bcdedit /set hypervisorlaunchtype off";
        UNICODE_STRING_NT img{ static_cast<USHORT>(wcslen(img_s) * 2), static_cast<USHORT>(wcslen(img_s) * 2 + 2), const_cast<wchar_t*>(img_s) };
        UNICODE_STRING_NT cmd{ static_cast<USHORT>(wcslen(cmd_s) * 2), static_cast<USHORT>(wcslen(cmd_s) * 2 + 2), const_cast<wchar_t*>(cmd_s) };

        PVOID params = nullptr;
        if (fn_cpp(&params, &img, nullptr, nullptr, &cmd, nullptr, nullptr, nullptr, nullptr, nullptr, 1) >= 0 && params)
        {
            uint8_t pi[128]{};
            *reinterpret_cast<ULONG*>(pi) = sizeof pi;
            fn_cup(&img, 0, params, nullptr, nullptr, reinterpret_cast<HANDLE>(-1), FALSE, nullptr, nullptr, pi);
            if (fn_dpp) fn_dpp(params);
            HANDLE th = *reinterpret_cast<HANDLE*>(pi + 8);
            if (th) { syscalls::resume_thread(th); syscalls::close(th); }
            HANDLE ph = *reinterpret_cast<HANDLE*>(pi);
            if (ph) syscalls::close(ph);
            syscalls::sleep_ms(2000);
        }
    }

    MessageBoxW(nullptr,
        L"Hyper-V has been disabled.\n\nPlease restart your computer, then run Swift again.",
        L"Swift Loader", 0x40u);
}

static DWORD find_pid(const wchar_t* name, int name_len)
{
    ULONG  sz = 512 * 1024;
    void* buf = nullptr;
    SIZE_T bsz = sz;
    syscalls::allocate_virtual_memory(reinterpret_cast<HANDLE>(-1), &buf, 0, &bsz,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) return 0;
    ULONG ret = 0;
    DWORD pid = 0;
    if (syscalls::query_system_information(5, buf, sz, &ret) >= 0)
    {
        auto* e = reinterpret_cast<SYSTEM_PROCESS_INFO*>(buf);
        for (;; )
        {
            if (e->ImageName.Buffer && e->ImageName.Length / 2 == static_cast<USHORT>(name_len))
            {
                bool match = true;
                for (int i = 0; i < name_len && match; ++i)
                {
                    wchar_t ch = e->ImageName.Buffer[i];
                    if (ch >= L'A' && ch <= L'Z') ch |= 32;
                    if (ch != name[i]) match = false;
                }
                if (match)
                {
                    pid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(e->UniqueProcessId));
                    break;
                }
            }
            if (!e->NextEntryOffset) break;
            e = reinterpret_cast<SYSTEM_PROCESS_INFO*>(
                reinterpret_cast<uint8_t*>(e) + e->NextEntryOffset);
        }
    }
    SIZE_T bsz2 = bsz;
    syscalls::free_virtual_memory(reinterpret_cast<HANDLE>(-1), &buf, &bsz2, MEM_RELEASE);
    return pid;
}

static HANDLE open_process_by_pid(DWORD pid)
{
    CLIENT_ID_NT         cid{ reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid)), nullptr };
    OBJECT_ATTRIBUTES_NT oa{};
    INIT_OA(oa, nullptr, 0);
    HANDLE h = nullptr;
    syscalls::open_process(&h, PROCESS_ALL_ACCESS, &oa, &cid);
    return h;
}

static bool process_has_module(HANDLE proc, uint32_t name_hash)
{
    struct PBI { PVOID r1; PVOID peb; PVOID r2[2]; ULONG_PTR pid, ppid; } pbi{};
    syscalls::query_information_process(proc, 0, &pbi, sizeof pbi, nullptr);
    if (!pbi.peb) return false;
    auto rvm = [&](uintptr_t addr, void* out, SIZE_T len) -> bool
        {
            SIZE_T rd = 0;
            return syscalls::read_virtual_memory(proc, reinterpret_cast<void*>(addr),
                out, len, &rd) >= 0 && rd == len;
        };
    uintptr_t ldr = 0;
    if (!rvm(reinterpret_cast<uintptr_t>(pbi.peb) + 0x18, &ldr, 8) || !ldr) return false;
    const uintptr_t head = ldr + 0x20;
    uintptr_t cur = 0;
    if (!rvm(head, &cur, 8)) return false;
    for (int n = 0; n < 512 && cur && cur != head; ++n)
    {
        uintptr_t name_ptr = 0;
        if (rvm(cur + 0x50, &name_ptr, 8) && name_ptr)
        {
            wchar_t nm[64]{};
            SIZE_T  rd = 0;
            syscalls::read_virtual_memory(proc, reinterpret_cast<void*>(name_ptr),
                nm, sizeof(nm) - 2, &rd);
            if (peb_import::fnv1a_w(nm) == name_hash) return true;
        }
        if (!rvm(cur, &cur, 8)) break;
    }
    return false;
}

static bool wait_for_module(HANDLE proc, uint32_t hash, DWORD timeout_ms)
{
    for (DWORD t = 0; t < timeout_ms; t += 500)
    {
        if (process_has_module(proc, hash)) return true;
        syscalls::sleep_ms(500);
    }
    return false;
}

static std::vector<uint8_t> read_file_from_exe_dir(const wchar_t* filename)
{
    auto* peb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    auto* params = *reinterpret_cast<uint8_t**>(peb + 0x20);
    struct US { uint16_t len, max; wchar_t* buf; };
    auto& img = *reinterpret_cast<US*>(params + 0x60);

    wchar_t dir[512]{};
    int n = img.len / 2;
    if (n >= 512) n = 511;
    memcpy(dir, img.buf, n * 2);
    dir[n] = 0;
    for (int i = n - 1; i >= 0; --i)
        if (dir[i] == L'\\' || dir[i] == L'/') { dir[i] = 0; break; }

    wchar_t path[600]{};
    const wchar_t* pfx = L"\\??\\";
    int j = 0, k = 0;
    while (pfx[k]) path[j++] = pfx[k++];
    k = 0;
    while (dir[k]) path[j++] = dir[k++];
    path[j++] = L'\\';
    k = 0;
    while (filename[k]) path[j++] = filename[k++];

    UNICODE_STRING_NT us{};
    us.Buffer = path; us.Length = static_cast<USHORT>(j * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES_NT oa{};
    INIT_OA(oa, &us, OBJ_CASE_INSENSITIVE);

    struct ISB { LONG Status; ULONG_PTR Info; } isb{};
    HANDLE h = nullptr;
    syscalls::open_file(&h, GENERIC_READ | SYNCHRONIZE, &oa, &isb,
        FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!h) return {};

    const uintptr_t ntdll = peb_import::find_module(MOD_HASH("ntdll.dll"));
    using NtQIF_t = LONG(NTAPI*)(HANDLE, ISB*, void*, ULONG, ULONG);
    using NtRF_t = LONG(NTAPI*)(HANDLE, HANDLE, PVOID, PVOID, ISB*, PVOID, ULONG, LARGE_INTEGER*, ULONG*);

    struct { LARGE_INTEGER alloc, eof; ULONG links; BOOLEAN del, dir; } fsi{};
    ISB isb2{};
    auto qif = reinterpret_cast<NtQIF_t>(peb_import::find_export(ntdll, peb_import::fnv1a("NtQueryInformationFile")));
    if (qif) qif(h, &isb2, &fsi, sizeof fsi, 5);

    const ULONG sz = static_cast<ULONG>(fsi.eof.QuadPart);
    if (!sz) { syscalls::close(h); return {}; }

    std::vector<uint8_t> data(sz);
    ISB isb3{};
    auto rf = reinterpret_cast<NtRF_t>(peb_import::find_export(ntdll, peb_import::fnv1a("NtReadFile")));
    if (rf) rf(h, nullptr, nullptr, nullptr, &isb3, data.data(), sz, nullptr, nullptr);
    syscalls::close(h);
    return data;
}

static bool hash_ok(const std::vector<uint8_t>& data)
{
    for (auto b : EXPECTED_HASH) if (b) goto check;
    return true;
check:
    return memcmp(crypto::sha256(data.data(), data.size()).data(), EXPECTED_HASH, 32) == 0;
}

// Key authentication is now handled inside loader_ui via keyauth_api.hpp.

#include "VMProtectSDK.h"

static bool on_inject(bool hwid_spoof, const std::string& token, const std::string& session_key_hex, std::string& status)
{
    VMProtectBeginUltra("on_inject");

    status = "Killing Steam";
    steam::kill_all();
    syscalls::sleep_ms(1000);

    if (hwid_spoof)
    {
        status = "Spoofing hardware identity";
        const std::string hw = hwid::get();
        steam::wipe_steam_machine_id();
        steam::spoof_machine_guid();
        steam::spoof_mac_addresses();
        steam::spoof_steam_identity();

        status = "Launching Steam";
        steam::launch();

        status = "Waiting for Steam to initialise";
        syscalls::sleep_ms(5000);

        steam::restore_machine_guid();
        steam::restore_mac_addresses();
        steam::restore_steam_identity();
    }
    else
    {
        status = "Launching Steam";
        steam::launch();
        syscalls::sleep_ms(2000);
    }

#ifndef _DEBUG
    if (hyperv_or_vm_present()) { disable_hyperv_and_notify(); abort_silently(); }
    if (antidebug::check() || integrity::check()) abort_silently();
#endif

    status = "Steam ready — open CS2 now";

#ifdef _DEBUG
    status = "Loading swift.bin from disk";
    std::vector<uint8_t> cheat_data = read_file_from_exe_dir(L"swift.bin");
    if (cheat_data.empty())
    {
        status = "[ERR] swift.bin not found next to swift.exe";
        return false;
    }
#else
    status = "Downloading cheat";
    auto dl = network::download_payload(token, hwid::get_short());
    if (!dl.success || dl.encrypted_data.empty()) { status = dl.error.empty() ? "[ERR] Download failed" : "[ERR] " + dl.error; return false; }

    status = "Decrypting";
    std::vector<uint8_t> cheat_data = crypto::decrypt_session_payload(dl.encrypted_data, session_key_hex);
    if (cheat_data.empty()) { status = "[ERR] Decryption failed"; return false; }
    if (!hash_ok(cheat_data)) { status = "[ERR] Integrity check failed"; return false; }
#endif

    status = "Waiting for CS2";
    DWORD cs2_pid = 0;
    for (int i = 0; i < 720 && !cs2_pid; ++i)
    {
        cs2_pid = find_pid(L"cs2.exe", 7);
        if (!cs2_pid) syscalls::sleep_ms(500);
    }
    if (!cs2_pid) { status = "[ERR] CS2 not found after 6 minutes"; return false; }

    HANDLE cs2 = open_process_by_pid(cs2_pid);
    if (!cs2) { status = "[ERR] Failed to open CS2"; return false; }

    status = "Waiting for client.dll";
    if (!wait_for_module(cs2, MOD_HASH("client.dll"), 120'000))
    {
        syscalls::close(cs2);
        status = "[ERR] client.dll not found";
        return false;
    }

    status = "Mapping cheat";
    const uintptr_t remote_base = mapper::map_remote(cs2, cheat_data);
    if (!remote_base)
    {
        syscalls::close(cs2);
        status = "[ERR] Remote map failed";
        return false;
    }

    crypto::secure_zero(cheat_data.data(), cheat_data.size());
    cheat_data.clear();
    cheat_data.shrink_to_fit();

    mapper::wipe_headers(cs2, remote_base);

    status = "Cheat running";
    syscalls::close(cs2);
    VMProtectEnd();
    return true;
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, char*, int)
{
    if (!syscalls::init())
    {
        MessageBoxW(nullptr, L"Syscall init failed.", L"Swift", 0x10u);
        return 1;
    }

    loader_ui::callbacks cb;
    cb.on_inject = [](bool hwid, const std::string& tk, const std::string& sk, std::string& st) {
        return on_inject(hwid, tk, sk, st);
    };
    loader_ui::run(cb);

    return 0;
}