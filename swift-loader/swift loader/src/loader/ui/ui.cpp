#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../imgui/backends/imgui_impl_dx11.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <functional>

#include "ui.hpp"

static std::mutex                g_log_mtx;
static std::vector<std::string>  g_log_lines;
static std::atomic<bool>         g_done { false };
static float                     g_spin { 0.f };

static ID3D11Device*             g_device = nullptr;
static ID3D11DeviceContext*      g_ctx    = nullptr;
static IDXGISwapChain*           g_swap   = nullptr;
static ID3D11RenderTargetView*   g_rtv    = nullptr;
static HWND                      g_hwnd   = nullptr;
static bool                      g_has_d3d = false;

static constexpr int WIN_W = 480;
static constexpr int WIN_H = 300;

static void create_rtv()
{
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer( 0, IID_PPV_ARGS( &back ) );
    if ( back ) { g_device->CreateRenderTargetView( back, nullptr, &g_rtv ); back->Release(); }
}

static void destroy_rtv()
{
    if ( g_rtv ) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool create_d3d( HWND hwnd )
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount          = 2;
    sd.BufferDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow         = hwnd;
    sd.SampleDesc.Count     = 1;
    sd.Windowed             = TRUE;
    sd.SwapEffect           = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if ( FAILED( D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd,
            &g_swap, &g_device, nullptr, &g_ctx ) ) )
        return false;

    create_rtv();
    return true;
}

static void destroy_d3d()
{
    destroy_rtv();
    if ( g_swap   ) { g_swap->Release();   g_swap   = nullptr; }
    if ( g_ctx    ) { g_ctx->Release();    g_ctx    = nullptr; }
    if ( g_device ) { g_device->Release(); g_device = nullptr; }
}

static void render_gdi()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint( g_hwnd, &ps );

    RECT rc{}; GetClientRect( g_hwnd, &rc );
    HBRUSH bg = CreateSolidBrush( RGB( 18, 18, 23 ) );
    FillRect( hdc, &rc, bg );
    DeleteObject( bg );

    SetBkMode( hdc, TRANSPARENT );
    HFONT font = CreateFontA( 16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas" );
    SelectObject( hdc, font );

    SetTextColor( hdc, RGB( 100, 200, 255 ) );
    TextOutA( hdc, 12, 10, "  SWIFT LOADER", 14 );

    int y = 45;
    {
        std::lock_guard<std::mutex> lk( g_log_mtx );
        for ( const auto& line : g_log_lines )
        {
            bool is_err = line.find( "[ERR]"  ) != std::string::npos
                       || line.find( "[FAIL]" ) != std::string::npos;
            bool is_ok  = line.find( "[OK]"   ) != std::string::npos
                       || line.find( "[DONE]" ) != std::string::npos;
            bool is_warn = line.find( "[WARN]" ) != std::string::npos;

            if      ( is_err  ) SetTextColor( hdc, RGB( 255,  80,  80 ) );
            else if ( is_ok   ) SetTextColor( hdc, RGB(  80, 210,  80 ) );
            else if ( is_warn ) SetTextColor( hdc, RGB( 255, 180,  60 ) );
            else                SetTextColor( hdc, RGB( 180, 180, 190 ) );

            TextOutA( hdc, 12, y, line.c_str(), static_cast<int>( line.size() ) );
            y += 18;
            if ( y > rc.bottom - 30 ) break;
        }
    }

    if ( g_done.load() )
    {
        SetTextColor( hdc, RGB( 80, 210, 80 ) );
        TextOutA( hdc, 12, rc.bottom - 24, "Done", 4 );
    }

    DeleteObject( font );
    EndPaint( g_hwnd, &ps );
}

