#pragma once
#include "nt_types.hpp"
#include <cstdint>
#include <vector>

namespace mapper
{
    uintptr_t map_local( const std::vector<uint8_t>& pe );
    uintptr_t map_remote( HANDLE proc, const std::vector<uint8_t>& pe );
    void      wipe_headers( HANDLE proc, uintptr_t base );
}
