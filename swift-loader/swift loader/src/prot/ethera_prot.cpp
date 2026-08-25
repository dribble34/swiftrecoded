#include "ethera_prot.hpp"
#include "webhook_report.hpp"
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <aclapi.h>
#include <intrin.h>
#include <tlhelp32.h>

struct PEB_MINIMAL {
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    BOOLEAN BitField;
    HANDLE Mutant;
    PVOID ImageBaseAddress;
    PVOID Ldr;
    PVOID ProcessParameters;
};

typedef NTSTATUS(NTAPI* pNtSetInformationThread)(HANDLE, UINT, PVOID, ULONG);
typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(HANDLE, UINT, PVOID, ULONG, PULONG);

namespace ethera_prot {

static bool g_watchdog_started = false;

[[noreturn]] void handle_attack() {
    HANDLE hCur = GetCurrentProcess();
    TerminateProcess(hCur, 0x992B);
    __fastfail(0x992B);
    for (long long int i = 0; ++i; (&i)[i] = i);
    *((char*)NULL) = 0;
    __assume(0);
}

static void specter_check_processes() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hSnap, &pe)) {
        do {
            std::string name = pe.szExeFile;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);

            const char* badProcs[] = {
                "ida.exe", "ida64.exe", "idag.exe", "idag64.exe", "idaw.exe", "idaw64.exe",
                "x64dbg.exe", "x32dbg.exe", "cheatengine-x86_64.exe", "cheatengine-i386.exe",
                "processhacker.exe", "systeminformer.exe", "scylla.exe", "scylla_x64.exe",
                "reshacker.exe", "wireshark.exe", "dnspy.exe", "fiddler.exe", "httpdebuggerui.exe"
            };

            for (const char* bp : badProcs) {
                if (name == bp || name.find(bp) != std::string::npos) {
                    CloseHandle(hSnap);
                    webhook_report::report_incident_and_die("Prohibited Analysis Process Detected: " + name);
                    return;
                }
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
}


static BOOL CALLBACK enum_windows_callback(HWND hwnd, LPARAM) {
    char title[256] = {};
    char className[256] = {};
    GetWindowTextA(hwnd, title, sizeof(title));
    GetClassNameA(hwnd, className, sizeof(className));

    std::string t = title;
    std::string c = className;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);

    const char* prohibitedTitles[] = {
        "processhacker", "systeminformer", "resource monitor",
        "fiddler", "httpdebugger", "cheat engine", "x64dbg",
        "x32dbg", "ida", "ghidra", "wireshark", "dnspy",
        "charles", "burp suite", "mitmproxy", "mcp server", "mcp-server",
        "ida-pro-mcp", "virtualbox", "vmware", "qemu", "sandboxie"
    };

    const char* prohibitedClasses[] = {
        "ProcessHacker", "Qt5QWindowIcon", "MainWindowClassName",
        "BrocessRacker", "HTTPDebugger", "Fiddler", "VBoxTray", "VMwareUser"
    };

    for (const char* p : prohibitedTitles) {
        if (t.find(p) != std::string::npos) {
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            webhook_report::report_incident_and_die("Prohibited Analysis Tool / Window Detected: " + t);
        }
    }

    for (const char* p : prohibitedClasses) {
        if (c.find(p) != std::string::npos) {
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            webhook_report::report_incident_and_die("Prohibited Window Class Detected: " + c);
        }
    }

    return TRUE;
}

void specter_check_debug() {
    // Scan processes
    specter_check_processes();

    // Check PEB BeingDebugged

    auto* pPeb = reinterpret_cast<PEB_MINIMAL*>(__readgsqword(0x60));
    if (pPeb && pPeb->BeingDebugged) {
        webhook_report::report_incident_and_die("Debugger Detected via PEB BeingDebugged");
    }

    // Check NtGlobalFlag (offset 0xBC on x64 PEB)
    DWORD* pNtGlobalFlag = reinterpret_cast<DWORD*>(reinterpret_cast<uintptr_t>(pPeb) + 0xBC);
    if (*pNtGlobalFlag & 0x70) {
        webhook_report::report_incident_and_die("Debugger Flag Detected via PEB NtGlobalFlag");
    }

    // Check NtQueryInformationProcess ProcessDebugPort (0x07)
    HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
    if (hNtDll) {
        auto pfnQuery = reinterpret_cast<pNtQueryInformationProcess>(GetProcAddress(hNtDll, "NtQueryInformationProcess"));
        if (pfnQuery) {
            DWORD_PTR debugPort = 0;
            if (pfnQuery(GetCurrentProcess(), 7, &debugPort, sizeof(debugPort), nullptr) == 0 && debugPort != 0) {
                webhook_report::report_incident_and_die("Debugger Detected via ProcessDebugPort");
            }
            DWORD debugFlags = 0;
            if (pfnQuery(GetCurrentProcess(), 0x1E, &debugFlags, sizeof(debugFlags), nullptr) == 0 && debugFlags == 0) {
                webhook_report::report_incident_and_die("Debugger Detected via ProcessDebugFlags");
            }
        }
    }

    // Check driver handles
    const char* driverDevices[] = {
        "\\\\.\\kdstinker",
        "\\\\.\\KsDumper",
        "\\\\.\\HyperHideDrv"
    };
    for (const char* dev : driverDevices) {
        HANDLE hFile = CreateFileA(dev, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            webhook_report::report_incident_and_die("Kernel Dumper / Protection Driver Detected: " + std::string(dev));
        }
    }

    // Scan window titles
    EnumWindows(enum_windows_callback, 0);
}

