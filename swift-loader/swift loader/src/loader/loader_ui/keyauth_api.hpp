#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <vector>
#include <cstdint>
#pragma comment(lib, "winhttp.lib")

// ── KeyAuth credentials (from credentials.txt) ───────────────────────────────
#define KA_NAME     "swiftauth"
#define KA_OWNERID  "755TbEbXn6"
#define KA_SECRET   "fcbe6b49387d07dd4a143ac89229a3b01431fb1b8f5a627ce5a90a22b12e553a"
#define KA_VERSION  "1.0"
#define KA_URL      L"keyauth.win"
// ─────────────────────────────────────────────────────────────────────────────

namespace keyauth_api
{
    struct CheckResult
    {
        bool  success      = false;
        bool  hwid_ok      = false;
        bool  sub_ok       = false;
        bool  server_ok    = false;
        std::string message;        // human-readable detail
        std::string expiry;         // subscription expiry date string
    };

    // Internal: raw WinHTTP POST to KeyAuth API
    static std::string http_post(const std::wstring& host, const std::string& path,
                                  const std::string& body)
    {
        HINTERNET hSession = WinHttpOpen(
            L"Mozilla/5.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return {};

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }

        std::wstring wpath(path.begin(), path.end());
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {}; }

        // Allow self-signed certs for testing – in production remove this
        // DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA;
        // WinHttpSetOption(hReq, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

