#pragma once
#include <windows.h>
#include <string>

namespace app {

// Starts the persistent Discord RPC background daemon process if not already running.
void ensure_discord_rpc_daemon();

// Main loop for the background RPC daemon (runs when loader is invoked with -rpc-daemon).
void run_discord_rpc_daemon();

} // namespace app
