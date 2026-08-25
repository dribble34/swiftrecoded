#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <cstdint>
#include "rsa_public_key.hpp"
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

// Verifies RSA-2048 SHA-256 signature of data using embedded server public key
static bool verify_signature(const uint8_t* data, size_t data_len, const std::string& base64_sig) {
    if (!data || data_len == 0 || base64_sig.empty()) return false;

    std::vector<uint8_t> sigBytes = base64_decode(base64_sig);
    if (sigBytes.empty() || sigBytes.size() != 256) return false; // RSA 2048-bit signature is exactly 256 bytes

    // Reverse signature bytes because CryptoAPI expects Little-Endian RSA signature
    std::vector<uint8_t> reversedSig(sigBytes.rbegin(), sigBytes.rend());

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return false;
    }

    HCRYPTKEY hKey = 0;
    if (!CryptImportKey(hProv, RSA_SERVER_PUBLIC_KEY, (DWORD)RSA_SERVER_PUBLIC_KEY_LEN, 0, 0, &hKey)) {
        CryptReleaseContext(hProv, 0);
        return false;
    }

    HCRYPTHASH hHash = 0;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return false;
    }

    if (!CryptHashData(hHash, data, (DWORD)data_len, 0)) {
        CryptDestroyHash(hHash);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return false;
    }

    BOOL verified = CryptVerifySignatureW(hHash, reversedSig.data(), (DWORD)reversedSig.size(), hKey, nullptr, 0);

    CryptDestroyHash(hHash);
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);

    return verified == TRUE;
}

static bool verify_string_signature(const std::string& text, const std::string& base64_sig) {
    return verify_signature(reinterpret_cast<const uint8_t*>(text.data()), text.size(), base64_sig);
}

} // namespace rsa_verify
