#pragma once
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <string>
#include <functional>
#include <random>
#include "loader/crypto/rsa_verify.hpp"
#include "prot/webhook_report.hpp"
#pragma comment(lib, "winhttp.lib")

namespace keyauth {

struct Result {
    bool success       = false;
    bool hwid_ok       = false;
    bool sub_ok        = false;
    bool server_ok     = false;
    std::string message;
    std::string expiry;
    std::string session_token;
    std::string cnonce;
    std::string snonce;
};

// Global last result storage for download phase
static Result g_last_verify_result{};

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

static std::string generate_random_nonce() {
    static const char hexChars[] = "0123456789abcdef";
    std::string nonce;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    for (int i = 0; i < 32; ++i) {
        nonce += hexChars[dis(gen)];
    }
    return nonce;
}

// ── HTTP POST to our own API ──────────────────────────────────────────────────
static std::string api_post(const wchar_t* host, const wchar_t* path, const std::string& body) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return {};
    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return {}; }
    HINTERNET hR = WinHttpOpenRequest(hC, L"POST", path, nullptr,
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

// HWID: combine volume serial + machine GUID (always >=20 chars)
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
        for (char& c : buf) if (c == '-') c = '_'; // keep URL-safe
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

// Main blocking verify — run on a background thread.
static Result verify(const std::string& key, const VerifyCallbacks& cb) {
    Result res;

    // ── HWID collection ──────────────────────────────────────────────────
    std::string hwid = get_hwid();
    if (cb.on_hwid_done) cb.on_hwid_done();

    // ── Generate Client Nonce ─────────────────────────────────────────────
    std::string cnonce = generate_random_nonce();

    // ── Call our API endpoint ────────────────────────────────────────────
    std::string body =
        std::string("key=") + url_encode(key)
        + "&hwid=" + url_encode(hwid)
        + "&cnonce=" + url_encode(cnonce);

    std::string resp = api_post(L"api.swiftfly.xyz", L"/verify.php", body);

    if (cb.on_sub_done) cb.on_sub_done();

    if (resp.empty()) {
        res.message = "Cannot reach server.";
        if (cb.on_server_done) cb.on_server_done();
        return res;
    }

    // ── Parse Response & Check RSA Signature ─────────────────────────────
    bool ok = (json_val(resp, "success") == "true");
    std::string error        = json_val(resp, "error");
    std::string expiry       = json_val(resp, "expiry");
    std::string sessionToken = json_val(resp, "session_token");
    std::string snonce       = json_val(resp, "snonce");
    std::string ts           = json_val(resp, "ts");
    std::string signature    = json_val(resp, "signature");

    if (ok) {
        // Construct canonical string to verify server's RSA signature
        std::string signPayload = "success=1&key=" + key + "&hwid=" + hwid +
                                  "&session_token=" + sessionToken +
                                  "&cnonce=" + cnonce + "&snonce=" + snonce + "&ts=" + ts;

        if (!rsa_verify::verify_string_signature(signPayload, signature)) {
            // RSA signature failed! Security attack / spoof detected!
            webhook_report::report_incident_and_die("API Spoofing / Tampering Attempt: RSA Signature Mismatch on /verify.php");
            res.success = false;
            res.message = "Security error: Invalid server RSA signature (Spoof attempt detected).";
            if (cb.on_server_done) cb.on_server_done();
            return res;
        }

        res.session_token = sessionToken;
        res.cnonce        = cnonce;
        res.snonce        = snonce;
    }

    res.server_ok = true;
    res.hwid_ok   = ok;
    res.sub_ok    = ok;
    res.expiry    = expiry;
    res.success   = ok;
    res.message   = ok ? "OK" : (error.empty() ? "Invalid key." : error);

    g_last_verify_result = res;

    if (cb.on_server_done) cb.on_server_done();
    return res;
}

} // namespace keyauth

