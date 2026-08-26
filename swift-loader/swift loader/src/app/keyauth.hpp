#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include "loader/network/network.hpp"
#include "prot/webhook_report.hpp"
#include "prot/ethera_prot.hpp"

namespace keyauth {

struct Result {
    bool success       = false;
    bool hwid_ok       = false;
    bool sub_ok        = false;
    bool server_ok     = false;
    std::string message;
    std::string expiry;
    std::string session_token;
    std::string session_key_hex;
    std::string cnonce;
    std::string snonce;
};

inline Result g_last_verify_result{};

static std::string get_hwid() {
    DWORD serial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    char vol[16]; snprintf(vol, sizeof vol, "%08X", serial);

    std::string guid;
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
        char buf[64] = {}; DWORD sz = sizeof buf;
        RegQueryValueExA(hk, "MachineGuid", nullptr, nullptr, (LPBYTE)buf, &sz);
        RegCloseKey(hk);
        for (char& c : buf) if (c == '-') c = '_';
        guid = buf;
    }

    std::string hwid = std::string(vol) + "_" + guid;
    while (hwid.size() < 20) hwid += "0";
    return hwid;
}

struct VerifyCallbacks {
    std::function<void()> on_hwid_done;
    std::function<void()> on_sub_done;
    std::function<void()> on_server_done;
};

static Result verify(const std::string& key, const VerifyCallbacks& cb) {
    Result res;

    std::string hwid = get_hwid();
    if (cb.on_hwid_done) cb.on_hwid_done();

    auto auth_res = network::authenticate(key, hwid);

    if (cb.on_sub_done) cb.on_sub_done();

    res.server_ok       = true;
    res.hwid_ok         = auth_res.success;
    res.sub_ok          = auth_res.success;
    res.success         = auth_res.success;
    res.message         = auth_res.message;
    res.expiry          = auth_res.expiry;
    res.session_token   = auth_res.token;
    res.cnonce          = auth_res.cnonce;
    res.snonce          = auth_res.snonce;
    res.session_key_hex = auth_res.session_key_hex;

    g_last_verify_result = res;

    if (cb.on_server_done) cb.on_server_done();

    return res;
}

} // namespace keyauth
