#pragma once
#include <functional>
#include <string>

namespace loader_ui
{
    struct callbacks
    {
        // Called when the user clicks "Load" on the main panel.
        // hwid_spoof: true if HWID spoofing is requested.
        // token: single-use download token obtained from auth server.
        // session_key_hex: ephemeral AES key for decrypting payload.
        // status: updated in-place with progress messages.
        // Returns true on success.
        std::function<bool(bool hwid_spoof, const std::string& token, const std::string& session_key_hex, std::string& status)> on_inject;
    };

    void run(callbacks cb);
}