void specter_mitigations() {
    PROCESS_MITIGATION_ASLR_POLICY aslr{};
    aslr.EnableBottomUpRandomization = 1;
    aslr.EnableForceRelocateImages = 1;
    aslr.EnableHighEntropy = 1;

    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY sig{};
    sig.MicrosoftSignedOnly = 1;

    PROCESS_MITIGATION_IMAGE_LOAD_POLICY img{};
    img.PreferSystem32Images = TRUE;
    img.NoRemoteImages = TRUE;
    img.NoLowMandatoryLabelImages = TRUE;

    PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY handlePolicy{};
    handlePolicy.RaiseExceptionOnInvalidHandleReference = 1;
    handlePolicy.HandleExceptionsPermanentlyEnabled = 1;

    SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr, sizeof(aslr));
    SetProcessMitigationPolicy(ProcessSignaturePolicy, &sig, sizeof(sig));
    SetProcessMitigationPolicy(ProcessImageLoadPolicy, &img, sizeof(img));
    SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &handlePolicy, sizeof(handlePolicy));
}

void specter_ntdll_unhook() {
    HANDLE process = GetCurrentProcess();
    HMODULE ntdllModule = GetModuleHandleA("ntdll.dll");
    if (!ntdllModule) return;

    MODULEINFO mi = {};
    GetModuleInformation(process, ntdllModule, &mi, sizeof(mi));
    LPVOID ntdllBase = mi.lpBaseOfDll;

    HANDLE ntdllFile = CreateFileA("c:\\windows\\system32\\ntdll.dll", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (ntdllFile == INVALID_HANDLE_VALUE) return;

    HANDLE ntdllMapping = CreateFileMappingA(ntdllFile, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    if (!ntdllMapping) {
        CloseHandle(ntdllFile);
        return;
    }

    LPVOID ntdllMappingAddress = MapViewOfFile(ntdllMapping, FILE_MAP_READ, 0, 0, 0);
    if (!ntdllMappingAddress) {
        CloseHandle(ntdllMapping);
        CloseHandle(ntdllFile);
        return;
    }

    PIMAGE_DOS_HEADER hookedDosHeader = (PIMAGE_DOS_HEADER)ntdllBase;
    PIMAGE_NT_HEADERS hookedNtHeader = (PIMAGE_NT_HEADERS)((DWORD_PTR)ntdllBase + hookedDosHeader->e_lfanew);

    for (WORD i = 0; i < hookedNtHeader->FileHeader.NumberOfSections; i++) {
        PIMAGE_SECTION_HEADER hookedSectionHeader = (PIMAGE_SECTION_HEADER)((DWORD_PTR)IMAGE_FIRST_SECTION(hookedNtHeader) + ((DWORD_PTR)IMAGE_SIZEOF_SECTION_HEADER * i));

        if (strcmp((char*)hookedSectionHeader->Name, ".text") == 0) {
            DWORD oldProtection = 0;
            LPVOID pTarget = (LPVOID)((DWORD_PTR)ntdllBase + (DWORD_PTR)hookedSectionHeader->VirtualAddress);
            LPVOID pSource = (LPVOID)((DWORD_PTR)ntdllMappingAddress + (DWORD_PTR)hookedSectionHeader->VirtualAddress);
            SIZE_T size = hookedSectionHeader->Misc.VirtualSize;

            if (VirtualProtect(pTarget, size, PAGE_EXECUTE_READWRITE, &oldProtection)) {
                memcpy(pTarget, pSource, size);
                VirtualProtect(pTarget, size, oldProtection, &oldProtection);
            }
        }
    }

    UnmapViewOfFile(ntdllMappingAddress);
    CloseHandle(ntdllMapping);
    CloseHandle(ntdllFile);
}

void specter_reset_peb() {
    auto* pPeb = reinterpret_cast<PEB_MINIMAL*>(__readgsqword(0x60));
    if (!pPeb) return;

    DWORD old = 0;
    VirtualProtect(pPeb, sizeof(PEB_MINIMAL), PAGE_READWRITE, &old);
    pPeb->BeingDebugged = 0;

    DWORD* pNtGlobalFlag = reinterpret_cast<DWORD*>(reinterpret_cast<uintptr_t>(pPeb) + 0xBC);
    *pNtGlobalFlag = 0;

    VirtualProtect(pPeb, sizeof(PEB_MINIMAL), old, &old);
}

void specter_remap_sections() {
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!base) return;

    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (sec[i].Misc.VirtualSize == 0) continue;
        DWORD old = 0;
        void* addr = reinterpret_cast<void*>(base + sec[i].VirtualAddress);
        VirtualProtect(addr, sec[i].Misc.VirtualSize, PAGE_EXECUTE_READWRITE, &old);
    }
}

