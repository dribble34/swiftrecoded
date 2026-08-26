#include "crypto.hpp"
#include "../config.hpp"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include <cstring>
#include <algorithm>
#include "VMProtectSDK.h"

namespace crypto
{

void xor_layer( std::vector<uint8_t>& data, const std::string& key )
{
    if ( key.empty() ) return;
    const size_t klen = key.size();
    for ( size_t i = 0; i < data.size(); ++i )
        data[ i ] ^= static_cast<uint8_t>( key[ i % klen ] );
}

void secure_zero( void* ptr, size_t len )
{
    if ( ptr && len )
    {
        RtlSecureZeroMemory( ptr, len );
    }
}

// ─── SHA-256 ─────────────────────────────────────────────────────────────────

std::array<uint8_t, SHA256_SIZE> sha256( const void* data, size_t len )
{
    std::array<uint8_t, SHA256_SIZE> result{};

    BCRYPT_ALG_HANDLE  alg  = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    do {
        if ( !BCRYPT_SUCCESS( BCryptOpenAlgorithmProvider(
                &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) ) )
            break;
        if ( !BCRYPT_SUCCESS( BCryptCreateHash(
                alg, &hash, nullptr, 0, nullptr, 0, 0 ) ) )
            break;
        BCryptHashData( hash,
            const_cast<PUCHAR>( reinterpret_cast<const uint8_t*>( data ) ),
            static_cast<ULONG>( len ), 0 );
        BCryptFinishHash( hash, result.data(),
            static_cast<ULONG>( result.size() ), 0 );
    } while ( false );

    if ( hash ) BCryptDestroyHash( hash );
    if ( alg  ) BCryptCloseAlgorithmProvider( alg, 0 );
    return result;
}

// ─── AES-256-CBC decrypt ─────────────────────────────────────────────────────

std::vector<uint8_t> decrypt( const std::vector<uint8_t>& cipher,
                               const Key& key, const IV& iv )
{
    if ( cipher.empty() || cipher.size() % AES_BLOCK_SIZE != 0 )
        return {};

    BCRYPT_ALG_HANDLE alg   = nullptr;
    BCRYPT_KEY_HANDLE hkey  = nullptr;
    std::vector<uint8_t> plain( cipher.size() );

    do {
        if ( !BCRYPT_SUCCESS( BCryptOpenAlgorithmProvider(
                &alg, BCRYPT_AES_ALGORITHM, nullptr, 0 ) ) )
            break;

        if ( !BCRYPT_SUCCESS( BCryptSetProperty( alg, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(
                    const_cast<wchar_t*>( BCRYPT_CHAIN_MODE_CBC ) ),
                static_cast<ULONG>( sizeof( BCRYPT_CHAIN_MODE_CBC ) ), 0 ) ) )
            break;

        struct { BCRYPT_KEY_DATA_BLOB_HEADER hdr; uint8_t kd[ AES_KEY_SIZE ]; } blob;
        blob.hdr.dwMagic   = BCRYPT_KEY_DATA_BLOB_MAGIC;
        blob.hdr.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        blob.hdr.cbKeyData = AES_KEY_SIZE;
        memcpy( blob.kd, key.data(), AES_KEY_SIZE );

        if ( !BCRYPT_SUCCESS( BCryptImportKey( alg, nullptr, BCRYPT_KEY_DATA_BLOB,
                &hkey, nullptr, 0,
                reinterpret_cast<PUCHAR>( &blob ), sizeof( blob ), 0 ) ) )
            break;

        IV iv_copy = iv;
        ULONG written = 0;

        if ( !BCRYPT_SUCCESS( BCryptDecrypt( hkey,
                const_cast<PUCHAR>( cipher.data() ),
                static_cast<ULONG>( cipher.size() ),
                nullptr,
                iv_copy.data(), static_cast<ULONG>( iv_copy.size() ),
                plain.data(), static_cast<ULONG>( plain.size() ),
                &written, 0 ) ) )
            break;

        // Strip PKCS#7 padding
        if ( written == 0 || written > plain.size() ) { plain.clear(); break; }
        const uint8_t pad = plain[ written - 1 ];
        if ( pad < 1 || pad > AES_BLOCK_SIZE ) { plain.clear(); break; }
        plain.resize( written - pad );

    } while ( false );

    if ( hkey ) BCryptDestroyKey( hkey );
    if ( alg  ) BCryptCloseAlgorithmProvider( alg, 0 );
    return plain;
}

std::vector<uint8_t> decrypt_swift_payload( const std::vector<uint8_t>& encrypted,
                                             const std::string& key,
                                             const std::string& hwid,
                                             const std::string& token,
                                             const std::string& cnonce,
                                             const std::string& snonce )
{
    VMProtectBeginUltra("decrypt_swift_payload");
    if ( encrypted.empty() || token.empty() ) return {};

    // 1. Derive AES key: SHA-256("swift_key_" . hwid . "_" . key . "_" . token . "_" . cnonce . "_" . snonce)
    std::string key_seed = "swift_key_" + hwid + "_" + key + "_" + token + "_" + cnonce + "_" + snonce;
    auto key_bytes = sha256(key_seed.data(), key_seed.size());

    Key aes_key{};
    memcpy(aes_key.data(), key_bytes.data(), AES_KEY_SIZE);

    // 2. Derive AES IV: first 16 bytes of SHA-256("swift_iv_" . snonce . "_" . cnonce . "_" . hwid)
    std::string iv_seed = "swift_iv_" + snonce + "_" + cnonce + "_" + hwid;
    auto iv_bytes = sha256(iv_seed.data(), iv_seed.size());

    IV aes_iv{};
    memcpy(aes_iv.data(), iv_bytes.data(), AES_IV_SIZE);

    // 3. AES-256-CBC Decrypt
    std::vector<uint8_t> xored = decrypt(encrypted, aes_key, aes_iv);
    if (xored.empty()) {
        VMProtectEnd();
        return {};
    }

    // 4. Undo XOR layer using (hwid . token)
    std::string xor_seed = hwid + token;
    xor_layer(xored, xor_seed);

    secure_zero(aes_key.data(), aes_key.size());
    secure_zero(aes_iv.data(), aes_iv.size());

    VMProtectEnd();
    return xored;
}

} // namespace crypto
