#include "network.hpp"
#include "../obfuscation.hpp"
#include "nt_types.hpp"
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <sstream>

namespace network
{

DownloadResult download(const wchar_t* host, const wchar_t* path, uint16_t port)
{
    DownloadResult res{};

    HINTERNET session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { res.error = "WinHttpOpen failed"; return res; }

    HINTERNET conn = WinHttpConnect(session, host, port, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        res.error = "WinHttpConnect failed";
        return res;
    }

    HINTERNET req = WinHttpOpenRequest(
        conn, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!req)
    {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.error = "WinHttpOpenRequest failed";
        return res;
    }


    DWORD opts = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
               | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURE_PROTOCOLS, &opts, sizeof(opts));

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr))
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.error = "request failed";
        return res;
    }


    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200)
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        res.error = "HTTP " + std::to_string(status);
        return res;
    }


    DWORD available = 0, downloaded = 0;
    do {
        WinHttpQueryDataAvailable(req, &available);
        if (!available) break;

        size_t offset = res.data.size();
        res.data.resize(offset + available);
        WinHttpReadData(req, res.data.data() + offset, available, &downloaded);
        res.data.resize(offset + downloaded);
    } while (available > 0);

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    res.ok = !res.data.empty();
    return res;
}

std::string authenticate(const wchar_t* host, const std::string& hwid_short)
{

    HINTERNET session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {};

    HINTERNET conn = WinHttpConnect(session, host, 443, 0);
    if (!conn) { WinHttpCloseHandle(session); return {}; }

    HINTERNET req = WinHttpOpenRequest(
        conn, L"POST", L"/auth", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!req)
    {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return {};
    }

    DWORD opts = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
               | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURE_PROTOCOLS, &opts, sizeof(opts));

    std::string body = "hwid=" + hwid_short;
    WinHttpAddRequestHeaders(req,
        L"Content-Type: application/x-www-form-urlencoded\r\n", -1L,
        WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(req,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            const_cast<char*>(body.c_str()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(req, nullptr))
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return {};
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
        WINHTTP_NO_HEADER_INDEX);

    std::string token{};
    if (status == 200)
    {
        DWORD avail = 0, read = 0;
        WinHttpQueryDataAvailable(req, &avail);
        if (avail > 0 && avail < 512)
        {
            token.resize(avail);
            WinHttpReadData(req, token.data(), avail, &read);
            token.resize(read);
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return token;
}

}
