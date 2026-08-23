// ── manualmap.cpp ─────────────────────────────────────────────────────────────
// Robust x64 manual-map injector using standard Win32 APIs.
// Injects in-memory PE buffer directly into cs2.exe process space.
// ──────────────────────────────────────────────────────────────────────────────
#include "app/manualmap.h"
#include <TlHelp32.h>
#include <cstring>
#include <algorithm>

namespace app {

// ── Logging helper ────────────────────────────────────────────────────────────
static void log_diagnostic(const std::string& msg) {
    FILE* f = nullptr;
    if (fopen_s(&f, "swift_loader.log", "a") == 0 && f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg.c_str());
        fclose(f);
    }
}

// ── Check if a module is loaded in remote process ─────────────────────────────
static bool is_module_loaded_in_proc(DWORD pid, const wchar_t* modName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, modName) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// ── Find process by name ──────────────────────────────────────────────────────
DWORD find_process_pid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// ── Shellcode Data Struct ─────────────────────────────────────────────────────
struct ManualMapData {
    decltype(&LoadLibraryA)   pLoadLibraryA;
    decltype(&GetProcAddress) pGetProcAddress;

    uintptr_t imageBase;
    uintptr_t preferredBase;
    DWORD     ntHeadersRva;
    DWORD     status; // 0 = running, 1 = success, 2 = reloc fail, 3 = import fail
};

// ── Shellcode Routine (runs in remote process) ────────────────────────────────
static DWORD WINAPI ShellcodeEntry(ManualMapData* pData) {
    if (!pData) return 1;

    auto* base = reinterpret_cast<uint8_t*>(pData->imageBase);
    auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS*>(base + pData->ntHeadersRva);

    // 1. Relocations
    auto& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (relocDir.VirtualAddress && pData->imageBase != pData->preferredBase) {
        ptrdiff_t delta = (ptrdiff_t)(pData->imageBase - pData->preferredBase);
        auto* blk = reinterpret_cast<IMAGE_BASE_RELOCATION*>(base + relocDir.VirtualAddress);
        while (blk->VirtualAddress) {
            DWORD count = (blk->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            auto* entries = reinterpret_cast<WORD*>(blk + 1);
            for (DWORD i = 0; i < count; ++i) {
                int type = entries[i] >> 12;
                WORD off = entries[i] & 0xFFF;
                if (type == IMAGE_REL_BASED_DIR64)
                    *reinterpret_cast<uint64_t*>(base + blk->VirtualAddress + off) += delta;
                else if (type == IMAGE_REL_BASED_HIGHLOW)
                    *reinterpret_cast<uint32_t*>(base + blk->VirtualAddress + off) += (uint32_t)delta;
            }
            blk = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                reinterpret_cast<uint8_t*>(blk) + blk->SizeOfBlock);
        }
    }

    // 2. Imports
    auto& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress) {
        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDir.VirtualAddress);
        for (; desc->Name; ++desc) {
            const char* modName = reinterpret_cast<const char*>(base + desc->Name);
            HMODULE hMod = pData->pLoadLibraryA(modName);
            if (!hMod) {
                pData->status = 3; // Import LoadLibrary failed
                return 3;
            }

            DWORD origRva    = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
            auto* origThunk  = reinterpret_cast<IMAGE_THUNK_DATA*>(base + origRva);
            auto* firstThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

            for (; origThunk->u1.AddressOfData; ++origThunk, ++firstThunk) {
                FARPROC fn = nullptr;
                if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                    fn = pData->pGetProcAddress(hMod,
                        reinterpret_cast<LPCSTR>(IMAGE_ORDINAL(origThunk->u1.Ordinal)));
                } else {
                    auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        base + origThunk->u1.AddressOfData);
                    fn = pData->pGetProcAddress(hMod, ibn->Name);
                }
                if (!fn) {
                    pData->status = 4; // GetProcAddress failed
                    return 4;
                }
                firstThunk->u1.Function = reinterpret_cast<uintptr_t>(fn);
            }
        }
    }

    // 3. TLS Callbacks
    auto& tlsDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tlsDir.VirtualAddress && tlsDir.Size) {
        auto* tls = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(base + tlsDir.VirtualAddress);
        if (tls->AddressOfCallBacks) {
            auto** cbs = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(tls->AddressOfCallBacks);
            while (*cbs) {
                (*cbs)(reinterpret_cast<void*>(base), DLL_PROCESS_ATTACH, nullptr);
                ++cbs;
            }
        }
    }

    // 4. DllMain Entry Point
    if (nt->OptionalHeader.AddressOfEntryPoint) {
        using DllMain_t = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
        auto entry = reinterpret_cast<DllMain_t>(base + nt->OptionalHeader.AddressOfEntryPoint);
        entry(reinterpret_cast<HINSTANCE>(base), DLL_PROCESS_ATTACH, nullptr);
    }

    pData->status = 1;
    return 0;
}

