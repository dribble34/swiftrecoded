// ── keyauth.hpp ───────────────────────────────────────────────────────────────
#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <functional>
#pragma comment(lib, "winhttp.lib")

#define KA_APP_NAME  "swiftauth"
#define KA_OWNERID   "755TbEbXn6"
#define KA_VERSION   "1.0"
#define KA_HOST      L"keyauth.win"

namespace keyauth {

struct Result {
    bool success   = false;
    bool hwid_ok   = false;
    bool sub_ok    = false;
    bool server_ok = false;
    std::string message;
    std::string expiry;
};

// ── Utilities ─────────────────────────────────────────────────────────────────
static std::string url_encode(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') { o+=(char)c; }
        else { char h[4]; snprintf(h,sizeof h,"%%%02X",c); o+=h; }
    }
    return o;
}

static std::string json_val(const std::string& j, const std::string& key) {
    std::string k = "\"" + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return {};
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return {};
    while (++p < j.size() && (j[p]==' '||j[p]=='\t'));
    if (p >= j.size()) return {};
    if (j[p] == '"') {
        size_t s = p + 1, e = j.find('"', s);
        return e == std::string::npos ? "" : j.substr(s, e - s);
    } else {
        size_t e = j.find_first_of(",}\r\n", p);
        if (e == std::string::npos) e = j.size();
        std::string v = j.substr(p, e - p);
        size_t a = v.find_first_not_of(" \t");
        size_t b = v.find_last_not_of(" \t");
        return a == std::string::npos ? "" : v.substr(a, b - a + 1);
    }
}

static std::string http_post(const std::string& body) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return {};
    HINTERNET hC = WinHttpConnect(hS, KA_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return {}; }
    HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/api/1.3/", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return {}; }
    const wchar_t* hd = L"Content-Type: application/x-www-form-urlencoded\r\n";
    BOOL ok = WinHttpSendRequest(hR, hd, (DWORD)-1,
        (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hR, nullptr);
    std::string resp;
    if (ok) {
        char buf[8192]; DWORD dl = 0;
        do {
            DWORD av = 0;
            WinHttpQueryDataAvailable(hR, &av);
            if (!av) break;
            if (av > sizeof buf - 1) av = sizeof buf - 1;
            WinHttpReadData(hR, buf, av, &dl);
            buf[dl] = 0; resp += buf;
        } while (dl > 0);
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return resp;
}

static bool can_reach_server() {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;
    HINTERNET hC = WinHttpConnect(hS, KA_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    bool ok = (hC != nullptr);
    if (hC) WinHttpCloseHandle(hC);
    WinHttpCloseHandle(hS);
    return ok;
}

// HWID: combine volume serial + machine GUID (always >=20 chars)
static std::string get_hwid() {
    // Volume serial of C drive
    DWORD serial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    char vol[16]; snprintf(vol, sizeof vol, "%08X", serial);

    // Part 2: machine GUID from registry (unique per install)
    std::string guid;
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
        char buf[64] = {}; DWORD sz = sizeof buf;
        RegQueryValueExA(hk, "MachineGuid", nullptr, nullptr, (LPBYTE)buf, &sz);
        RegCloseKey(hk);
        for (char& c : buf) if (c == '-') c = '_'; // keep URL-safe
        guid = buf;
    }

    std::string hwid = std::string(vol) + "_" + guid;
    // Ensure at least 20 chars
    while (hwid.size() < 20) hwid += "0";
    return hwid;
}

struct VerifyCallbacks {
    std::function<void()> on_hwid_done;
    std::function<void()> on_sub_done;
    std::function<void()> on_server_done;
};

// Main blocking verify — run on a background thread.
static Result verify(const std::string& key, const VerifyCallbacks& cb) {
    Result res;

    // ── Server reachability ───────────────────────────────────────────────
    res.server_ok = can_reach_server();
    if (!res.server_ok) {
        res.message = "Cannot reach server.";
        if (cb.on_hwid_done)   cb.on_hwid_done();
        if (cb.on_sub_done)    cb.on_sub_done();
        if (cb.on_server_done) cb.on_server_done();
        return res;
    }

    // ── Session init ──────────────────────────────────────────────────────
    char sessid[32];
    snprintf(sessid, sizeof sessid, "%08X%08X",
        (DWORD)GetTickCount(), (DWORD)GetCurrentProcessId());

    std::string init_body =
        std::string("type=init")
        + "&ver="      + url_encode(KA_VERSION)
        + "&name="     + url_encode(KA_APP_NAME)
        + "&ownerid="  + url_encode(KA_OWNERID)
        + "&sessionid="+ url_encode(sessid);

    std::string init_resp = http_post(init_body);
    bool init_ok = (json_val(init_resp, "success") == "true");
    std::string sid = json_val(init_resp, "sessionid");

    if (!init_ok || sid.empty()) {
        res.message = "Init failed: " + json_val(init_resp, "message");
        if (cb.on_hwid_done)   cb.on_hwid_done();
        if (cb.on_sub_done)    cb.on_sub_done();
        if (cb.on_server_done) cb.on_server_done();
        return res;
    }

    // ── HWID callback (shown while license call runs) ─────────────────────
    if (cb.on_hwid_done) cb.on_hwid_done();

    // ── License check ─────────────────────────────────────────────────────
    std::string hwid = get_hwid();
    std::string lic_body =
        std::string("type=license")
        + "&key="       + url_encode(key)
        + "&hwid="      + url_encode(hwid)
        + "&sessionid=" + url_encode(sid)
        + "&name="      + url_encode(KA_APP_NAME)
        + "&ownerid="   + url_encode(KA_OWNERID);

    std::string lic_resp = http_post(lic_body);
    bool lic_ok  = (json_val(lic_resp, "success") == "true");
    std::string msg    = json_val(lic_resp, "message");
    std::string expiry = json_val(lic_resp, "expiry");

    if (cb.on_sub_done) cb.on_sub_done();

    res.hwid_ok  = lic_ok;
    res.sub_ok   = lic_ok;
    res.expiry   = expiry;
    res.success  = lic_ok;
    res.message  = msg.empty() ? (lic_ok ? "OK" : "Invalid key.") : msg;

    if (cb.on_server_done) cb.on_server_done();
    return res;
}

} // namespace keyauth
