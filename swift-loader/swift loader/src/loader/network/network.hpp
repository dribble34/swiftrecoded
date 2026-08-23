#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdint>
#include "../nt_types.hpp"
#include <string>
#include <cstdint>

namespace network
{
    struct DownloadResult
    {
        bool                 ok{};
        std::vector<uint8_t> data{};
        std::string          error{};
    };


    DownloadResult download(const wchar_t* host, const wchar_t* path, uint16_t port = 443);



    std::string authenticate(const wchar_t* host, const std::string& hwid_short);
}
