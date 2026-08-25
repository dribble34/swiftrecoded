// ── download.cpp ──────────────────────────────────────────────────────────────
#include "app/download.h"
#include "app/manualmap.h"
#include "app/keyauth.hpp"
#include "loader/crypto/crypto.hpp"
#include "loader/crypto/rsa_verify.hpp"
#include "loader/obfuscation.hpp"
#include "prot/webhook_report.hpp"
#include <winhttp.h>
#include <shellapi.h>
#include <vector>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace app {

static std::string url_encode(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)c;
        } else {
            char h[4];
            snprintf(h, sizeof h, "%%%02X", c);
            o += h;
        }
    }
    return o;
}

DownloadResult download_and_inject(const std::string& key, DownloadState* state) {
    DownloadResult res;

    // ── Stage 1: Download Encrypted Memory Stream ───────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Downloading);

    HINTERNET hSession = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        res.message = "Failed to open WinHTTP session.";
        return res;
    }

    HINTERNET hConnect = WinHttpConnect(
        hSession, L"api.swiftfly.xyz",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        res.message = "Failed to connect to Swift API.";
        return res;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", L"/download.php",
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.message = "Failed to create request handle.";
        return res;
    }

    std::string hwid = keyauth::get_hwid();
    keyauth::Result vr = keyauth::g_last_verify_result;
    if (vr.session_token.empty() || vr.cnonce.empty() || vr.snonce.empty()) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.message = "Security error: Missing session token. Please re-authenticate.";
        return res;
    }

    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    std::string body = "key=" + url_encode(key) +
                       "&hwid=" + url_encode(hwid) +
                       "&session_token=" + url_encode(vr.session_token) +
                       "&cnonce=" + url_encode(vr.cnonce) +
                       "&snonce=" + url_encode(vr.snonce);

    BOOL ok = WinHttpSendRequest(
        hRequest, headers.c_str(), (DWORD)-1,
        (LPVOID)body.c_str(), (DWORD)body.size(),
        (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);

    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.message = "HTTP request execution failed.";
        return res;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode, &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    if (statusCode == 401 || statusCode == 403) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.message = "License invalid, expired, or session token used/expired.";
        return res;
    }

    if (statusCode == 429) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.message = "Download rate limit exceeded. Try again later.";
        return res;
    }

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.message = "API server error (HTTP " + std::to_string(statusCode) + ").";
        return res;
    }

    // Read X-Swift-Signature header for binary RSA signature verification
    wchar_t sigBuf[1024] = {};
    DWORD sigBufSize = sizeof(sigBuf);
    std::string signatureHeader;
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"X-Swift-Signature", sigBuf, &sigBufSize, WINHTTP_NO_HEADER_INDEX)) {
        char sigA[1024] = {};
        WideCharToMultiByte(CP_UTF8, 0, sigBuf, -1, sigA, sizeof(sigA), nullptr, nullptr);
        signatureHeader = sigA;
    }

    // Stream directly into memory vector
    std::vector<uint8_t> encData;
    BYTE buf[8192];
    DWORD avail = 0, readBytes = 0;

    for (;;) {
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
            break;
        DWORD toRead = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
        if (!WinHttpReadData(hRequest, buf, toRead, &readBytes) || readBytes == 0)
            break;
        encData.insert(encData.end(), buf, buf + readBytes);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (encData.empty()) {
        res.message = "Downloaded payload buffer is empty.";
        return res;
    }

    // ── Verify Server RSA Signature of Binary Payload ─────────────────────
    if (signatureHeader.empty() || !rsa_verify::verify_signature(encData.data(), encData.size(), signatureHeader)) {
        SecureZeroMemory(encData.data(), encData.size());
        webhook_report::report_incident_and_die("Binary Payload RSA Signature Mismatch on /download.php (Spoof attempt)");
        res.message = "Security error: Invalid payload RSA signature (Spoof attempt detected).";
        return res;
    }

    // ── Derive Dynamic Session Key & IV ───────────────────────────────────
    std::string keySeed = "swift_key_" + hwid + "_" + key + "_" + vr.session_token + "_" + vr.cnonce + "_" + vr.snonce;
    std::string ivSeed  = "swift_iv_" + vr.snonce + "_" + vr.cnonce + "_" + hwid;

    crypto::Key aesKey = crypto::sha256(reinterpret_cast<const uint8_t*>(keySeed.data()), keySeed.size());
    auto aesIvHash = crypto::sha256(reinterpret_cast<const uint8_t*>(ivSeed.data()), ivSeed.size());

    crypto::IV aesIv{};
    memcpy(aesIv.data(), aesIvHash.data(), 16);

    std::vector<uint8_t> peData = crypto::decrypt(encData, aesKey, aesIv);
    SecureZeroMemory(encData.data(), encData.size());

    if (peData.empty()) {
        res.message = "Payload decryption failed (invalid key or tampered data).";
        return res;
    }

    std::string xorKeyStr = hwid + vr.session_token;
    crypto::xor_layer(peData, xorKeyStr);

    // ── Stage 2: Ensure CS2 is running ─────────────────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::WaitingForGame);

    DWORD csPid = find_process_pid(L"cs2.exe");
    if (!csPid) {
        ShellExecuteA(nullptr, "open", "steam://rungameid/730",
            nullptr, nullptr, SW_SHOWNORMAL);

        const int maxWaitMs = 120000;
        const int pollMs    = 2000;
        int waited = 0;
        while (waited < maxWaitMs) {
            Sleep(pollMs);
            waited += pollMs;
            csPid = find_process_pid(L"cs2.exe");
            if (csPid) break;
        }
        if (!csPid) {
            SecureZeroMemory(peData.data(), peData.size());
            res.message = "CS2 did not start within expected timeframe.";
            return res;
        }

        // Allow CS2 game engine time to complete initialization
        Sleep(8000);
    }

    // ── Stage 3: Inject In-Memory Buffer directly ─────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Injecting);

    MapResult mapRes = manual_map_inject(peData);

    // Immediately zero out PE memory buffer in loader RAM
    SecureZeroMemory(peData.data(), peData.size());
    peData.clear();

    if (!mapRes.success) {
        res.message = mapRes.message;
        return res;
    }

    // ── Stage 4: Cleanup ──────────────────────────────────────────────────
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Cleanup);
    if (state) InterlockedExchange(&state->stage, (LONG)DlStage::Done);

    res.success = true;
    return res;
}

} // namespace app

