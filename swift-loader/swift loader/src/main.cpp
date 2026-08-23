#include <windows.h>
#include <windowsx.h>
#include <string>
#include "core/types.h"
#include "gfx/device.h"
#include "gfx/renderer.h"
#include "ui/ui.h"
#include "app/app.h"

using core::Vec2;

static const int kMargin = 28;
static const int kMaxPanelW = 520;
static const int kMaxPanelH = 320;
static const float kBarHeight = app::kTitleBarHeight;

struct AppState {
    gfx::Device device;
    gfx::Renderer renderer;
    app::App app;

    ui::Input input;
    bool prevDown  = false;
    char lastChar  = 0;
    bool backspace = false;
    bool enterKey  = false;
    std::string pasteText;

    bool dragging = false;
    POINT dragOrigin = {};
    RECT dragWindow = {};

    core::Rect panel;

    LARGE_INTEGER freq = {}, last = {};
    bool wantShot = false;
    std::wstring shotPath;
    float shotTime = 0.6f;
    float simTime = 0.f;
    bool running = true;
};

static AppState* gs = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!gs) return DefWindowProc(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_MOUSEMOVE:
            gs->input.mouse = Vec2((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            gs->input.down = true;
            const float mx = (float)GET_X_LPARAM(lParam);
            const float my = (float)GET_Y_LPARAM(lParam);
            const core::Rect& p = gs->panel;
            if (my > p.y && my < p.y + kBarHeight && mx > p.x && mx < p.r() - 40.f) {
                gs->dragging = true;
                GetCursorPos(&gs->dragOrigin);
                GetWindowRect(hwnd, &gs->dragWindow);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            ReleaseCapture();
            gs->input.down = false;
            gs->dragging = false;
            return 0;
        case WM_CHAR:
            if (wParam >= 0x20 && wParam < 0x7F)
                gs->lastChar = (char)wParam;
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) gs->app.requestClose();
            if (wParam == VK_BACK)   gs->backspace = true;
            if (wParam == VK_RETURN) gs->enterKey  = true;
            if (wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                // Ctrl+V — read clipboard text
                if (OpenClipboard(nullptr)) {
                    HANDLE h = GetClipboardData(CF_TEXT);
                    if (h) {
                        const char* txt = (const char*)GlobalLock(h);
                        if (txt) { gs->pasteText = txt; GlobalUnlock(h); }
                    }
                    CloseClipboard();
                }
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            gs->running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void applyDrag(HWND hwnd) {
    if (!gs->dragging) return;
    POINT now;
    GetCursorPos(&now);
    SetWindowPos(hwnd, nullptr, gs->dragWindow.left + (now.x - gs->dragOrigin.x),
                 gs->dragWindow.top + (now.y - gs->dragOrigin.y), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdline, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    AppState state;
    gs = &state;

    const std::wstring cl = cmdline ? cmdline : L"";
    bool headless = false;
    bool autoInject = false;

    auto argAfter = [&](const wchar_t* flag, std::wstring& out) -> bool {
        size_t p = cl.find(flag);
        if (p == std::wstring::npos) return false;
        std::wstring rest = cl.substr(p + wcslen(flag));
        size_t s = rest.find_first_not_of(L" ");
        if (s == std::wstring::npos) return false;
        std::wstring value = rest.substr(s);
        size_t e = value.find(L' ');
        if (e != std::wstring::npos) value = value.substr(0, e);
        if (value.empty() || value[0] == L'-') return false;
        out = value;
        return true;
    };

    if (cl.find(L"-shot") != std::wstring::npos) {
        state.wantShot = true;
        headless = true;
        state.shotPath = L"shot.bmp";
        std::wstring value;
        if (argAfter(L"-shot", value)) state.shotPath = value;
        if (argAfter(L"-t", value)) state.shotTime = (float)_wtof(value.c_str());
    }
    if (cl.find(L"-inject") != std::wstring::npos) autoInject = true;
    const bool forceHover = cl.find(L"-hover") != std::wstring::npos;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SwiftLoader";
    RegisterClassExW(&wc);

    const int winW = kMaxPanelW + kMargin * 2;
    const int winH = kMaxPanelH + kMargin * 2;
    const int sx = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    const int sy = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, wc.lpszClassName, L"Swift",
                                WS_POPUP, sx, sy, winW, winH, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    if (!state.device.create(hwnd, winW, winH)) return 1;
    if (!state.renderer.create(state.device.dev())) return 1;
    if (!state.app.init(state.device.dev())) return 1;

    if (!headless) ShowWindow(hwnd, SW_SHOW);
    if (autoInject) state.app.debugStartInject();

    QueryPerformanceFrequency(&state.freq);
    QueryPerformanceCounter(&state.last);

    while (state.running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) state.running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!state.running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - state.last.QuadPart) / (float)state.freq.QuadPart;
        state.last = now;
        dt = (std::min)((std::max)(dt, 1.f / 240.f), 0.05f);
        if (state.wantShot) dt = 1.f / 60.f;
        state.simTime += dt;

        applyDrag(hwnd);

        ui::Input frameInput = state.input;
        if (forceHover)
            frameInput.mouse = Vec2(state.panel.center().x, state.panel.y + kBarHeight + 32.f);
        frameInput.pressed   = state.input.down && !state.prevDown;
        frameInput.released  = !state.input.down && state.prevDown;
        frameInput.lastChar  = state.lastChar;
        frameInput.backspace = state.backspace;
        frameInput.enterKey  = state.enterKey;
        frameInput.pasteText = state.pasteText;
        state.prevDown   = state.input.down;
        state.lastChar   = 0;
        state.backspace  = false;
        state.enterKey   = false;
        state.pasteText.clear();

        ui::newFrame(frameInput, dt, (float)winW, (float)winH);
        state.app.frame(dt);
        ui::endFrame();
        state.panel = state.app.panelRect();

        state.device.beginFrame();
        state.renderer.render(state.device.ctx(), ui::dl(), winW, winH, state.app.fadeAlpha());
        state.device.present(true);

        if (state.app.finished()) state.running = false;

        if (state.wantShot && state.simTime >= state.shotTime) {
            state.device.captureBackbuffer(state.shotPath);
            state.running = false;
        }
    }

    state.app.shutdown();
    state.renderer.destroy();
    state.device.destroy();
    DestroyWindow(hwnd);
    CoUninitialize();
    return 0;
}
