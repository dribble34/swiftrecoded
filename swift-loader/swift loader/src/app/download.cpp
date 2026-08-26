// ── download.cpp ──────────────────────────────────────────────────────────────
#include "app/download.h"
#include "app/keyauth.hpp"
#include "loader/crypto/crypto.hpp"
#include "loader/crypto/rsa_verify.hpp"
#include "loader/obfuscation.hpp"
#include "loader/network/network.hpp"
#include "loader/steam/steam.hpp"
#include "loader/mapper/mapper.hpp"
#include "loader/protection/syscalls.hpp"
#include "prot/webhook_report.hpp"
#include <winhttp.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <vector>
#include <sstream>
#include <fstream>

#pragma comment(lib, "winhttp.lib")

namespace app {

static DWORD find_cs2_pid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static HANDLE open_game_process(DWORD pid) {
    CLIENT_ID_NT cid{ reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid)), nullptr };
    OBJECT_ATTRIBUTES_NT oa{};
    INIT_OA(oa, nullptr, 0);
    HANDLE h = nullptr;
    syscalls::open_process(&h, PROCESS_ALL_ACCESS, &oa, &cid);
    return h;
}

static bool game_has_module(DWORD pid, const wchar_t* modName) {
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

static void wait_module(DWORD pid, const wchar_t* modName) {
    // Wait until module appears in process module snapshot
    while (!game_has_module(pid, modName)) {
        syscalls::sleep_ms(500);
    }
    // Give engine an extra 3 seconds to finish initializing hooks & textures
    syscalls::sleep_ms(3000);
}

static std::atomic<bool> g_is_injecting{false};

DownloadResult download_and_inject(const std::string& key, DownloadState* state) {
    DownloadResult res;

    // Guard: Prevent any concurrent or repeat execution in the same session
    bool expected = false;
    if (!g_is_injecting.compare_exchange_strong(expected, true)) {
        res.message = "Injection already in progress.";
        return res;
    }

    // ── Stage 1: Download Encrypted Payload Stream ───────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Downloading);

    std::string hwid = keyauth::get_hwid();
    keyauth::Result vr = keyauth::g_last_verify_result;
    if (vr.session_token.empty() || vr.cnonce.empty() || vr.snonce.empty()) {
        res.message = "Security error: Missing session token. Please re-authenticate.";
        return res;
    }

    auto dl = network::download_payload(key, hwid, vr.session_token, vr.cnonce, vr.snonce);
    if (!dl.success || dl.encrypted_data.empty()) {
        res.message = dl.error.empty() ? "Failed to download payload." : dl.error;
        return res;
    }

    std::vector<uint8_t> peData = crypto::decrypt_swift_payload(dl.encrypted_data, key, hwid, vr.session_token, vr.cnonce, vr.snonce);
    if (peData.empty()) {
        res.message = "Payload decryption failed.";
        return res;
    }

    DWORD ep_rva = 0;
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(peData.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            res.message = "Invalid binary image format.";
            return res;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(peData.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            res.message = "Invalid NT header format.";
            return res;
        }
        ep_rva = nt->OptionalHeader.AddressOfEntryPoint;
    }

    // ── Stage 2: Ensure CS2 is running ─────────────────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::WaitingForGame);

    DWORD csPid = find_cs2_pid();
    if (!csPid) {
        // CS2 is not running yet -> trigger auto launch via Steam
        ShellExecuteW(nullptr, L"open", L"steam://rungameid/730", nullptr, nullptr, SW_SHOWNORMAL);

        // Poll until CS2 process is detected
        while (!csPid) {
            csPid = find_cs2_pid();
            if (!csPid) syscalls::sleep_ms(500);
        }
    }

    HANDLE game = open_game_process(csPid);
    if (!game) {
        game = OpenProcess(PROCESS_ALL_ACCESS, FALSE, csPid);
    }
    if (!game) {
        crypto::secure_zero(peData.data(), peData.size());
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Failed to open CS2 process (PID %lu, Error %lu).", csPid, GetLastError());
        res.message = errBuf;
        return res;
    }

    // If CS2 is already running, proceed directly to countdown
    {
        std::ofstream diag("swift_injection.log", std::ios::app);
        if (diag.is_open()) diag << "[LOADER] CS2 process opened (PID " << csPid << "). Starting 60-second stabilization timer...\n";
    }

    // Wait 60 seconds (1 full minute) to ensure CS2 DirectX renderer, panorama, and main menu are 100% loaded
    for (int sec = 1; sec <= 60; ++sec) {
        Sleep(1000);
        if (sec % 10 == 0) {
            std::ofstream diag("swift_injection.log", std::ios::app);
            if (diag.is_open()) diag << "[LOADER] Waiting for CS2 main menu: " << sec << "/60s elapsed.\n";
        }
    }

    {
        std::ofstream diag("swift_injection.log", std::ios::app);
        if (diag.is_open()) diag << "[LOADER] 60 seconds elapsed. Proceeding to VAC patch & manual map...\n";
    }

    // Patch VAC in game process
    steam::patch_vac(game);
    syscalls::sleep_ms(1000);

    // ── Stage 3: Remote Map PE Buffer using original mapper ──────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Injecting);

    const uintptr_t remote_base = mapper::map_remote(game, peData);
    if (!remote_base) {
        syscalls::close(game);
        crypto::secure_zero(peData.data(), peData.size());
        res.message = "Remote memory mapping failed.";
        return res;
    }

    crypto::secure_zero(peData.data(), peData.size());
    peData.clear();
    peData.shrink_to_fit();

    if (game) syscalls::close(game);

    // ── Stage 4: Done ─────────────────────────────────────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Cleanup);
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Done);

    res.success = true;
    return res;
}

} // namespace app