static void ShellcodeEnd() {}

// ── Main Remote Inject Routine ────────────────────────────────────────────────
MapResult manual_map_inject(const std::vector<uint8_t>& pe_data) {
    MapResult res;

    if (pe_data.size() < sizeof(IMAGE_DOS_HEADER)) {
        res.message = "Invalid PE buffer: too small.";
        return res;
    }
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe_data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        res.message = "Invalid PE buffer: bad DOS signature.";
        return res;
    }
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(pe_data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        res.message = "Invalid PE buffer: bad NT signature.";
        return res;
    }

    DWORD pid = find_process_pid(L"cs2.exe");
    if (!pid) {
        res.message = "Counter-Strike 2 process not found.";
        log_diagnostic("ERROR: CS2 PID not found.");
        return res;
    }

    // Wait until CS2 engine modules (client.dll, engine2.dll, server.dll) are fully loaded
    const int maxModWaitMs = 60000;
    const int modPollMs    = 1000;
    int modWaited = 0;
    while (modWaited < maxModWaitMs) {
        if (is_module_loaded_in_proc(pid, L"client.dll") &&
            is_module_loaded_in_proc(pid, L"engine2.dll") &&
            is_module_loaded_in_proc(pid, L"server.dll"))
            break;
        Sleep(modPollMs);
        modWaited += modPollMs;
    }

    if (!is_module_loaded_in_proc(pid, L"client.dll")) {
        res.message = "CS2 engine (client.dll) not ready yet.";
        log_diagnostic("ERROR: Timeout waiting for CS2 client.dll initialization.");
        return res;
    }

    // Additional safety delay: Wait 60 seconds as requested by the user
    log_diagnostic("INFO: Engine modules detected. Waiting 60 seconds for full game initialization before injecting...");
    Sleep(60000);

    HANDLE hProc = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProc) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "ERROR: OpenProcess failed with error code %d", GetLastError());
        log_diagnostic(errBuf);
        res.message = "Failed to open CS2 process memory handles.";
        return res;
    }

    const auto& opt = nt->OptionalHeader;

    // Allocate image memory in target process (PAGE_EXECUTE_READWRITE for mapping phase)
    void* remoteBase = VirtualAllocEx(hProc, nullptr, opt.SizeOfImage,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteBase) {
        CloseHandle(hProc);
        res.message = "Failed to allocate memory in CS2.";
        return res;
    }

    // Map headers and sections into local memory layout first
    std::vector<uint8_t> localImg(opt.SizeOfImage, 0);
    memcpy(localImg.data(), pe_data.data(), opt.SizeOfHeaders);

    auto* secHdr = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        reinterpret_cast<const uint8_t*>(&nt->OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++secHdr) {
        if (!secHdr->SizeOfRawData || secHdr->PointerToRawData >= pe_data.size()) continue;
        SIZE_T csz = (std::min)((SIZE_T)secHdr->SizeOfRawData,
            pe_data.size() - secHdr->PointerToRawData);
        memcpy(localImg.data() + secHdr->VirtualAddress,
               pe_data.data() + secHdr->PointerToRawData, csz);
    }

    // Write mapped image into target process space
    if (!WriteProcessMemory(hProc, remoteBase, localImg.data(), localImg.size(), nullptr)) {
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        res.message = "Failed to write memory image to CS2.";
        return res;
    }

    // Prepare Shellcode Data
    ManualMapData mapData = {};
    mapData.pLoadLibraryA   = LoadLibraryA;
    mapData.pGetProcAddress = GetProcAddress;
    mapData.imageBase       = reinterpret_cast<uintptr_t>(remoteBase);
    mapData.preferredBase   = opt.ImageBase;
    mapData.ntHeadersRva    = dos->e_lfanew;
    mapData.status          = 0;

    SIZE_T shellcodeSize = reinterpret_cast<uintptr_t>(&ShellcodeEnd) -
                           reinterpret_cast<uintptr_t>(&ShellcodeEntry);
    if (shellcodeSize > 0x10000) shellcodeSize = 0x10000;

    SIZE_T totalAlloc = shellcodeSize + sizeof(ManualMapData) + 64;
    void* remoteShellcode = VirtualAllocEx(hProc, nullptr, totalAlloc,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteShellcode) {
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        res.message = "Failed to allocate shellcode memory in CS2.";
        return res;
    }

    uint8_t* remoteDataAddr = reinterpret_cast<uint8_t*>(remoteShellcode) + shellcodeSize + 16;

    WriteProcessMemory(hProc, remoteShellcode, &ShellcodeEntry, shellcodeSize, nullptr);
    WriteProcessMemory(hProc, remoteDataAddr, &mapData, sizeof(mapData), nullptr);

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteShellcode),
        remoteDataAddr, 0, nullptr);

    if (!hThread) {
        VirtualFreeEx(hProc, remoteShellcode, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        res.message = "Failed to create remote execution thread in CS2.";
        return res;
    }

    WaitForSingleObject(hThread, 15000);

    // Read back shellcode execution status
    ManualMapData checkData = {};
    ReadProcessMemory(hProc, remoteDataAddr, &checkData, sizeof(checkData), nullptr);

    if (checkData.status != 1) {
        char statusBuf[128];
        snprintf(statusBuf, sizeof(statusBuf), "ERROR: Shellcode execution failed with status code %d", checkData.status);
        log_diagnostic(statusBuf);
        VirtualFreeEx(hProc, remoteShellcode, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hThread);
        CloseHandle(hProc);
        res.message = "Injection shellcode failed inside CS2.";
        return res;
    }

    log_diagnostic("SUCCESS: Manual map injection completed successfully into CS2.");

    // Reset section header pointer to beginning before changing page protections
    const IMAGE_SECTION_HEADER* secHdrProt = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        reinterpret_cast<const uint8_t*>(&nt->OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++secHdrProt) {
        if (!secHdrProt->VirtualAddress) continue;
        DWORD prot = PAGE_NOACCESS;
        bool exec  = (secHdrProt->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool read  = (secHdrProt->Characteristics & IMAGE_SCN_MEM_READ)    != 0;
        bool write = (secHdrProt->Characteristics & IMAGE_SCN_MEM_WRITE)   != 0;
        if      (exec && write) prot = PAGE_EXECUTE_READWRITE;
        else if (exec && read)  prot = PAGE_EXECUTE_READ;
        else if (exec)          prot = PAGE_EXECUTE;
        else if (write)         prot = PAGE_READWRITE;
        else if (read)          prot = PAGE_READONLY;

        DWORD oldProt = 0;
        VirtualProtectEx(hProc, reinterpret_cast<uint8_t*>(remoteBase) + secHdrProt->VirtualAddress,
            secHdrProt->Misc.VirtualSize, prot, &oldProt);
    }

    // Wipe PE headers in CS2 (zeroing headers gently without setting PAGE_NOACCESS which causes crashes)
    std::vector<uint8_t> zeros(0x1000, 0);
    DWORD oldProt = 0;
    VirtualProtectEx(hProc, remoteBase, 0x1000, PAGE_READWRITE, &oldProt);
    WriteProcessMemory(hProc, remoteBase, zeros.data(), 0x1000, nullptr);
    VirtualProtectEx(hProc, remoteBase, 0x1000, PAGE_READONLY, &oldProt);

    VirtualFreeEx(hProc, remoteShellcode, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProc);

    res.success = true;
    return res;
}

} // namespace app
