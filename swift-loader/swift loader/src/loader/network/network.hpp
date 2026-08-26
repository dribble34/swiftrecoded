#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdint>

namespace network
{
    struct AuthResult
    {
        bool        success = false;
        std::string message;
        std::string token;
        std::string session_key_hex;
        std::string cnonce;
        std::string snonce;
        std::string expiry;
    };

    struct PayloadResult
    {
        bool                 success = false;
        std::vector<uint8_t> encrypted_data;
        std::string          error;
    };

    // Performs server-side auth check at /verify.php
    AuthResult authenticate(const std::string& key, const std::string& hwid);

    // Downloads encrypted payload with single-use token at /download.php
    PayloadResult download_payload(const std::string& key, const std::string& hwid, const std::string& token, const std::string& cnonce, const std::string& snonce);
}
