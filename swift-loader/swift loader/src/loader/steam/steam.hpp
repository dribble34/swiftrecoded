#pragma once
#include "nt_types.hpp"
#include <cstdint>

namespace steam
{
    void kill_all();
    void spoof_machine_guid();
    void restore_machine_guid();
    void spoof_mac_addresses();
    void restore_mac_addresses();
    void spoof_steam_identity();
    void restore_steam_identity();
    bool launch();
    void patch_vac( HANDLE game );
    void wipe_steam_machine_id();
}
