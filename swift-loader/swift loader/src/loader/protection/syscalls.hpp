#pragma once
#include "../nt_types.hpp"
#include <cstdint>
#include <algorithm>

namespace syscalls
{
    using NTSTATUS = LONG;

    static constexpr LONG NT_SUCCESS_VAL   = 0;
    static constexpr LONG NT_FAIL_VAL      = static_cast<LONG>(0xC0000001L);
    static constexpr LONG NT_BADHANDLE_VAL = static_cast<LONG>(0xC0000008L);

    bool init();

    // Process / thread info
    NTSTATUS query_information_process( HANDLE, uint32_t, void*, ULONG, ULONG* );
    NTSTATUS query_system_information  ( uint32_t, void*, ULONG, ULONG* );
    NTSTATUS set_information_thread    ( HANDLE, uint32_t, void*, ULONG );
    NTSTATUS get_context_thread        ( HANDLE, CONTEXT* );

    // Process / thread lifecycle
    NTSTATUS open_process    ( HANDLE*, ULONG, OBJECT_ATTRIBUTES_NT*, CLIENT_ID_NT* );
    NTSTATUS terminate_process( HANDLE, NTSTATUS );
    NTSTATUS create_thread_ex( HANDLE*, ULONG, OBJECT_ATTRIBUTES_NT*, HANDLE,
                               void*, void*, ULONG,
                               SIZE_T, SIZE_T, SIZE_T, void* );
    NTSTATUS resume_thread   ( HANDLE );
    NTSTATUS close           ( HANDLE );

    // Virtual memory
    NTSTATUS allocate_virtual_memory ( HANDLE, void**, ULONG_PTR, SIZE_T*, ULONG, ULONG );
    NTSTATUS free_virtual_memory     ( HANDLE, void**, SIZE_T*, ULONG );
    NTSTATUS protect_virtual_memory  ( HANDLE, void**, SIZE_T*, ULONG, ULONG* );
    NTSTATUS read_virtual_memory     ( HANDLE, void*, void*, SIZE_T, SIZE_T* );
    NTSTATUS write_virtual_memory    ( HANDLE, void*, const void*, SIZE_T, SIZE_T* );

    // Registry
    NTSTATUS open_key       ( HANDLE*, ULONG, OBJECT_ATTRIBUTES_NT* );
    NTSTATUS set_value_key  ( HANDLE, UNICODE_STRING_NT*, ULONG, ULONG, const void*, ULONG );
    NTSTATUS query_value_key( HANDLE, UNICODE_STRING_NT*, ULONG, void*, ULONG, ULONG* );

    // File / volume
    NTSTATUS open_file                  ( HANDLE*, ULONG, OBJECT_ATTRIBUTES_NT*,
                                         void*, ULONG, ULONG );
    NTSTATUS query_volume_information_file( HANDLE, void*, void*, ULONG, ULONG );
    NTSTATUS query_object               ( HANDLE, uint32_t, void*, ULONG, ULONG* );

    // Timing
    NTSTATUS delay_execution( BOOLEAN, LARGE_INTEGER* );

    // Helper: sleep in milliseconds via NtDelayExecution
    inline void sleep_ms( DWORD ms )
    {
        LARGE_INTEGER li;
        li.QuadPart = -static_cast<LONGLONG>(ms) * 10000LL;
        delay_execution( FALSE, &li );
    }
}
