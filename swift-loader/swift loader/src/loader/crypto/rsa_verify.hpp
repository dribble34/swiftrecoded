#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <cstdint>
#include "rsa_public_key.hpp"
#include "prot/webhook_report.hpp"
#pragma comment(lib, "crypt32.lib")


namespace rsa_verify {

// Helper to base64 decode a string
static std::vector<uint8_t> base64_decode(const std::string& in) {
    DWORD outLen = 0;
    if (!CryptStringToBinaryA(in.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &outLen, nullptr, nullptr)) {
        return {};
    }
    std::vector<uint8_t> out(outLen);
    if (!CryptStringToBinaryA(in.c_str(), 0, CRYPT_STRING_BASE64, out.data(), &outLen, nullptr, nullptr)) {
        return {};
    }
    return out;
}

#ifndef PROV_RSA_AES
#define PROV_RSA_AES 24
#endif

static std::string unescape_json_string(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == '/') {
            out += '/';
            i++;
        } else if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == '\\') {
            out += '\\';
            i++;
        } else {
            out += in[i];
        }
    }
    return out;
}

// Verifies RSA-2048 SHA-256 signature of data using embedded server public key
static bool verify_signature(const uint8_t* data, size_t data_len, const std::string& base64_sig) {
    if (!data || data_len == 0 || base64_sig.empty()) {
        webhook_report::write_debug_log("[RSA Verify] FAIL: Null data, empty length or empty signature.");
        return false;
    }

    std::string cleanSig = unescape_json_string(base64_sig);
    std::vector<uint8_t> sigBytes = base64_decode(cleanSig);
    if (sigBytes.empty() || sigBytes.size() != 256) {
        webhook_report::write_debug_log("[RSA Verify] FAIL: Base64 decode failed or signature size != 256. RawLen=" + std::to_string(base64_sig.size()) + " CleanLen=" + std::to_string(cleanSig.size()) + " DecodedSize=" + std::to_string(sigBytes.size()));
        return false;
    }

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            webhook_report::write_debug_log("[RSA Verify] FAIL: CryptAcquireContextW failed. Error=" + std::to_string(GetLastError()));
            return false;
        }
    }

    HCRYPTKEY hKey = 0;
    if (!CryptImportKey(hProv, RSA_SERVER_PUBLIC_KEY, (DWORD)RSA_SERVER_PUBLIC_KEY_LEN, 0, 0, &hKey)) {
        webhook_report::write_debug_log("[RSA Verify] FAIL: CryptImportKey failed. Error=" + std::to_string(GetLastError()));
        CryptReleaseContext(hProv, 0);
        return false;
    }

    HCRYPTHASH hHash = 0;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        webhook_report::write_debug_log("[RSA Verify] FAIL: CryptCreateHash CALG_SHA_256 failed. Error=" + std::to_string(GetLastError()));
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return false;
    }

    if (!CryptHashData(hHash, data, (DWORD)data_len, 0)) {
        webhook_report::write_debug_log("[RSA Verify] FAIL: CryptHashData failed. Error=" + std::to_string(GetLastError()));
        CryptDestroyHash(hHash);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return false;
    }

    // 1. Try reversed (Little-Endian) signature bytes (Standard CryptoAPI format)
    std::vector<uint8_t> reversedSig(sigBytes.rbegin(), sigBytes.rend());
    BOOL verified = CryptVerifySignatureW(hHash, reversedSig.data(), (DWORD)reversedSig.size(), hKey, nullptr, 0);

    if (!verified) {
        DWORD err1 = GetLastError();
        // 2. Try original (Big-Endian) signature bytes as fallback
        verified = CryptVerifySignatureW(hHash, sigBytes.data(), (DWORD)sigBytes.size(), hKey, nullptr, 0);
        if (!verified) {
            DWORD err2 = GetLastError();
            webhook_report::write_debug_log("[RSA Verify] FAIL: CryptVerifySignatureW failed for both LE (err=" + std::to_string(err1) + ") and BE (err=" + std::to_string(err2) + ").");
        } else {
            webhook_report::write_debug_log("[RSA Verify] SUCCESS via Big-Endian fallback.");
        }
    } else {
        webhook_report::write_debug_log("[RSA Verify] SUCCESS via Little-Endian.");
    }

    CryptDestroyHash(hHash);
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);

    return verified == TRUE;
}

static bool verify_string_signature(const std::string& text, const std::string& base64_sig) {
    webhook_report::write_debug_log("[RSA Verify] Verifying payload string: " + text);
    webhook_report::write_debug_log("[RSA Verify] Signature: " + base64_sig);
    return verify_signature(reinterpret_cast<const uint8_t*>(text.data()), text.size(), base64_sig);
}

} // namespace rsa_verify

