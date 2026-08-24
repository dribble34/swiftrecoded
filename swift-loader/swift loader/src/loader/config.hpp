#pragma once
#include "obfuscation.hpp"

#define CFG_HOST            WOBF(L"api.swiftfly.xyz")
#define CFG_LOADER_PATH     WOBF(L"/loader/loader.dll")
#define CFG_DLL_PATH        WOBF(L"/download.php")
#define CFG_AUTH_PATH       WOBF(L"/verify.php")
#define CFG_PORT            443

#define CFG_XOR_KEY         0x9F2A4B8C1D6E3F70ULL

#define CFG_KEY_SALT        "swiftfly_dll_key_v2_"
#define CFG_IV_SALT         "swiftfly_dll_iv__v2_"

#define CFG_MUTEX_NAME      WOBF(L"Local\\{A3F8C2D1-4E7B-49A0-B5C3-D2E1F6A8B9C0}")

#define CFG_WINDOW_TITLE    WOBF(L"System Update Service")
#define CFG_WINDOW_CLASS    WOBF(L"SystemSvc")

#define CFG_TIMING_LIMIT    3'000'000ULL

#define CFG_EXPECTED_HASH { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}
