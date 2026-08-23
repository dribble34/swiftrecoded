// ============================================================
//  Swift Loader – loader_ui.cpp
//  Flow:
//    1. Key input screen
//    2. Verification animation (HWID → Sub → Server, sequential)
//    3. Result screen  (success → main panel, fail → error)
//    4. Main panel (existing inject flow)
// ============================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define STB_IMAGE_IMPLEMENTATION
#include "../stb/stb_image.h"

#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../imgui/backends/imgui_impl_dx11.h"

#include <string>
#include <cmath>
#include <atomic>
#include <functional>

#include "loader_ui.hpp"
#include "../protection/hwid.hpp"
#include "keyauth_api.hpp"
#include "embedded_icons.hpp"   // icon_*_data / icon_*_size

// ── D3D globals ───────────────────────────────────────────────────────────────
static ID3D11Device*            g_device  = nullptr;
static ID3D11DeviceContext*     g_ctx     = nullptr;
static IDXGISwapChain*          g_swap    = nullptr;
static ID3D11RenderTargetView*  g_rtv     = nullptr;
static HWND                     g_hwnd    = nullptr;
static bool                     g_has_d3d = false;

static constexpr int WIN_W = 420;
static constexpr int WIN_H = 560;

// ── D3D helpers ───────────────────────────────────────────────────────────────
static void create_rtv()
{
    ID3D11Texture2D* b = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&b));
    if (b) { g_device->CreateRenderTargetView(b, nullptr, &g_rtv); b->Release(); }
}
static void destroy_rtv() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }
static bool create_d3d(HWND h)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        lv, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_device, nullptr, &g_ctx)))
        return false;
    create_rtv();
    return true;
}
static void destroy_d3d()
{
    destroy_rtv();
    if (g_swap)   { g_swap->Release();   g_swap   = nullptr; }
    if (g_ctx)    { g_ctx->Release();    g_ctx    = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

// ── Window proc ───────────────────────────────────────────────────────────────
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (g_has_d3d && ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return TRUE;
    if (m == WM_SIZE && g_has_d3d && g_swap)
    { destroy_rtv(); g_swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0); create_rtv(); }
    if (m == WM_NCHITTEST)
    {
        RECT rc; GetWindowRect(h, &rc);
        int x = LOWORD(l) - rc.left, y = HIWORD(l) - rc.top;
        if (y < 36) return HTCAPTION;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

// ── ImGui theme ───────────────────────────────────────────────────────────────
static void apply_theme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 20.f; s.FrameRounding   = 10.f;
    s.ChildRounding    = 14.f; s.GrabRounding    = 10.f;
    s.WindowBorderSize = 0;    s.ChildBorderSize = 0;   s.FrameBorderSize = 0;
    s.WindowPadding    = {18.f, 18.f};
    s.FramePadding     = {12.f,  9.f};
    s.ItemSpacing      = {10.f, 10.f};
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = {0.07f, 0.07f, 0.09f, 1.f};
    c[ImGuiCol_ChildBg]         = {0.13f, 0.13f, 0.16f, 1.f};
    c[ImGuiCol_Text]            = {0.93f, 0.93f, 0.95f, 1.f};
    c[ImGuiCol_TextDisabled]    = {0.50f, 0.50f, 0.55f, 1.f};
    c[ImGuiCol_FrameBg]         = {0.17f, 0.17f, 0.21f, 1.f};
    c[ImGuiCol_FrameBgHovered]  = {0.22f, 0.22f, 0.28f, 1.f};
    c[ImGuiCol_Button]          = {0.55f, 0.48f, 1.00f, 1.f};
    c[ImGuiCol_ButtonHovered]   = {0.63f, 0.57f, 1.00f, 1.f};
    c[ImGuiCol_ButtonActive]    = {0.48f, 0.42f, 0.90f, 1.f};
    c[ImGuiCol_Header]          = {0.55f, 0.48f, 1.00f, 1.f};
    c[ImGuiCol_HeaderHovered]   = {0.63f, 0.57f, 1.00f, 1.f};
    c[ImGuiCol_ScrollbarBg]     = {0.07f, 0.07f, 0.09f, 1.f};
    c[ImGuiCol_ScrollbarGrab]   = {0.55f, 0.48f, 1.00f, 1.f};
}

// ── Texture helper ────────────────────────────────────────────────────────────
static ID3D11ShaderResourceView* load_texture(const uint8_t* data, uint32_t size)
{
    int w, h, ch;
    unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &ch, 4);
    if (!px) return nullptr;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w; td.Height = (UINT)h;
    td.MipLevels = 1;   td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = px; sd.SysMemPitch = (UINT)(w * 4);

    ID3D11Texture2D* tex = nullptr;
    g_device->CreateTexture2D(&td, &sd, &tex);
    stbi_image_free(px);
    if (!tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    g_device->CreateShaderResourceView(tex, &srvd, &srv);
    tex->Release();
    return srv;
}

// ── Centred text helper ───────────────────────────────────────────────────────
static void centred_text(const char* txt, float y, ImVec4 col = { -1,-1,-1,-1 })
{
    float W   = ImGui::GetContentRegionAvail().x;
    ImVec2 ts = ImGui::CalcTextSize(txt);
    ImGui::SetCursorPosY(y);
    ImGui::SetCursorPosX((W - ts.x) * 0.5f);
    if (col.x >= 0.f) ImGui::TextColored(col, "%s", txt);
    else               ImGui::TextUnformatted(txt);
}

// ── Shared verification state (written by worker, read by main thread) ─────────
struct VerifShared
{
    volatile LONG               step    = 0;  // 1=hwid done, 2=sub done, 3=srv done
    volatile bool               done    = false;
    keyauth_api::CheckResult    result  = {};
    std::string                 key;
    std::string                 hwid_str;
};
static VerifShared g_vs;

// ═════════════════════════════════════════════════════════════════════════════
//  run()
// ═════════════════════════════════════════════════════════════════════════════
namespace loader_ui {

void run(callbacks cb)
{
    // ── Window ────────────────────────────────────────────────────────────────
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"SwiftLoader";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST, L"SwiftLoader", L"Swift",
        WS_POPUP,
        (sw - WIN_W) / 2, (sh - WIN_H) / 2,
        WIN_W, WIN_H, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hwnd);
    UpdateWindow(g_hwnd);

    g_has_d3d = create_d3d(g_hwnd);
    if (!g_has_d3d) { DestroyWindow(g_hwnd); return; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    apply_theme();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_ctx);

    // ── Textures ──────────────────────────────────────────────────────────────
    ID3D11ShaderResourceView* tex_hwid = load_texture(icon_hwid_check_data,      icon_hwid_check_size);
    ID3D11ShaderResourceView* tex_sub  = load_texture(icon_subscription_check_data, icon_subscription_check_size);
    ID3D11ShaderResourceView* tex_srv  = load_texture(icon_server_check_data,    icon_server_check_size);
    ID3D11ShaderResourceView* tex_fail = load_texture(icon_failed_data,          icon_failed_size);

    // ── App state ─────────────────────────────────────────────────────────────
    enum class Page { key_input, verifying, result_fail, panel };
    Page cur = Page::key_input;

    char        key_buf[128]{};
    std::string key_err;

    bool        verif_running = false;
    std::string fail_msg;

    std::string inject_status;
    bool        injecting  = false;
    bool        injected   = false;
    bool        hwid_spoof = true;

    float t = 0.f;

    // ── Main loop ─────────────────────────────────────────────────────────────
    MSG msg{};
    while (true)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) goto cleanup;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        t += ImGui::GetIO().DeltaTime;

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize     | ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl   = ImGui::GetWindowDrawList();
        ImVec2      wpos = ImGui::GetWindowPos();
        ImVec2      wsz  = ImGui::GetWindowSize();
        float       W    = ImGui::GetContentRegionAvail().x;

        // ── Background gradient ───────────────────────────────────────────────
        dl->AddRectFilledMultiColor(wpos, { wpos.x + wsz.x, wpos.y + wsz.y },
            IM_COL32(14, 13, 22, 255), IM_COL32(14, 13, 22, 255),
            IM_COL32(20, 16, 32, 255), IM_COL32(20, 16, 32, 255));

        // Top accent bar
        dl->AddRectFilled(wpos, { wpos.x + wsz.x, wpos.y + 2.f },
            IM_COL32(130, 110, 255, 200));

        // ─────────────────────────────────────────────────────────────────────
        //  PAGE: KEY INPUT
        // ─────────────────────────────────────────────────────────────────────
        if (cur == Page::key_input)
        {
            // Animated logo glow
            float ga = 0.5f + 0.5f * sinf(t * 1.8f);
            ImVec2 cp = { wpos.x + wsz.x * 0.5f, wpos.y + 72.f };
            dl->AddCircleFilled(cp, 44.f + ga * 8.f,
                IM_COL32(110, 90, 255, (int)(25 * ga)));
            dl->AddCircleFilled(cp, 34.f, IM_COL32(130, 110, 255, 55));

            ImGui::SetWindowFontScale(2.0f);
            centred_text("SWIFT", 44.f, { 0.68f, 0.58f, 1.00f, 1.f });
            ImGui::SetWindowFontScale(1.f);

            centred_text("Enter your license key to continue", 92.f,
                { 0.52f, 0.52f, 0.60f, 1.f });

            // Card
            float card_w = W - 36.f;
            ImGui::SetCursorPos({ 18.f, 126.f });
            ImVec2 cpos = ImGui::GetCursorScreenPos();
            float  card_h = 170.f;

            dl->AddRectFilled(cpos, { cpos.x + card_w, cpos.y + card_h },
                IM_COL32(26, 24, 38, 255), 16.f);
            dl->AddRect(cpos, { cpos.x + card_w, cpos.y + card_h },
                IM_COL32(75, 60, 155, 120), 16.f, 0, 1.f);

            ImGui::BeginChild("##kcard", { card_w, card_h }, false);

            ImGui::SetCursorPos({ 14.f, 14.f });
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.58f, 0.80f, 1.f));
            ImGui::TextUnformatted("License Key");
            ImGui::PopStyleColor();

            ImGui::SetCursorPos({ 14.f, 38.f });
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.10f, 0.09f, 0.15f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.13f, 0.20f, 1.f));
            ImGui::SetNextItemWidth(card_w - 28.f);
            bool enter_pressed = ImGui::InputText("##key", key_buf, sizeof key_buf,
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor(2);

            if (!key_err.empty())
            {
                ImGui::SetCursorPos({ 14.f, 76.f });
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
                ImGui::TextWrapped("%s", key_err.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorPos({ 14.f, 118.f });
            float pulse = 0.88f + 0.12f * sinf(t * 2.5f);
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.55f * pulse, 0.48f * pulse, 1.00f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.63f, 0.57f, 1.f, 1.f));
            bool clicked = ImGui::Button("  CHECK KEY  ", { card_w - 28.f, 38.f });
            ImGui::PopStyleColor(2);

            ImGui::EndChild();

            if ((clicked || enter_pressed) && !verif_running)
            {
                if (strlen(key_buf) < 4)
                {
                    key_err = "Please enter a valid license key.";
                }
                else
                {
                    key_err.clear();

                    // Reset shared state
                    g_vs = {};
                    g_vs.key      = key_buf;
                    g_vs.hwid_str = hwid::get_short();

                    verif_running = true;
                    cur = Page::verifying;

                    // Launch verification thread
                    CreateThread(nullptr, 0, [](LPVOID) -> DWORD
                    {
                        keyauth_api::VerifyCallbacks cbs;
                        cbs.on_hwid_done    = []{ InterlockedExchange(&g_vs.step, 1); Sleep(800); };
                        cbs.on_sub_done     = []{ InterlockedExchange(&g_vs.step, 2); Sleep(700); };
                        cbs.on_server_done  = []{ InterlockedExchange(&g_vs.step, 3); Sleep(500); };

                        g_vs.result = keyauth_api::verify_key(g_vs.key, g_vs.hwid_str, cbs);
                        g_vs.done   = true;
                        return 0;
                    }, nullptr, 0, nullptr);
                }
            }
        }
        // ─────────────────────────────────────────────────────────────────────
        //  PAGE: VERIFYING
        // ─────────────────────────────────────────────────────────────────────
        else if (cur == Page::verifying)
        {
            int step_done = (int)InterlockedCompareExchange(&g_vs.step, 0, 0);

            // Check for completion
            if (g_vs.done && step_done >= 3)
            {
                verif_running = false;
                if (g_vs.result.success)
                {
                    cur = Page::panel;
                }
                else
                {
                    fail_msg = "";
                    if (!g_vs.result.hwid_ok)
                        fail_msg += "Failed HWID Check\n";
                    if (!g_vs.result.sub_ok)
                        fail_msg += "Failed Subscription Check\n";
                    if (!g_vs.result.server_ok)
                        fail_msg += "Failed Server Check";
                    if (fail_msg.empty())
                        fail_msg = g_vs.result.message.empty()
                            ? "Authentication failed." : g_vs.result.message;
                    cur = Page::result_fail;
                }
            }

            // Title
            ImGui::SetWindowFontScale(1.6f);
            centred_text("Verifying", 24.f, { 0.70f, 0.60f, 1.00f, 1.f });
            ImGui::SetWindowFontScale(1.f);
            centred_text("Please wait...", 58.f, { 0.48f, 0.48f, 0.56f, 1.f });

            // Three step circles
            struct StepDef { ID3D11ShaderResourceView* tex; const char* label; };
            StepDef steps[3] = {
                { tex_hwid, "HWID Check"         },
                { tex_sub,  "Subscription Check" },
                { tex_srv,  "Server Check"       },
            };

            float total_w   = (float)WIN_W;
            float spacing   = total_w / 3.f;
            float r         = 46.f;
            float img_sz    = 68.f;
            float base_y    = wpos.y + 110.f;

            for (int i = 0; i < 3; ++i)
            {
                float cx = wpos.x + spacing * i + spacing * 0.5f;
                float cy = base_y + r;

                bool is_done   = (step_done > i);
                bool is_active = (step_done == i) && !g_vs.done;

                // Ring
                float pr = is_active ? (r + 4.f * sinf(t * 3.f)) : r;
                ImU32 ring_col = is_done   ? IM_COL32(100, 210, 90, 200)
                               : is_active ? IM_COL32(130, 110, 255, 220)
                                           : IM_COL32(55,  55,  75, 160);
                dl->AddCircleFilled({ cx, cy }, pr, IM_COL32(24, 22, 36, 255));
                dl->AddCircle({ cx, cy }, pr, ring_col, 64, is_active ? 2.f : 1.5f);

                // Icon inside circle
                if (steps[i].tex)
                    dl->AddImage((ImTextureID)steps[i].tex,
                        { cx - img_sz * 0.5f, cy - img_sz * 0.5f },
                        { cx + img_sz * 0.5f, cy + img_sz * 0.5f });

                // Spinning arc on active step
                if (is_active)
                {
                    constexpr int   N   = 30;
                    constexpr float PI2 = 6.2831853f;
                    float angle = fmodf(t * 3.2f, PI2);
                    for (int s = 0; s < N; ++s)
                    {
                        float a0 = angle + s * PI2 / N;
                        float a1 = angle + (s + 1) * PI2 / N;
                        int   al = (int)((float)s / N * 220.f);
                        dl->AddLine(
                            { cx + (pr + 5.f) * cosf(a0), cy + (pr + 5.f) * sinf(a0) },
                            { cx + (pr + 5.f) * cosf(a1), cy + (pr + 5.f) * sinf(a1) },
                            IM_COL32(130, 110, 255, al), 2.5f);
                    }
                }

                // Checkmark tick on done steps
                if (is_done)
                {
                    // Small green circle badge top-right
                    float bx = cx + r * 0.65f, by = cy - r * 0.65f;
                    dl->AddCircleFilled({ bx, by }, 10.f, IM_COL32(80, 200, 90, 240));
                    // ✓ drawn as two lines
                    dl->AddLine({ bx - 5.f, by }, { bx - 1.f, by + 4.f },
                        IM_COL32(255, 255, 255, 255), 2.f);
                    dl->AddLine({ bx - 1.f, by + 4.f }, { bx + 5.f, by - 3.f },
                        IM_COL32(255, 255, 255, 255), 2.f);
                }

                // Label
                ImVec2 lsz = ImGui::CalcTextSize(steps[i].label);
                ImU32 tcol = is_done   ? IM_COL32(100, 220, 90, 255)
                           : is_active ? IM_COL32(180, 160, 255, 255)
                                       : IM_COL32(90, 90, 110, 200);
                dl->AddText({ cx - lsz.x * 0.5f, cy + r + 12.f }, tcol, steps[i].label);
            }

            // Connector lines
            for (int i = 0; i < 2; ++i)
            {
                float x0 = wpos.x + spacing * i     + spacing * 0.5f + r + 4.f;
                float x1 = wpos.x + spacing * (i+1) + spacing * 0.5f - r - 4.f;
                float y  = base_y + r;
                ImU32 lc = (step_done > i)
                    ? IM_COL32(100, 210, 90, 100)
                    : IM_COL32(55, 55, 75, 100);
                dl->AddLine({ x0, y }, { x1, y }, lc, 1.5f);
            }
        }
        // ─────────────────────────────────────────────────────────────────────
        //  PAGE: RESULT FAIL
        // ─────────────────────────────────────────────────────────────────────
        else if (cur == Page::result_fail)
        {
            // Failed icon
            if (tex_fail)
            {
                float sz = 130.f;
                float ix = wpos.x + (wsz.x - sz) * 0.5f;
                float iy = wpos.y + 50.f;
                dl->AddImage((ImTextureID)tex_fail,
                    { ix, iy }, { ix + sz, iy + sz },
                    { 0, 0 }, { 1, 1 }, IM_COL32(255, 170, 170, 230));
            }

            ImGui::SetWindowFontScale(1.55f);
            centred_text("Verification Failed", 196.f, { 1.f, 0.32f, 0.32f, 1.f });
            ImGui::SetWindowFontScale(1.f);

            // Detail card
            float card_w = W - 28.f;
            float card_h = 110.f;
            ImGui::SetCursorPos({ 14.f, 240.f });
            ImVec2 cpos = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(cpos, { cpos.x + card_w, cpos.y + card_h },
                IM_COL32(38, 18, 18, 255), 14.f);
            dl->AddRect(cpos, { cpos.x + card_w, cpos.y + card_h },
                IM_COL32(200, 55, 55, 100), 14.f, 0, 1.f);

            ImGui::BeginChild("##ferr", { card_w, card_h }, false);
            ImGui::SetCursorPos({ 14.f, 10.f });
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.50f, 0.50f, 1.f));
            ImGui::TextWrapped("%s", fail_msg.c_str());
            ImGui::PopStyleColor();
            ImGui::EndChild();

            float btn_w = card_w;
            // Retry
            ImGui::SetCursorPos({ 14.f, (float)WIN_H - 100.f });
            if (ImGui::Button("  Try Another Key  ", { btn_w, 38.f }))
            {
                memset(key_buf, 0, sizeof key_buf);
                key_err.clear();
                cur = Page::key_input;
            }
            // Exit
            ImGui::SetCursorPos({ 14.f, (float)WIN_H - 54.f });
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.20f, 0.16f, 0.28f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(0.26f, 0.22f, 0.36f, 1.f));
            if (ImGui::Button("  Exit  ", { btn_w, 36.f }))
                PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            ImGui::PopStyleColor(2);
        }
        // ─────────────────────────────────────────────────────────────────────
        //  PAGE: MAIN PANEL
        // ─────────────────────────────────────────────────────────────────────
        else if (cur == Page::panel)
        {
            ImGui::SetWindowFontScale(1.55f);
            centred_text("SWIFT", 16.f, { 0.70f, 0.60f, 1.00f, 1.f });
            ImGui::SetWindowFontScale(1.f);
            centred_text("Authenticated", 50.f, { 0.48f, 0.48f, 0.56f, 1.f });

            float pad    = 12.f;
            float card_h = 88.f;
            float card_w = W - pad * 2.f;

            ImGui::SetCursorPos({ pad, 78.f });
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = { p0.x + card_w, p0.y + card_h };

            dl->AddRectFilled(p0, p1, IM_COL32(30, 28, 42, 255), 14.f);
            dl->AddRect(p0, p1, IM_COL32(130, 110, 255, 180), 14.f, 0, 1.5f);

            // Thumbnail
            dl->AddRectFilled(
                { p0.x + 8.f, p0.y + 8.f },
                { p0.x + 78.f, p0.y + 72.f },
                IM_COL32(230, 165, 60, 255), 10.f);
            dl->AddText({ p0.x + 18.f, p0.y + 32.f }, IM_COL32(0, 0, 0, 200), "CS2");

            // Name + badge
            dl->AddText({ p0.x + 92.f, p0.y + 10.f },
                IM_COL32(255, 255, 255, 255), "CS2");
            ImVec2 b0 = { p1.x - 64.f, p0.y + 10.f };
            ImVec2 b1 = { p1.x -  8.f, p0.y + 26.f };
            dl->AddRectFilled(b0, b1, IM_COL32(130, 110, 255, 255), 10.f);
            dl->AddText({ b0.x + 8.f, b0.y + 2.f },
                IM_COL32(255, 255, 255, 255), "Updated");

            // Description lines
            dl->AddText({ p0.x + 92.f, p0.y + 30.f },
                IM_COL32(145, 145, 162, 255), "Gain full map awareness.");
            dl->AddText({ p0.x + 92.f, p0.y + 44.f },
                IM_COL32(145, 145, 162, 255), "Outmaneuver opponents with");
            dl->AddText({ p0.x + 92.f, p0.y + 58.f },
                IM_COL32(145, 145, 162, 255), "enhanced vision and precision.");

            ImGui::Dummy({ card_w, card_h });

            ImGui::SetCursorPos({ pad, 178.f });
            ImGui::Checkbox("Spoof hardware identity", &hwid_spoof);

            // Inject status
            if (!inject_status.empty())
            {
                bool is_err  = inject_status.rfind("[ERR]",  0) == 0;
                bool is_done = inject_status.rfind("[DONE]", 0) == 0;
                ImVec4 col = is_err  ? ImVec4(1.f, 0.35f, 0.35f, 1.f)
                           : is_done ? ImVec4(0.40f, 0.85f, 0.40f, 1.f)
                                     : ImVec4(0.75f, 0.75f, 0.80f, 1.f);
                ImGui::SetCursorPos({ pad, 210.f });
                ImGui::TextColored(col, "%s", inject_status.c_str());
            }

            if (injecting)
            {
                bool done = inject_status.rfind("[DONE]", 0) == 0
                         || inject_status.rfind("[ERR]",  0) == 0;
                if (done) { injecting = false; injected = true; }
            }

            // Load button
            ImGui::SetCursorPos({ pad, (float)WIN_H - 52.f });
            bool canLoad = !injecting && !injected;
            if (!canLoad) ImGui::BeginDisabled();

            const char* btn_lbl = injecting ? "Loading..."
                                 : injected  ? "Injected"
                                             : "  Load";
            if (ImGui::Button(btn_lbl, { (float)WIN_W - pad * 2.f, 38.f }))
            {
                injecting = true; injected = false; inject_status = "[ ] Starting";
                struct Ctx { callbacks* cb; std::string* st; bool hw; };
                CreateThread(nullptr, 0, [](LPVOID p) -> DWORD
                {
                    auto* c = (Ctx*)p;
                    bool ok = c->cb->on_inject(c->hw, *c->st);
                    if (ok)
                        *c->st = "[DONE] Injected successfully";
                    else if (c->st->rfind("[ERR]", 0) != 0)
                        *c->st = "[ERR] " + *c->st;
                    delete c;
                    return 0;
                }, new Ctx{ &cb, &inject_status, hwid_spoof }, 0, nullptr);
            }

            if (!canLoad) ImGui::EndDisabled();

            if (injected && inject_status.rfind("[DONE]", 0) == 0)
            {
                ImGui::SetCursorPos({ pad, (float)WIN_H - 22.f });
                ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.40f, 1.f), "Close to exit");
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            }
        }

        ImGui::End();
        ImGui::Render();

        constexpr float clr[4] = { 0.07f, 0.07f, 0.09f, 1.f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

cleanup:
    if (tex_hwid) tex_hwid->Release();
    if (tex_sub)  tex_sub->Release();
    if (tex_srv)  tex_srv->Release();
    if (tex_fail) tex_fail->Release();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_d3d();
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

} // namespace loader_ui