static void apply_theme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.f;
    s.FrameRounding     = 4.f;
    s.ScrollbarRounding = 4.f;
    s.WindowBorderSize  = 0.f;
    s.FramePadding      = { 8, 4 };
    s.ItemSpacing       = { 8, 6 };

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = { 0.07f, 0.07f, 0.09f, 1.f };
    c[ImGuiCol_ChildBg]       = { 0.10f, 0.10f, 0.12f, 1.f };
    c[ImGuiCol_FrameBg]       = { 0.13f, 0.13f, 0.16f, 1.f };
    c[ImGuiCol_Separator]     = { 0.20f, 0.20f, 0.25f, 1.f };
    c[ImGuiCol_Text]          = { 0.90f, 0.90f, 0.92f, 1.f };
    c[ImGuiCol_TextDisabled]  = { 0.45f, 0.45f, 0.50f, 1.f };
    c[ImGuiCol_ScrollbarBg]   = { 0.07f, 0.07f, 0.09f, 1.f };
    c[ImGuiCol_ScrollbarGrab] = { 0.25f, 0.25f, 0.30f, 1.f };
    c[ImGuiCol_Button]        = { 0.20f, 0.45f, 0.60f, 1.f };
    c[ImGuiCol_ButtonHovered] = { 0.25f, 0.55f, 0.75f, 1.f };
    c[ImGuiCol_ButtonActive]  = { 0.30f, 0.65f, 0.85f, 1.f };
}

static void render_imgui()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( vp->Pos );
    ImGui::SetNextWindowSize( vp->Size );
    ImGui::Begin( "##root", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize     | ImGuiWindowFlags_NoSavedSettings );

    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4{ 0.40f, 0.80f, 1.00f, 1.f } );
    ImGui::SetWindowFontScale( 1.35f );
    const char* title = "  SWIFT  LOADER";
    ImGui::SetCursorPosX( ( avail - ImGui::CalcTextSize( title ).x ) * 0.5f );
    ImGui::Text( "%s", title );
    ImGui::SetWindowFontScale( 1.f );
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4{ 0.08f, 0.08f, 0.10f, 1.f } );
    ImGui::BeginChild( "##log", { 0.f, vp->Size.y - 90.f }, true );

    {
        std::lock_guard<std::mutex> lk( g_log_mtx );
        for ( const auto& line : g_log_lines )
        {
            bool is_err  = line.find( "[ERR]"  ) != std::string::npos
                        || line.find( "[FAIL]" ) != std::string::npos;
            bool is_ok   = line.find( "[OK]"   ) != std::string::npos
                        || line.find( "[DONE]" ) != std::string::npos;
            bool is_warn = line.find( "[WARN]" ) != std::string::npos;

            if      ( is_err  ) ImGui::PushStyleColor( ImGuiCol_Text, { 1.f,   0.35f, 0.35f, 1.f } );
            else if ( is_ok   ) ImGui::PushStyleColor( ImGuiCol_Text, { 0.4f,  0.85f, 0.40f, 1.f } );
            else if ( is_warn ) ImGui::PushStyleColor( ImGuiCol_Text, { 1.f,   0.70f, 0.25f, 1.f } );
            else                ImGui::PushStyleColor( ImGuiCol_Text, { 0.75f, 0.75f, 0.80f, 1.f } );

            ImGui::TextUnformatted( line.c_str() );
            ImGui::PopStyleColor();
        }
    }

    if ( ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f )
        ImGui::SetScrollHereY( 1.f );

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if ( g_done.load() )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, { 0.4f, 0.85f, 0.4f, 1.f } );
        ImGui::Text( "  OK  Done" );
        ImGui::PopStyleColor();
        ImGui::SameLine( avail - 64.f );
        if ( ImGui::Button( "Close" ) )
            PostMessageW( g_hwnd, WM_CLOSE, 0, 0 );
    }
    else
    {
        g_spin += ImGui::GetIO().DeltaTime;
        const char* frames[] = { "o--", "-o-", "--o", "-o-" };
        ImGui::Text( "  [%s]  Loading", frames[ static_cast<int>( g_spin * 6.f ) & 3 ] );
    }

    ImGui::End();
    ImGui::Render();

    constexpr float clear[4] = { 0.07f, 0.07f, 0.09f, 1.f };
    g_ctx->OMSetRenderTargets( 1, &g_rtv, nullptr );
    g_ctx->ClearRenderTargetView( g_rtv, clear );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
    g_swap->Present( 1, 0 );
}

