#pragma once
#include <functional>
#include <string>

namespace loader_ui
{
    struct callbacks
    {
        // Called when the user clicks "Load" on the main panel.
        // hwid_spoof: true if HWID spoofing is requested.
        // status: updated in-place with progress messages.
        // Returns true on success.
        std::function<bool(bool hwid_spoof, std::string& status)> on_inject;
    };

    void run(callbacks cb);
}