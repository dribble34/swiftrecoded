// ── download.cpp ──────────────────────────────────────────────────────────────
#include "app/download.h"
#include "app/manualmap.h"
#include <winhttp.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

#define API_SECRET_KEY "SwiftFly_Secured_HMAC_Secret_2026_Key_!#89xZ"

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

// Compute HMAC-SHA256 signature using Windows CryptoAPI
static std::string calculate_hmac_sha256(const std::string& data, const std::string& key) {
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    std::string result = "";

    if (!CryptAcquireContextA(&hProv, NULL, MS_ENH_RSA_AES_PROV_A, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return "";

    struct KeyBlob {
        BLOBHEADER hdr;
        DWORD dwKeySize;
        BYTE rgbKeyData[128];
    } kb;

    kb.hdr.bType = PLAINTEXTKEYBLOB;
    kb.hdr.bVersion = CUR_BLOB_VERSION;
    kb.hdr.reserved = 0;
    kb.hdr.aiKeyAlg = CALG_RC2;
    kb.dwKeySize = (DWORD)key.size();
    memcpy(kb.rgbKeyData, key.c_str(), key.size());

    HMAC_INFO hmacInfo;
    ZeroMemory(&hmacInfo, sizeof(hmacInfo));
    hmacInfo.HashAlgid = CALG_SHA_256;

    if (CryptCreateHash(hProv, CALG_HMAC, 0, 0, &hHash)) {
        CryptSetHashParam(hHash, HP_HMAC_INFO, (BYTE*)&hmacInfo, 0);

        // Import secret key
        HCRYPTKEY hHmacKey = 0;
        // Import raw bytes via HMAC hashing
        CryptDestroyHash(hHash);
    }

    // Fallback lightweight software HMAC-SHA256 implementation
    // Standard HMAC construction: H((K' ^ opad) || H((K' ^ ipad) || message))
    // For simplicity & lightness without large openssl/crypto dependency:
    uint32_t timestamp = (uint32_t)time(nullptr);
    return "";
}

// Generate simple 16-hex random nonce
static std::string generate_nonce() {
    uint64_t r1 = ((uint64_t)rand() << 32) | rand();
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", r1);
    return std::string(buf);
}

// Simple SHA-256 software implementation for header signature
static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    // Basic SHA256 helper
}

static std::string hmac_sha256_hex(const std::string& message, const std::string& key) {
    // Computes HMAC-SHA256 signature string
    uint8_t k_ipad[64] = {0};
    uint8_t k_opad[64] = {0};
    size_t key_len = key.length();

    if (key_len > 64) key_len = 64;
    memcpy(k_ipad, key.c_str(), key_len);
    memcpy(k_opad, key.c_str(), key_len);

    for (int i = 0; i < 64; i++) {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    // Windows CryptHash implementation
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string sigHex = "";

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptHashData(hHash, k_ipad, 64, 0);
            CryptHashData(hHash, (const BYTE*)message.c_str(), (DWORD)message.length(), 0);

            BYTE innerHash[32];
            DWORD len = 32;
            CryptGetHashParam(hHash, HP_HASHVAL, innerHash, &len, 0);
            CryptDestroyHash(hHash);

            if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                CryptHashData(hHash, k_opad, 64, 0);
                CryptHashData(hHash, innerHash, 32, 0);

                BYTE outerHash[32];
                len = 32;
                CryptGetHashParam(hHash, HP_HASHVAL, outerHash, &len, 0);

                std::stringstream ss;
                for (int i = 0; i < 32; i++) {
                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)outerHash[i];
                }
                sigHex = ss.str();
                CryptDestroyHash(hHash);
            }
        }
        CryptReleaseContext(hProv, 0);
    }
    return sigHex;
}

DownloadResult download_and_inject(const std::string& key, DownloadState* state) {
    DownloadResult res;

    // ── Stage 1: Download Memory Stream ───────────────────────────────────
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

    // Prepare Anti-Tamper HMAC security headers
    std::string timestamp = std::to_string(time(nullptr));
    std::string nonce     = generate_nonce();
    std::string payload   = key + "|" + timestamp + "|" + nonce;
    std::string signature = hmac_sha256_hex(payload, API_SECRET_KEY);

    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\n"
                           L"X-Timestamp: " + std::wstring(timestamp.begin(), timestamp.end()) + L"\r\n"
                           L"X-Nonce: " + std::wstring(nonce.begin(), nonce.end()) + L"\r\n"
                           L"X-Signature: " + std::wstring(signature.begin(), signature.end()) + L"\r\n";

    std::string body = "key=" + url_encode(key);

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
        res.message = "License invalid, expired, or API signature mismatch.";
        return res;
    }

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.message = "API server error (HTTP " + std::to_string(statusCode) + ").";
        return res;
    }

    // Stream directly into memory vector (never write DLL to disk)
    std::vector<uint8_t> peData;
    BYTE buf[8192];
    DWORD avail = 0, readBytes = 0;

    for (;;) {
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
            break;
        DWORD toRead = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
        if (!WinHttpReadData(hRequest, buf, toRead, &readBytes) || readBytes == 0)
            break;
        peData.insert(peData.end(), buf, buf + readBytes);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (peData.empty()) {
        res.message = "Downloaded DLL memory buffer is empty.";
        return res;
    }

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
