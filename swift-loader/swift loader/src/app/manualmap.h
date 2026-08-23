// ── manualmap.h ───────────────────────────────────────────────────────────────
#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace app {

struct MapResult {
    bool        success = false;
    std::string message;
};

// Find a process by name, returns PID or 0 if not found.
DWORD find_process_pid(const wchar_t* name);

// Find cs2.exe process and manual-map the PE image in memory directly into it.
// On success the DLL entry point (DllMain) is called with DLL_PROCESS_ATTACH.
MapResult manual_map_inject(const std::vector<uint8_t>& pe_data);

} // namespace app
