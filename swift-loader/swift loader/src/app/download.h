#pragma once
#include <windows.h>
#include <string>

namespace app {

enum class DlStatus { Idle, Working, Success, Failed };

// Stages the background thread goes through
enum class DlStage { Downloading, WaitingForGame, Injecting, Cleanup, Done };

struct DownloadState {
    volatile LONG  status = (LONG)DlStatus::Idle;
    volatile LONG  stage  = (LONG)DlStage::Downloading;
    std::string    message;   // written once before setting status to Failed
};

struct DownloadResult {
    bool        success = false;
    std::string message;
};

// Blocking — call on a background thread.
// Downloads DLL, manual-map injects into cs2.exe, then deletes the file.
DownloadResult download_and_inject(const std::string& key, DownloadState* state);

} // namespace app
