#include "hwid.hpp"
#include "syscalls.hpp"
#include "../obfuscation.hpp"
#include "../crypto/crypto.hpp"
#include <intrin.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace hwid
{

static std::string cpu_id()
{
    int info[4]{};
    __cpuid(info, 0);
    std::ostringstream ss;
    ss << std::hex << info[1] << info[3] << info[2];
    if (info[0] >= 1) { __cpuid(info, 1); ss << info[0] << info[3]; }
    return ss.str();
}

static std::string disk_serial()
{
    struct IO_STATUS_BLOCK { NTSTATUS Status; ULONG_PTR Info; };

    struct FILE_FS_VOLUME_INFO
    {
        LARGE_INTEGER VolumeCreationTime;
        ULONG         VolumeSerialNumber;
        ULONG         VolumeLabelLength;
        BOOLEAN       SupportsObjects;
        WCHAR         VolumeLabel[1];
    };

    auto root = WOBF(L"\\??\\C:\\");
    UNICODE_STRING_NT us{ 14, 16, const_cast<wchar_t*>(root.str()) };
    OBJECT_ATTRIBUTES_NT oa{}; INIT_OA(oa, &us, OBJ_CASE_INSENSITIVE);
    IO_STATUS_BLOCK isb{};

    HANDLE h = nullptr;
    NTSTATUS st = syscalls::open_file(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &oa, &isb,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0x20 );
    if (st < 0 || !h) return "00000000";

    uint8_t buf[512]{};
    st = syscalls::query_volume_information_file(h, &isb, buf, sizeof(buf), 1);
    syscalls::close(h);

    if (st < 0) return "00000000";
    ULONG serial = reinterpret_cast<FILE_FS_VOLUME_INFO*>(buf)->VolumeSerialNumber;
    std::ostringstream ss;
    ss << std::hex << std::setw(8) << std::setfill('0') << serial;
    return ss.str();
}

static std::string machine_guid()
{
    auto key_path = WOBF(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography");
    UNICODE_STRING_NT kus;
    kus.Buffer = const_cast<wchar_t*>(key_path.str());
    kus.Length = (USHORT)(wcslen(key_path.str()) * sizeof(wchar_t));
    kus.MaximumLength = kus.Length + sizeof(wchar_t);

    OBJECT_ATTRIBUTES_NT oa{}; INIT_OA(oa, &kus, OBJ_CASE_INSENSITIVE);
    HANDLE hkey = nullptr;
    if (syscalls::open_key(&hkey, KEY_READ | KEY_WOW64_64KEY, &oa) < 0 || !hkey)
        return "00000000-0000-0000-0000-000000000000";

    auto val = WOBF(L"MachineGuid");
    UNICODE_STRING_NT vus;
    vus.Buffer = const_cast<wchar_t*>(val.str());
    vus.Length = (USHORT)(wcslen(val.str()) * sizeof(wchar_t));
    vus.MaximumLength = vus.Length + sizeof(wchar_t);

    uint8_t buf[512]{}; ULONG result_len = 0;
    NTSTATUS st = syscalls::query_value_key(hkey, &vus, 2, buf, sizeof(buf), &result_len);
    syscalls::close(hkey);

    if (st < 0) return "00000000-0000-0000-0000-000000000000";
    auto* kv = reinterpret_cast<KEY_VALUE_PARTIAL_INFO*>(buf);
    if (kv->Type != REG_SZ || kv->DataLength < 2)
        return "00000000-0000-0000-0000-000000000000";

    auto* ws = reinterpret_cast<wchar_t*>(kv->Data);
    std::string out;
    for (ULONG i = 0; i < kv->DataLength / 2; ++i)
    {
        if (!ws[i]) break;
        out += (char)(ws[i] & 0xFF);
    }
    return out;
}

std::string get()
{
    return cpu_id() + "|" + disk_serial() + "|" + machine_guid();
}

std::string get_short()
{
    std::string full = get();
    auto hash = crypto::sha256((const uint8_t*)full.c_str(), full.size());
    std::ostringstream ss;
    for (int i = 0; i < 8; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return ss.str();
}

}