        const wchar_t* hdrs = L"Content-Type: application/x-www-form-urlencoded\r\n";
        BOOL ok = WinHttpSendRequest(hReq, hdrs, (DWORD)-1,
            (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
        if (!ok) { WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {}; }

        ok = WinHttpReceiveResponse(hReq, nullptr);
        if (!ok) { WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {}; }

        std::string result;
        DWORD downloaded = 0;
        char buf[4096];
        do {
            DWORD avail = 0;
            WinHttpQueryDataAvailable(hReq, &avail);
            if (!avail) break;
            if (avail > sizeof(buf) - 1) avail = sizeof(buf) - 1;
            WinHttpReadData(hReq, buf, avail, &downloaded);
            buf[downloaded] = '\0';
            result += buf;
        } while (downloaded > 0);

        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Minimal JSON field extractor (no external lib)
    static std::string json_get(const std::string& json, const std::string& key)
    {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return {};
        pos = json.find(':', pos + search.size());
        if (pos == std::string::npos) return {};
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

        if (pos < json.size() && json[pos] == '"')
        {
            // string value
            size_t start = pos + 1;
            size_t end = json.find('"', start);
            if (end == std::string::npos) return {};
            return json.substr(start, end - start);
        }
        else
        {
            // bool/number
            size_t end = json.find_first_of(",}\r\n", pos);
            if (end == std::string::npos) end = json.size();
            std::string val = json.substr(pos, end - pos);
            // trim
            size_t s = val.find_first_not_of(" \t");
            size_t e = val.find_last_not_of(" \t");
            return (s == std::string::npos) ? "" : val.substr(s, e - s + 1);
        }
    }

    // URL encode a string
    static std::string url_encode(const std::string& s)
    {
        std::string out;
        for (unsigned char c : s)
        {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += (char)c;
            else
            {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", c);
                out += hex;
            }
        }
        return out;
    }

    // Build a simple session ID (timestamp + random)
    static std::string make_session_id()
    {
        DWORD tick = GetTickCount();
        char buf[32];
        snprintf(buf, sizeof(buf), "%08X%08X", tick, GetCurrentProcessId());
        return buf;
    }

    // ── Public API ────────────────────────────────────────────────────────────

    // Step 1: init – establish session with KeyAuth
    static std::string ka_init()
    {
        std::string body =
            "type=init"
            "&ver=" + url_encode(KA_VERSION) +
            "&name=" + url_encode(KA_NAME) +
            "&ownerid=" + url_encode(KA_OWNERID) +
            "&sessionid=" + make_session_id();

        std::string resp = http_post(KA_URL, "/api/1.3/", body);
        return resp;
    }

    // Step 2: license – verify a license key
    static std::string ka_license(const std::string& sessionid, const std::string& key,
                                    const std::string& hwid)
    {
        std::string body =
            "type=license"
            "&key=" + url_encode(key) +
            "&hwid=" + url_encode(hwid) +
            "&sessionid=" + url_encode(sessionid) +
            "&name=" + url_encode(KA_NAME) +
            "&ownerid=" + url_encode(KA_OWNERID);

        return http_post(KA_URL, "/api/1.3/", body);
    }

    // Perform a simple connectivity check (ping keyauth.win)
    static bool server_reachable()
    {
        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        HINTERNET hConnect = WinHttpConnect(hSession, KA_URL,
            INTERNET_DEFAULT_HTTPS_PORT, 0);
        bool ok = (hConnect != nullptr);
        if (hConnect) WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return ok;
    }

    // ── Main verification function ────────────────────────────────────────────
    // Runs all three checks in order: HWID → Subscription → Server
    // Callbacks are called after each step so the UI can update.
    // Returns CheckResult with the final state.
    struct VerifyCallbacks
    {
        std::function<void()> on_hwid_done;       // called when hwid check finishes
        std::function<void()> on_sub_done;        // called when subscription check finishes
        std::function<void()> on_server_done;     // called when server check finishes
    };

    static CheckResult verify_key(const std::string& key,
                                   const std::string& hwid,
                                   const VerifyCallbacks& cbs)
    {
        CheckResult res;

        // ── Server reachability check ──────────────────────────────────────
        // (done first so we know if we can reach the API at all)
        res.server_ok = server_reachable();
        // Don't call on_server_done yet — we'll call it last per UI flow

        if (!res.server_ok)
        {
            res.message = "Cannot reach authentication server.";
            // Still call all callbacks so UI advances
            if (cbs.on_hwid_done)    cbs.on_hwid_done();
            if (cbs.on_sub_done)     cbs.on_sub_done();
            if (cbs.on_server_done)  cbs.on_server_done();
            return res;
        }

        // ── Init session ──────────────────────────────────────────────────
        std::string init_resp = ka_init();
        bool init_ok = (json_get(init_resp, "success") == "true");
        std::string sessionid = json_get(init_resp, "sessionid");

        if (!init_ok || sessionid.empty())
        {
            res.message = "Server initialisation failed: " + json_get(init_resp, "message");
            if (cbs.on_hwid_done)    cbs.on_hwid_done();
            if (cbs.on_sub_done)     cbs.on_sub_done();
            if (cbs.on_server_done)  cbs.on_server_done();
            return res;
        }

        // ── License verification (includes HWID + subscription) ───────────
        std::string lic_resp = ka_license(sessionid, key, hwid);
        bool lic_ok  = (json_get(lic_resp, "success") == "true");
        std::string msg     = json_get(lic_resp, "message");
        std::string expiry  = json_get(lic_resp, "expiry");

        // HWID check: KeyAuth will return "hwid mismatch" on HWID failure
        // We treat any success OR specific non-hwid errors as hwid_ok=true
        bool hwid_mismatch = (msg.find("hwid") != std::string::npos ||
                              msg.find("HWID") != std::string::npos);
        res.hwid_ok = lic_ok || !hwid_mismatch;

        if (cbs.on_hwid_done) cbs.on_hwid_done();

        // Subscription check: success means sub is active
        bool sub_expired = (msg.find("expir") != std::string::npos ||
                            msg.find("time") != std::string::npos);
        res.sub_ok  = lic_ok;
        res.expiry  = expiry;

        if (cbs.on_sub_done) cbs.on_sub_done();

        // Server check (already done above)
        if (cbs.on_server_done) cbs.on_server_done();

        res.success = lic_ok;
        res.message = msg.empty() ? (lic_ok ? "Authenticated successfully" : "Invalid license key") : msg;

        return res;
    }

} // namespace keyauth_api
