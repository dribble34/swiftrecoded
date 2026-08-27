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

static void log_inject(const char* msg) {
    std::ofstream diag("swift_injection.log", std::ios::app);
    if (diag.is_open()) diag << "[LOADER] " << msg << "\n";
}

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

// Poll until cs2.exe shows up in the process list, or timeout. Returns 0 on timeout.
static DWORD wait_for_cs2(int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        DWORD pid = find_cs2_pid();
        if (pid) return pid;
        syscalls::sleep_ms(500);
        elapsed += 500;
    }
    return 0;
}

// Poll until the three engine modules are all loaded in the target process, or timeout.
// Injecting before these are up puts the DLL at risk of pattern-scanning missing modules
// and either crashing or silently failing.
static bool wait_for_engine_ready(DWORD pid, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (game_has_module(pid, L"client.dll") &&
            game_has_module(pid, L"engine2.dll") &&
            game_has_module(pid, L"server.dll")) {
            return true;
        }
        syscalls::sleep_ms(1000);
        elapsed += 1000;
    }
    return false;
}

static std::atomic<bool> g_is_injecting{false};

// RAII guard so failed injections don't permanently lock the loader for the session.
struct InjectGuard {
    bool held{false};
    bool acquire() { bool exp = false; held = g_is_injecting.compare_exchange_strong(exp, true); return held; }
    ~InjectGuard() { if (held) g_is_injecting.store(false); }
};

DownloadResult download_and_inject(const std::string& key, DownloadState* state) {
    DownloadResult res;

    // Guard: block concurrent runs, but ALWAYS release on return (RAII).
    // The old code set the flag to true and never reset it on failure paths,
    // so any failed attempt locked the loader for the rest of the session.
    InjectGuard guard;
    if (!guard.acquire()) {
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

    log_inject("Downloading encrypted payload...");
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

    // Sanity-check PE headers before touching CS2 -- fail fast if the payload is malformed
    {
        if (peData.size() < sizeof(IMAGE_DOS_HEADER)) {
            res.message = "Payload too small to be a valid PE.";
            return res;
        }
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
    }
    log_inject("Payload decrypted and validated.");

    // ── Stage 2: Ensure CS2 is running ─────────────────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::WaitingForGame);

    DWORD csPid = find_cs2_pid();
    if (!csPid) {
        // CS2 not running yet -> trigger auto launch via Steam.
        log_inject("CS2 not running. Launching via Steam...");
        ShellExecuteW(nullptr, L"open", L"steam://rungameid/730", nullptr, nullptr, SW_SHOWNORMAL);

        // Timeout: 3 minutes. If Steam is missing/broken or the user cancels
        // the launch prompt, we surface a real error instead of hanging.
        csPid = wait_for_cs2(180000);
        if (!csPid) {
            crypto::secure_zero(peData.data(), peData.size());
            res.message = "CS2 did not launch within 3 minutes. Is Steam installed and CS2 owned?";
            return res;
        }
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "CS2 process opened (PID %lu).", csPid);
    log_inject(buf);

    HANDLE game = open_game_process(csPid);
    if (!game) {
        game = OpenProcess(PROCESS_ALL_ACCESS, FALSE, csPid);
    }
    if (!game) {
        crypto::secure_zero(peData.data(), peData.size());
        snprintf(buf, sizeof(buf), "Failed to open CS2 process (PID %lu, Error %lu).", csPid, GetLastError());
        res.message = buf;
        return res;
    }

    // Wait for the actual engine modules the injected DLL is going to pattern-scan.
    // The old code hard-slept 60 seconds regardless -- meaning if CS2 was already
    // at the main menu, users still had to sit through a full minute. And if CS2
    // was NOT loaded yet, 60s might still not be enough. Poll for readiness
    // instead, with a 3-minute cap.
    log_inject("Waiting for engine modules (client.dll, engine2.dll, server.dll)...");
    if (!wait_for_engine_ready(csPid, 180000)) {
        syscalls::close(game);
        crypto::secure_zero(peData.data(), peData.size());
        res.message = "CS2 engine modules didn't finish loading in time. Reach the main menu and try again.";
        return res;
    }

    // Small grace period after modules appear -- schemas/vtables aren't guaranteed
    // populated the instant client.dll shows up. 5s is much shorter than the
    // previous unconditional 60s.
    log_inject("Engine modules loaded. Waiting 5s for stabilization...");
    syscalls::sleep_ms(5000);

    // Patch VAC in game process (no-op on modern CS2 where no vac* module is
    // loaded inside cs2.exe; harmless to keep for older builds).
    steam::patch_vac(game);
    syscalls::sleep_ms(500);

    // ── Stage 3: Remote Map PE Buffer using original mapper ──────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Injecting);
    log_inject("Mapping DLL into CS2...");

    const uintptr_t remote_base = mapper::map_remote(game, peData);
    if (!remote_base) {
        syscalls::close(game);
        crypto::secure_zero(peData.data(), peData.size());
        log_inject("Mapper returned null base.");
        res.message = "Remote memory mapping failed.";
        return res;
    }

    crypto::secure_zero(peData.data(), peData.size());
    peData.clear();
    peData.shrink_to_fit();

    syscalls::close(game);

    // ── Stage 4: Done ─────────────────────────────────────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Cleanup);
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Done);
    log_inject("Injection complete.");

    res.success = true;
    return res;
}

} // namespace app

