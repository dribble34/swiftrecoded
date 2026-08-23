#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include "../nt_types.hpp"

namespace crypto
{
    constexpr int AES_KEY_SIZE   = 32;
    constexpr int AES_BLOCK_SIZE = 16;
    constexpr int AES_IV_SIZE    = 16;
    constexpr int SHA256_SIZE    = 32;

    using Key = std::array<uint8_t, AES_KEY_SIZE>;
    using IV  = std::array<uint8_t, AES_IV_SIZE>;

    std::vector<uint8_t> decrypt( const std::vector<uint8_t>& cipher,
                                   const Key& key, const IV& iv );
    void xor_layer( std::vector<uint8_t>& data, const std::string& hwid );
    Key  derive_key( const std::string& hwid, const std::string& salt );
    IV   derive_iv ( const std::string& hwid, const std::string& salt );
    std::array<uint8_t, SHA256_SIZE> sha256( const void* data, size_t len );
}