void specter_protect_process() {
    HANDLE handle = GetCurrentProcess();
    DWORD aclSize = sizeof(ACL) + sizeof(ACCESS_DENIED_ACE) + GetSidLengthRequired(1);
    PACL pDacl = (PACL)new BYTE[aclSize];
    ZeroMemory(pDacl, aclSize);
    InitializeAcl(pDacl, aclSize, ACL_REVISION);

    PSID pSpecificSid = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuth = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&SIDAuth, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pSpecificSid);
    AddAccessDeniedAce(pDacl, ACL_REVISION, DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER | SYNCHRONIZE, pSpecificSid);
    FreeSid(pSpecificSid);

    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, pDacl, FALSE);
    SetKernelObjectSecurity(handle, DACL_SECURITY_INFORMATION, &sd);

    delete[] pDacl;
}

bool specter_self_delete() {
    WCHAR wcPath[MAX_PATH + 1] = {};
    if (!GetModuleFileNameW(NULL, wcPath, MAX_PATH)) return false;

    HANDLE hCurrent = CreateFileW(wcPath, DELETE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hCurrent == INVALID_HANDLE_VALUE) return false;

    LPCWSTR lpwStream = L":Specter.enc";
    DWORD bslpwStream = (DWORD)(wcslen(lpwStream)) * sizeof(WCHAR);
    DWORD bsfRename = sizeof(FILE_RENAME_INFO) + bslpwStream;

    auto* fRename = (FILE_RENAME_INFO*)malloc(bsfRename);
    if (!fRename) {
        CloseHandle(hCurrent);
        return false;
    }

    ZeroMemory(fRename, bsfRename);
    fRename->FileNameLength = bslpwStream;
    memcpy(fRename->FileName, lpwStream, bslpwStream);

    if (!SetFileInformationByHandle(hCurrent, FileRenameInfo, fRename, bsfRename)) {
        free(fRename);
        CloseHandle(hCurrent);
        return false;
    }

    free(fRename);
    CloseHandle(hCurrent);

    hCurrent = CreateFileW(wcPath, DELETE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hCurrent == INVALID_HANDLE_VALUE) return false;

    FILE_DISPOSITION_INFO fDelete = {};
    fDelete.DeleteFile = TRUE;
    bool ok = SetFileInformationByHandle(hCurrent, FileDispositionInfo, &fDelete, sizeof(fDelete));
    CloseHandle(hCurrent);
    return ok;
}

static DWORD WINAPI watchdog_thread_proc(LPVOID) {
    HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
    if (hNtDll) {
        auto pfnHide = reinterpret_cast<pNtSetInformationThread>(GetProcAddress(hNtDll, "NtSetInformationThread"));
        if (pfnHide) {
            pfnHide(GetCurrentThread(), 0x11, nullptr, 0); // HideThreadFromDebugger (0x11)
        }
    }

    while (true) {
        specter_check_debug();
        Sleep(1500);
    }
    return 0;
}

void specter_start_watchdog() {
    if (g_watchdog_started) return;
    g_watchdog_started = true;
    HANDLE hTh = CreateThread(nullptr, 0, watchdog_thread_proc, nullptr, 0, nullptr);
    if (hTh) CloseHandle(hTh);
}

void init() {
    specter_check_debug();
    specter_ntdll_unhook();
    specter_mitigations();
    specter_reset_peb();
    specter_protect_process();
    specter_start_watchdog();
}

} // namespace ethera_prot
