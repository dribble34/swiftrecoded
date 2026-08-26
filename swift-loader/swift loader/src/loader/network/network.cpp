#include "network.hpp"
#include "config.hpp"
#include "VMProtectSDK.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <ctime>
#include <random>

namespace network
{

static std::string url_encode(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)c;
        } else {
            char h[4];
            snprintf(h, sizeof h, "%%%02X", c);
            o += h;
        }
    }
    return o;
}

static std::string json_extract(const std::string& json, const std::string& key)
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
        size_t start = pos + 1;
        size_t end = json.find('"', start);
        if (end == std::string::npos) return {};
        return json.substr(start, end - start);
    }
    else
    {
        size_t end = json.find_first_of(",}\r\n", pos);
        if (end == std::string::npos) end = json.size();
        std::string val = json.substr(pos, end - pos);
        size_t s = val.find_first_not_of(" \t");
        size_t e = val.find_last_not_of(" \t");
        return (s == std::string::npos) ? "" : val.substr(s, e - s + 1);
    }
}

static std::string make_nonce()
{
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

static void write_debug_log(const std::string& msg)
{
    std::ofstream log("swift_debug.log", std::ios::app);
    if (log.is_open())
    {
        std::time_t t = std::time(nullptr);
        char ts[64];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        log << "[" << ts << "] " << msg << "\n";
    }
}

AuthResult authenticate(const std::string& key, const std::string& hwid)
{
    VMProtectBeginUltra("authenticate");
    AuthResult res{};

    write_debug_log("Starting authentication for key: " + (key.length() > 8 ? key.substr(0, 8) + "..." : key) + " HWID: " + hwid);

    HINTERNET session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        DWORD err = GetLastError();
        res.message = "WinHttpOpen failed (Error " + std::to_string(err) + ")";
        write_debug_log("[FAIL] " + res.message);
        VMProtectEnd();
        return res;
    }

    HINTERNET conn = WinHttpConnect(session, CFG_HOST, CFG_PORT, 0);
    if (!conn)
    {
        DWORD err = GetLastError();
        WinHttpCloseHandle(session);
        res.message = "WinHttpConnect failed (Error " + std::to_string(err) + ")";
        write_debug_log("[FAIL] " + res.message);
        VMProtectEnd();
        return res;
    }

    HINTERNET req = WinHttpOpenRequest(
        conn, L"POST", CFG_AUTH_PATH, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!req)
    {
        DWORD err = GetLastError();
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.message = "WinHttpOpenRequest failed (Error " + std::to_string(err) + ")";
        write_debug_log("[FAIL] " + res.message);
        VMProtectEnd();
        return res;
    }

    std::string cnonce = make_nonce();
    std::string body_str = "key=" + url_encode(key) +
                           "&hwid=" + url_encode(hwid) +
                           "&cnonce=" + url_encode(cnonce);

    WinHttpAddRequestHeaders(req,
        L"Content-Type: application/x-www-form-urlencoded\r\n", -1L,
        WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(req,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            const_cast<char*>(body_str.c_str()),
            static_cast<DWORD>(body_str.size()),
            static_cast<DWORD>(body_str.size()), 0) ||
        !WinHttpReceiveResponse(req, nullptr))
    {
        DWORD err = GetLastError();
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.message = "Network request failed (WinHTTP Error " + std::to_string(err) + ")";
        write_debug_log("[FAIL] " + res.message);
        VMProtectEnd();
        return res;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
        WINHTTP_NO_HEADER_INDEX);

    std::string resp_text;
    DWORD avail = 0, read = 0;
    do {
        WinHttpQueryDataAvailable(req, &avail);
        if (!avail) break;
        std::string buf(avail, '\0');
        WinHttpReadData(req, &buf[0], avail, &read);
        buf.resize(read);
        resp_text += buf;
    } while (avail > 0);

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    write_debug_log("Server Response (HTTP " + std::to_string(status) + "): " + resp_text);

    bool is_success = (json_extract(resp_text, "success") == "true");

    if (is_success)
    {
        res.success         = true;
        res.message         = "OK";
        res.token           = json_extract(resp_text, "session_token");
        res.cnonce          = json_extract(resp_text, "cnonce");
        res.snonce          = json_extract(resp_text, "snonce");
        res.expiry          = json_extract(resp_text, "expiry");
        write_debug_log("[SUCCESS] Authenticated successfully. Expiry: " + res.expiry);
    }
    else
    {
        res.success = false;
        std::string err_msg = json_extract(resp_text, "error");
        res.message = err_msg.empty() ? ("Server HTTP " + std::to_string(status)) : err_msg;
        write_debug_log("[FAIL] Auth status failure: " + res.message);
    }

    VMProtectEnd();
    return res;
}

PayloadResult download_payload(const std::string& key, const std::string& hwid, const std::string& token, const std::string& cnonce, const std::string& snonce)
{
    VMProtectBeginUltra("download_payload");
    PayloadResult res{};

    if (token.empty() || hwid.empty())
    {
        res.error = "Invalid token or HWID";
        VMProtectEnd();
        return res;
    }

    HINTERNET session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { res.error = "WinHttpOpen failed"; VMProtectEnd(); return res; }

    HINTERNET conn = WinHttpConnect(session, CFG_HOST, CFG_PORT, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        res.error = "WinHttpConnect failed";
        VMProtectEnd();
        return res;
    }

    HINTERNET req = WinHttpOpenRequest(
        conn, L"POST", CFG_PAYLOAD_PATH, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!req)
    {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.error = "WinHttpOpenRequest failed";
        VMProtectEnd();
        return res;
    }

    std::string body_str = "key=" + url_encode(key) +
                           "&hwid=" + url_encode(hwid) +
                           "&session_token=" + url_encode(token) +
                           "&cnonce=" + url_encode(cnonce) +
                           "&snonce=" + url_encode(snonce);

    WinHttpAddRequestHeaders(req,
        L"Content-Type: application/x-www-form-urlencoded\r\n", -1L,
        WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            const_cast<char*>(body_str.c_str()),
                            static_cast<DWORD>(body_str.size()),
                            static_cast<DWORD>(body_str.size()), 0) ||
        !WinHttpReceiveResponse(req, nullptr))
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.error = "Download request failed";
        VMProtectEnd();
        return res;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
        WINHTTP_NO_HEADER_INDEX);

    if (status != 200)
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.error = "Download failed (HTTP " + std::to_string(status) + ")";
        VMProtectEnd();
        return res;
    }

    BYTE buf[8192];
    DWORD avail = 0, readBytes = 0;
    for (;;) {
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
            break;
        DWORD toRead = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
        if (!WinHttpReadData(req, buf, toRead, &readBytes) || readBytes == 0)
            break;
        res.encrypted_data.insert(res.encrypted_data.end(), buf, buf + readBytes);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (res.encrypted_data.empty()) {
        res.error = "Downloaded payload buffer is empty";
        VMProtectEnd();
        return res;
    }

    res.success = true;
    VMProtectEnd();
    return res;
}

} // namespace network
