#pragma once
// nt_types.hpp — Windows NT type definitions for the swift loader
//
// Strategy: Both projects already pull in Windows.h (via network.hpp/crypto.hpp).
// Rather than fight include ordering, we ensure Windows.h runs first so its types
// establish the baseline. We then add only the NT-specific types the SDK omits.

#include <cstdint>
#include <cstring>
#include <algorithm>

// ─── Pull in Windows SDK types (idempotent — safe to call multiple times) ────
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINDOWS_
#include <windows.h>
#endif

// ─── NT-specific scalar aliases not declared by basic Windows.h ──────────────
#ifndef _NTSTATUS_
#define _NTSTATUS_
typedef LONG NTSTATUS;
#endif

#ifndef UINT
using UINT    = unsigned int;
#endif
#ifndef LPCWSTR
using LPCWSTR = const wchar_t*;
#endif
#ifndef PUCHAR
using PUCHAR  = unsigned char*;
#endif
#ifndef UCHAR
using UCHAR   = unsigned char;
#endif
#ifndef BOOLEAN
using BOOLEAN = unsigned char;
#endif

// ─── NT-specific constants not in WIN32_LEAN_AND_MEAN Windows.h ──────────────
#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE     0x40ul
#define OBJ_INHERIT              0x02ul
#endif
#ifndef FILE_SYNCHRONOUS_IO_NONALERT
#define FILE_SYNCHRONOUS_IO_NONALERT 0x20ul
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000ul
#endif
#ifndef DLL_PROCESS_ATTACH
#define DLL_PROCESS_ATTACH 1ul
#define DLL_PROCESS_DETACH 0ul
#endif
#ifndef CONTEXT_DEBUG_REGISTERS
#define CONTEXT_DEBUG_REGISTERS 0x00010010ul
#endif

// ─── NT-specific structs (not in standard Win32 SDK headers) ─────────────────

struct UNICODE_STRING_NT  { USHORT Length, MaximumLength; wchar_t* Buffer; };
struct CLIENT_ID_NT       { HANDLE UniqueProcess, UniqueThread; };
struct OBJECT_ATTRIBUTES_NT
{
    ULONG Length; HANDLE RootDirectory; UNICODE_STRING_NT* ObjectName;
    ULONG Attributes; PVOID SecurityDescriptor, SecurityQualityOfService;
};

#define INIT_OA(oa,name,attr) do { \
    (oa).Length=sizeof(OBJECT_ATTRIBUTES_NT); (oa).RootDirectory=nullptr; \
    (oa).ObjectName=(name); (oa).Attributes=(attr); \
    (oa).SecurityDescriptor=nullptr; (oa).SecurityQualityOfService=nullptr; \
} while(0)

struct KEY_VALUE_PARTIAL_INFO { ULONG TitleIndex, Type, DataLength; BYTE Data[1]; };

struct SYSTEM_PROCESS_INFO
{
    ULONG NextEntryOffset, NumberOfThreads;
    uint8_t _pad[48];
    UNICODE_STRING_NT ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    uint8_t _rest[1];
};

// ─── PE/image helpers (supplement the IMAGE_* structs already in winnt.h) ────
// IMAGE_FIRST_SECTION may already exist; guard it.
#ifndef IMAGE_FIRST_SECTION_DEFINED
#define IMAGE_FIRST_SECTION_DEFINED
inline IMAGE_SECTION_HEADER* nt_first_section( const IMAGE_NT_HEADERS* nh )
{
    return reinterpret_cast<IMAGE_SECTION_HEADER*>(
        const_cast<uint8_t*>(
            reinterpret_cast<const uint8_t*>(&nh->OptionalHeader)
            + nh->FileHeader.SizeOfOptionalHeader ) );
}
inline uint64_t nt_image_ordinal( uint64_t o ) { return o & 0xffffu; }
#endif

