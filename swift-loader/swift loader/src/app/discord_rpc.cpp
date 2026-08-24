#include "app/discord_rpc.h"
#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <memory>

namespace app {

static const wchar_t* kRpcMutexName = L"Local\\SwiftLoaderDiscordRPC_Mutex";

// Discord IPC connection handler
class DiscordIPC {
public:
    DiscordIPC() = default;
    ~DiscordIPC() { close(); }

    bool connectToPipe(int pipeIndex) {
        close();
        std::wstring pipeName = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(pipeIndex);
        m_pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_pipe != INVALID_HANDLE_VALUE) {
            std::string handshakeJson = "{\"v\":1,\"client_id\":\"1541358668480520212\"}";
            if (sendPacket(0, handshakeJson)) {
                int op = -1; std::string resp;
                if (readPacket(op, resp)) {
                    m_startTime = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    return true;
                }
            }
            close();
        }
        return false;
    }

    void close() {
        if (m_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
    }

    bool sendActivityPayload(const std::string& details, const std::string& state, const std::string& largeImage, const std::string& largeText) {
        if (m_pipe == INVALID_HANDLE_VALUE) return false;

        static int64_t nonce = 1;

        std::string json = "{"
            "\"cmd\":\"SET_ACTIVITY\","
            "\"args\":{"
                "\"pid\":" + std::to_string(GetCurrentProcessId()) + ","
                "\"activity\":{"
                    "\"details\":\"" + details + "\","
                    "\"state\":\"" + state + "\","
                    "\"timestamps\":{\"start\":" + std::to_string(m_startTime) + "},"
                    "\"assets\":{"
                        "\"large_image\":\"" + largeImage + "\","
                        "\"large_text\":\"" + largeText + "\""
                    "}"
                "}"
            "},"
            "\"nonce\":\"" + std::to_string(nonce++) + "\""
        "}";

        if (!sendPacket(1, json)) {
            close();
            return false;
        }

        // Non-blocking peek / drain response so we don't block or close pipe on delay
        DWORD avail = 0;
        if (PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &avail, nullptr) && avail >= 8) {
            int op = -1; std::string resp;
            readPacket(op, resp);
        }

        return true;
    }

private:
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    int64_t m_startTime = 0;

    bool sendPacket(int opcode, const std::string& payload) {
        if (m_pipe == INVALID_HANDLE_VALUE) return false;
        uint32_t op = (uint32_t)opcode;
        uint32_t len = (uint32_t)payload.size();

        std::string packet;
        packet.resize(8 + len);
        memcpy(&packet[0], &op, 4);
        memcpy(&packet[4], &len, 4);
        memcpy(&packet[8], payload.data(), len);

        DWORD written = 0;
        return WriteFile(m_pipe, packet.data(), (DWORD)packet.size(), &written, nullptr) && written == packet.size();
    }

    bool readPacket(int& opcode, std::string& payload) {
        if (m_pipe == INVALID_HANDLE_VALUE) return false;
        uint32_t header[2] = {0};
        DWORD read = 0;
        if (!ReadFile(m_pipe, header, 8, &read, nullptr) || read != 8) return false;

        opcode = (int)header[0];
        uint32_t len = header[1];
        payload.resize(len);

        if (len > 0) {
            if (!ReadFile(m_pipe, &payload[0], len, &read, nullptr) || read != len) return false;
        }
        return true;
    }
};

void ensure_discord_rpc_daemon() {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kRpcMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return;
    }
    if (mutex) CloseHandle(mutex);

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" -rpc-daemon";

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

struct ClientInstance {
    int pipeIndex;
    std::unique_ptr<DiscordIPC> ipc;
};

void run_discord_rpc_daemon() {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kRpcMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return;
    }

    std::vector<ClientInstance> activeClients;

    while (true) {
        // Probe all pipes 0..9
        for (int i = 0; i < 10; ++i) {
            bool found = false;
            for (auto& c : activeClients) {
                if (c.pipeIndex == i) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                auto client = std::make_unique<DiscordIPC>();
                if (client->connectToPipe(i)) {
                    client->sendActivityPayload("swift.fly", "Playing swift.fly", "swiftloog", "swift.fly");
                    activeClients.push_back(ClientInstance{ i, std::move(client) });
                }
            }
        }

        // Send activity updates across all open client connections
        for (auto it = activeClients.begin(); it != activeClients.end(); ) {
            if (!it->ipc->sendActivityPayload("swift.fly", "Playing swift.fly", "swiftloog", "swift.fly")) {
                it = activeClients.erase(it);
            } else {
                ++it;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(15));
    }

    if (mutex) ReleaseMutex(mutex);
}

} // namespace app