extern LRESULT ImGui_ImplWin32_WndProcHandler( HWND, UINT, WPARAM, LPARAM );

static LRESULT CALLBACK WndProc( HWND h, UINT msg, WPARAM w, LPARAM l )
{
    if ( g_has_d3d && ImGui_ImplWin32_WndProcHandler( h, msg, w, l ) )
        return TRUE;

    switch ( msg )
    {
    case WM_PAINT:
        if ( !g_has_d3d ) render_gdi();
        else ValidateRect( h, nullptr );
        return 0;

    case WM_SIZE:
        if ( g_has_d3d && g_swap )
        {
            destroy_rtv();
            g_swap->ResizeBuffers( 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0 );
            create_rtv();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }

    return DefWindowProcW( h, msg, w, l );
}

static DWORD call_work_seh( std::function<void()>* fn )
{
    __try { ( *fn )(); }
    __except ( EXCEPTION_EXECUTE_HANDLER ) { return GetExceptionCode(); }
    return 0;
}

namespace ui
{
    void log( const char* fmt, ... )
    {
        char buf[512];
        va_list ap;
        va_start( ap, fmt );
        vsnprintf( buf, sizeof buf, fmt, ap );
        va_end( ap );

        {
            std::lock_guard<std::mutex> lk( g_log_mtx );
            g_log_lines.emplace_back( buf );
        }

        if ( !g_has_d3d && g_hwnd )
            InvalidateRect( g_hwnd, nullptr, TRUE );
    }

    void run( std::function<void()> work )
    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof wc;
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW( nullptr );
        wc.hbrBackground = static_cast<HBRUSH>( GetStockObject( BLACK_BRUSH ) );
        wc.lpszClassName = L"SwiftLoaderUI";
        RegisterClassExW( &wc );

        const int scr_w = GetSystemMetrics( SM_CXSCREEN );
        const int scr_h = GetSystemMetrics( SM_CYSCREEN );
        g_hwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            L"SwiftLoaderUI", L"Swift Loader",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            ( scr_w - WIN_W ) / 2, ( scr_h - WIN_H ) / 2,
            WIN_W, WIN_H,
            nullptr, nullptr, wc.hInstance, nullptr );

        ShowWindow( g_hwnd, SW_SHOWNORMAL );
        SetForegroundWindow( g_hwnd );
        UpdateWindow( g_hwnd );

        g_has_d3d = create_d3d( g_hwnd );

        if ( g_has_d3d )
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            apply_theme();
            ImGui_ImplWin32_Init( g_hwnd );
            ImGui_ImplDX11_Init( g_device, g_ctx );
        }

        HANDLE wt = CreateThread( nullptr, 0,
            []( LPVOID ctx ) -> DWORD
            {
                auto* fn = static_cast<std::function<void()>*>( ctx );
                const DWORD code = call_work_seh( fn );
                if ( code )
                {
                    char buf[ 64 ];
                    sprintf_s( buf, sizeof buf, "[ERR] CRASH 0x%08X in loader", code );
                    ui::log( buf );
                }
                delete fn;
                g_done.store( true );
                if ( g_hwnd ) InvalidateRect( g_hwnd, nullptr, TRUE );
                return 0;
            },
            new std::function<void()>( std::move( work ) ), 0, nullptr );

        MSG msg{};
        while ( true )
        {
            while ( PeekMessageW( &msg, nullptr, 0, 0, PM_REMOVE ) )
            {
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
                if ( msg.message == WM_QUIT ) goto cleanup;
            }

            if ( g_has_d3d )
                render_imgui();
            else
                Sleep( 16 );


        }

    cleanup:
        if ( wt ) { WaitForSingleObject( wt, 5000 ); CloseHandle( wt ); }

        if ( g_has_d3d )
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            destroy_d3d();
        }

        DestroyWindow( g_hwnd );
        g_hwnd = nullptr;
        UnregisterClassW( wc.lpszClassName, wc.hInstance );
    }
}
