// ============================================================
//  app.cpp — Swift Loader
// ============================================================
#include "app/app.h"
#include "app/embedded_fonts.h"
#include "ui/ui.h"
#include "ui/widgets.h"
#include <windows.h>
#include <shellapi.h>
#include <cmath>
#include <algorithm>

using namespace ui;
using core::Col; using core::Rect; using core::Vec2;

namespace app {

static const float kBarH = kTitleBarHeight;

// ── Shared worker state ────────────────────────────────────────────────────────
static VerifState  g_vs{};
static std::string g_vs_key;

// ── Colour helpers (pure grey palette, matches original theme) ────────────────
static Col dimText(float a)   { return Col::hex(0xA1A1A6, a); }
static Col bodyText(float a)  { return Col::hex(0xF2F2F3, a); }
static Col subtleBg(float a)  { return Col::hex(0x252529, a); }
static Col softBorder(float a){ return Col::hex(0xFFFFFF, 0.07f * a); }
static Col accentCol(float a) { return Col::hex(0xEDEDEF, a); }

// ─────────────────────────────────────────────────────────────────────────────
bool App::init(ID3D11Device* dev) {
    FontData fd;
    fd.semiBold     = kInterSemiBold;
    fd.semiBoldSize = kInterSemiBold_size;
    fd.bold         = kInterBold;
    fd.boldSize     = kInterBold_size;
    if (!ui::init(dev, fd)) return false;
    m_assets.load(dev);
    return true;
}

void App::shutdown() {
    m_assets.unload();
    ui::shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
void App::startVerification() {
    g_vs         = {};
    g_vs_key     = m_key;
    m_verifShown = 0;
    m_verifPageT = 0.f;
    m_verifDelay = 0.f;

    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        keyauth::VerifyCallbacks cb;
        cb.on_hwid_done   = []{ InterlockedExchange(&g_vs.step, 1); Sleep(1200); };
        cb.on_sub_done    = []{ InterlockedExchange(&g_vs.step, 2); Sleep(1200); };
        cb.on_server_done = []{ InterlockedExchange(&g_vs.step, 3); };
        g_vs.result = keyauth::verify(g_vs_key, cb);
        g_vs.done   = true;
        return 0;
    }, nullptr, 0, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  screenKeyInput
// ─────────────────────────────────────────────────────────────────────────────
void App::screenKeyInput(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.004f) return;

    // ── Text input (only when this page is fully active) ──────────────────
    if (alpha > 0.5f) {
        char lc = g().input.lastChar;
        if (lc >= 0x20 && lc < 0x7F && m_key.size() < 64) {
            m_key += lc;
            m_fieldFocused = true;
        }
        if (g().input.backspace && !m_key.empty())
            m_key.pop_back();
        if (!g().input.pasteText.empty()) {
            m_fieldFocused = true;
            for (char c : g().input.pasteText)
                if (c >= 0x20 && c < 0x7F && m_key.size() < 64) m_key += c;
        }
    }

    Rect content(panel.x, panel.y + kBarH, panel.w, panel.h - kBarH);

    // ── Title ─────────────────────────────────────────────────────────────
    Rect titleR(content.x, content.y + 16.f, content.w, 18.f);
    ui::text(fonts().title, titleR, "License Key",
        bodyText(alpha), AlignH::Center, AlignV::Middle, 0.3f);

    Rect subR(content.x, content.y + 38.f, content.w, 13.f);
    ui::text(fonts().body, subR, "Enter your key to authenticate",
        dimText(0.65f * alpha), AlignH::Center, AlignV::Middle, 0.1f);

    // ── Key input field ───────────────────────────────────────────────────
    float fieldY = content.y + 64.f;
    float fieldW = content.w - 40.f;
    float fieldH = 34.f;
    Rect  field(content.x + 20.f, fieldY, fieldW, fieldH);

    // Detect click on field → mark focused (clears placeholder)
    const uint32_t fwid = id("field.key");
    if (clicked(fwid, field)) m_fieldFocused = true;

    // Background — slightly lighter when focused
    float focusT = core::easeOutCubic(anim(fwid, 2, m_fieldFocused ? 1.f : 0.f, 8.f));
    Col fieldBg(0.14f + focusT * 0.02f, 0.14f + focusT * 0.02f,
                0.15f + focusT * 0.02f, alpha);
    dl().rect(field, fieldBg, 6.f);
    dl().border(field.shrink(0.5f),
        Col::hex(0xFFFFFF, (0.06f + focusT * 0.05f) * alpha), 1.f, 5.5f);

    // Text — placeholder only shown before first focus/type
    bool hasText = !m_key.empty();
    bool showPlaceholder = !hasText && !m_fieldFocused;

    // Blinking cursor bar
    bool showCursor = m_fieldFocused && (fmodf(g().time, 1.0f) < 0.52f);

    std::string render = showPlaceholder ? "XXXX-XXXX-XXXX-XXXX"
                       : hasText         ? m_key
                                         : "";
    if (!showPlaceholder && showCursor) render += "|";

    Col fieldTextCol = showPlaceholder ? dimText(0.28f * alpha) : bodyText(alpha);
    Rect textR(field.x + 10.f, field.y, field.w - 20.f, field.h);
    ui::text(fonts().body, textR, render.c_str(),
        fieldTextCol, AlignH::Left, AlignV::Middle, 0.1f);

    // ── Error message ─────────────────────────────────────────────────────
    if (!m_keyError.empty()) {
        Rect errR(field.x, fieldY + fieldH + 5.f, fieldW, 13.f);
        ui::text(fonts().body, errR, m_keyError.c_str(),
            Col::hex(0xFF5555, alpha * 0.85f), AlignH::Left, AlignV::Middle, 0.1f);
    }

    // ── Check button ──────────────────────────────────────────────────────
    float btnY = fieldY + fieldH + (m_keyError.empty() ? 14.f : 30.f);
    float btnH = 30.f;
    Rect  btn(content.x + 20.f, btnY, fieldW, btnH);

    const uint32_t wid = id("btn.check");
    bool over  = hovered(wid, btn);
    bool click = clicked(wid, btn);
    float hT   = core::easeOutCubic(anim(wid, 0, over ? 1.f : 0.f, 10.f));

    Col btnCol(0.21f + hT * 0.05f, 0.21f + hT * 0.05f, 0.23f + hT * 0.05f, alpha);
    dl().rect(btn, btnCol, 6.f);
    dl().border(btn.shrink(0.5f),
        Col::hex(0xFFFFFF, (0.09f + hT * 0.05f) * alpha), 1.f, 5.5f);
    ui::text(fonts().title, btn, "CHECK KEY",
        accentCol(alpha), AlignH::Center, AlignV::Middle, 0.5f);

    // ── "Get key" clickable link (bottom right) ────────────────────────────
    Rect linkR(content.x, content.b() - 20.f, content.w - 12.f, 14.f);
    const uint32_t lwid = id("link.getkey");
    bool linkOver  = hovered(lwid, linkR);
    bool linkClick = clicked(lwid, linkR);
    float linkHT   = anim(lwid, 0, linkOver ? 1.f : 0.f, 10.f);
    ui::text(fonts().body, linkR, "Get a key \xE2\x86\x92",
        dimText((0.32f + linkHT * 0.25f) * alpha), AlignH::Right, AlignV::Middle, 0.1f);
    if (linkClick)
        ShellExecuteA(nullptr, "open", "https://swiftfly.xyz", nullptr, nullptr, SW_SHOWNORMAL);

    // ── Trigger check ─────────────────────────────────────────────────────
    if ((click || g().input.enterKey) && alpha > 0.5f) {
        if (m_key.size() < 4)
            m_keyError = "Please enter a valid key.";
        else {
            m_keyError.clear();
            m_page = Page::Verifying;
            startVerification();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  screenVerifying  —  no icons, just spinner + step label
// ─────────────────────────────────────────────────────────────────────────────
void App::screenVerifying(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.004f) return;

    int stepDone = (int)InterlockedCompareExchange(&g_vs.step, 0, 0);

    // Advance displayed sub-page when step increases (fade cross)
    if (m_verifShown < stepDone) {
        m_verifPageT = core::approach(m_verifPageT, -1.f, 5.f, dt);
        if (m_verifPageT <= -0.85f) {
            m_verifShown = stepDone;
            m_verifPageT = 0.f;
        }
    } else {
        m_verifPageT = core::approach(m_verifPageT, 1.f, 5.f, dt);
    }
    float iconA = alpha * std::max(0.f, m_verifPageT);

    // Wait a beat then transition to result
    if (g_vs.done && m_verifShown >= 3) {
        m_verifDelay += dt;
        if (m_verifDelay > 0.8f) {
            if (g_vs.result.success) {
                m_page = Page::GameList;
            } else {
                m_failMsg = g_vs.result.message.empty()
                    ? "Authentication failed." : g_vs.result.message;
                m_page = Page::FailResult;
            }
        }
    }

    Rect content(panel.x, panel.y + kBarH, panel.w, panel.h - kBarH);
    float cy = content.y + content.h * 0.5f;

    // ── Step label ────────────────────────────────────────────────────────
    const char* labels[] = { "Verifying...", "HWID Check", "Subscription Check", "Server Check" };
    const char* subs[]   = { "Connecting to server", "Verifying hardware ID", "Checking license", "Confirming connectivity" };
    int idx = m_verifShown < 4 ? m_verifShown : 3;

    Rect labelR(content.x, cy - 18.f, content.w, 18.f);
    ui::text(fonts().title, labelR, labels[idx],
        bodyText(iconA), AlignH::Center, AlignV::Middle, 0.3f);

    Rect subLabelR(content.x, cy + 4.f, content.w, 13.f);
    ui::text(fonts().body, subLabelR, subs[idx],
        dimText(iconA * 0.65f), AlignH::Center, AlignV::Middle, 0.1f);

    // ── Loading Bar ───────────────────────────────────────────────────────
    float cycle = 7.0f, phase = std::fmod(g().time, cycle) / cycle;
    Rect lineBg(std::floor(content.center().x - 60.f), std::floor(content.b() - 28.f), 120.f, 4.f);
    loadingLine(lineBg, phase, g().time, alpha);
}

// ─────────────────────────────────────────────────────────────────────────────
//  screenFail  —  just the icon, title, retry button (no detail message box)
// ─────────────────────────────────────────────────────────────────────────────
void App::screenFail(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.004f) return;

    m_failTimer -= dt;
    if (m_failTimer <= 0.f && !m_closing) {
        requestClose();
    }

    Rect content(panel.x, panel.y + kBarH, panel.w, panel.h - kBarH);
    float cy = content.y + content.h * 0.5f;

    // Title
    Rect titleR(content.x, cy - 20.f, content.w, 18.f);
    ui::text(fonts().title, titleR, "Verification Failed",
        bodyText(alpha), AlignH::Center, AlignV::Middle, 0.3f);

    // Countdown
    char countBuf[64];
    snprintf(countBuf, sizeof countBuf, "Closing in %d...", std::max(1, (int)std::ceil(m_failTimer)));
    Rect countR(content.x, cy + 12.f, content.w, 14.f);
    ui::text(fonts().body, countR, countBuf,
        dimText(0.65f * alpha), AlignH::Center, AlignV::Middle, 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  screenList / screenStatus (original, untouched)
// ─────────────────────────────────────────────────────────────────────────────
void App::screenList(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.8f) return;
    Rect row(panel.x + 16.f, panel.y + kBarH + 18.f, panel.w - 32.f, 52.f);
    if (gameRow("game.cs2", row, &m_assets.cs2Icon, "Counter-Strike 2")) startDownload();
}

void App::screenStatus(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.004f) return;
    Theme& t  = theme();
    Rect content(panel.x, panel.y + kBarH, panel.w, panel.h - kBarH);
    if (m_assets.cs2Hero.valid()) {
        float ia = m_assets.cs2Hero.aspect(), ra = content.w / content.h;
        Rect uv(0, 0, 1, 1);
        if (ia > ra) uv.w = ra / ia; else uv.h = ia / ra;
        uv.x = (1 - uv.w) * 0.62f; uv.y = (1 - uv.h) * 0.42f;
        float fade = 0.18f * alpha;
        dl().imageShaped(m_assets.cs2Hero.srv, content,
            Col(fade, fade, fade, alpha * 0.85f), panel, t.radius, uv, 0.72f);
        dl().rect(content, Col::hex(0x1E1E21, 0.58f * alpha), t.radius);
        dl().gradientVMasked(content,
            Col::hex(0x1E1E21, 0.f), Col::hex(0x1E1E21, 0.78f * alpha), panel, t.radius);
    }
    float cycle = 7.0f, phase = std::fmod(g().time, cycle) / cycle;
    float beat  = 0.5f - 0.5f * std::cos(phase * core::kPi * 2.f);
    float rise  = core::easeOutCubic(anim(id("logo.rise"), 0, 1.f, 2.0f));
    float logoW = content.w * 0.52f * (1.f + beat * 0.010f);
    float logoH = logoW * 0.664f;
    Rect logoBox(content.center().x - logoW * 0.5f,
                 content.y + content.h * 0.42f - logoH * 0.5f + (1.f - rise) * 10.f,
                 logoW, logoH);
    logoMark(&m_assets.logo, logoBox, alpha * rise * (0.90f + beat * 0.08f));
    Rect lineBg(std::floor(content.center().x - 52.f), std::floor(content.b() - 22.f), 104.f, 4.f);
    loadingLine(lineBg, phase, g().time, alpha);
}

// ─────────────────────────────────────────────────────────────────────────────
//  startDownload — spawn background thread to download + inject the DLL
// ─────────────────────────────────────────────────────────────────────────────
static DownloadState* g_dlState = nullptr;
static std::string    g_dlKey;

void App::startDownload() {
    DlStatus currentStatus = (DlStatus)InterlockedCompareExchange(&m_dlState.status, 0, 0);
    if (currentStatus == DlStatus::Working) return; // Prevent spawning multiple injection threads!

    m_dlState   = {};
    g_dlState   = &m_dlState;
    g_dlKey     = m_key;
    InterlockedExchange(&m_dlState.status, (LONG)DlStatus::Working);
    InterlockedExchange(&m_dlState.stage,  (LONG)DlStage::Downloading);
    m_page = Page::Downloading;

    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        DownloadResult r = download_and_inject(g_dlKey, g_dlState);
        if (g_dlState) {
            g_dlState->message = r.message;
            InterlockedExchange(&g_dlState->status,
                (LONG)(r.success ? DlStatus::Success : DlStatus::Failed));
        }
        return 0;
    }, nullptr, 0, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  screenDownloading — progress UI while DLL is being downloaded + injected
// ─────────────────────────────────────────────────────────────────────────────
void App::screenDownloading(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.004f) return;

    DlStatus st = (DlStatus)InterlockedCompareExchange(&m_dlState.status, 0, 0);

    if (st == DlStatus::Success) {
        m_successCloseTimer = 3.99f;
        m_page = Page::InjectSuccess;
        return;
    }
    if (st == DlStatus::Failed) {
        m_dlFailMsg   = m_dlState.message.empty() ? "Operation failed." : m_dlState.message;
        m_dlFailTimer = 4.99f;
        m_page = Page::DownloadFail;
        return;
    }

    // Determine stage-specific labels
    DlStage stage = (DlStage)InterlockedCompareExchange(&m_dlState.stage, 0, 0);
    const char* title = "Downloading...";
    const char* sub   = "Fetching game files";
    switch (stage) {
        case DlStage::Downloading:    title = "Checking API...";    sub = "Verifying API & License status"; break;
        case DlStage::WaitingForGame: title = "Waiting for CS2..."; sub = "Launching game engine";          break;
        case DlStage::Injecting:      title = "Injecting...";      sub = "Mapping memory into process";    break;
        case DlStage::Cleanup:        title = "Cleaning up...";    sub = "Wiping memory buffers";          break;
        case DlStage::Done:           title = "Done";              sub = "Finishing up";                   break;
    }

    Rect content(panel.x, panel.y + kBarH, panel.w, panel.h - kBarH);
    float cy = content.y + content.h * 0.5f;

    Rect labelR(content.x, cy - 18.f, content.w, 18.f);
    ui::text(fonts().title, labelR, title,
        bodyText(alpha), AlignH::Center, AlignV::Middle, 0.3f);

    Rect subR(content.x, cy + 4.f, content.w, 13.f);
    ui::text(fonts().body, subR, sub,
        dimText(alpha * 0.65f), AlignH::Center, AlignV::Middle, 0.1f);

    float cycle = 7.0f, phase = std::fmod(g().time, cycle) / cycle;
    Rect lineBg(std::floor(content.center().x - 60.f), std::floor(content.b() - 28.f), 120.f, 4.f);
    loadingLine(lineBg, phase, g().time, alpha);
}

// ─────────────────────────────────────────────────────────────────────────────
//  screenInjectSuccess — shows successful injection and auto closes loader
// ─────────────────────────────────────────────────────────────────────────────
void App::screenInjectSuccess(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.004f) return;

    m_successCloseTimer -= dt;
    if (m_successCloseTimer <= 0.f && !m_closing) {
        requestClose();
    }

    Rect content(panel.x, panel.y + kBarH, panel.w, panel.h - kBarH);
    float cy = content.y + content.h * 0.5f;

    Rect titleR(content.x, cy - 20.f, content.w, 18.f);
    ui::text(fonts().title, titleR, "Injection Successful",
        bodyText(alpha), AlignH::Center, AlignV::Middle, 0.3f);

    char countBuf[64];
    snprintf(countBuf, sizeof countBuf, "Closing in %d...", std::max(1, (int)std::ceil(m_successCloseTimer)));
    Rect countR(content.x, cy + 12.f, content.w, 14.f);
    ui::text(fonts().body, countR, countBuf,
        dimText(0.65f * alpha), AlignH::Center, AlignV::Middle, 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  screenDownloadFail — error display, auto-returns to game list
// ─────────────────────────────────────────────────────────────────────────────
void App::screenDownloadFail(const Rect& panel, float alpha, float dt) {
    if (alpha <= 0.004f) return;

    m_dlFailTimer -= dt;
    if (m_dlFailTimer <= 0.f) {
        m_page = Page::GameList;
        return;
    }

    Rect content(panel.x, panel.y + kBarH, panel.w, panel.h - kBarH);
    float cy = content.y + content.h * 0.5f;

    Rect titleR(content.x, cy - 20.f, content.w, 18.f);
    ui::text(fonts().title, titleR, "Download Failed",
        bodyText(alpha), AlignH::Center, AlignV::Middle, 0.3f);

    Rect msgR(content.x + 20.f, cy + 6.f, content.w - 40.f, 13.f);
    ui::text(fonts().body, msgR, m_dlFailMsg.c_str(),
        Col::hex(0xFF5555, alpha * 0.85f), AlignH::Center, AlignV::Middle, 0.1f);

    char countBuf[64];
    snprintf(countBuf, sizeof countBuf, "Returning in %d...", std::max(1, (int)std::ceil(m_dlFailTimer)));
    Rect countR(content.x, cy + 28.f, content.w, 14.f);
    ui::text(fonts().body, countR, countBuf,
        dimText(0.65f * alpha), AlignH::Center, AlignV::Middle, 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  frame()
// ─────────────────────────────────────────────────────────────────────────────
void App::frame(float dt) {
    m_openT  = core::clamp01(m_openT  + dt / 0.42f);
    m_fadeIn = core::easeOutCubic(m_openT);
    if (m_closing) {
        m_closeT = core::clamp01(m_closeT + dt / 0.28f);
        m_fade   = 1.f - m_closeT * m_closeT;
    }

    m_inject.update(dt);
    if (m_inject.active() && m_page == Page::GameList)
        m_page = Page::GameStatus;

    switch (m_page) {
        case Page::KeyInput:
            m_target.width  = 400.f;
            m_target.height = 228.f;
            break;
        case Page::Verifying:
            m_target.width  = 340.f;
            m_target.height = 220.f;
            break;
        case Page::FailResult:
            m_target.width  = 360.f;
            m_target.height = 260.f;
            break;
        case Page::GameList:
        case Page::GameStatus:
            m_target.width  = 480.f;
            m_target.height = 286.f;
            break;
        case Page::Downloading:
            m_target.width  = 340.f;
            m_target.height = 220.f;
            break;
        case Page::DownloadFail:
            m_target.width  = 400.f;
            m_target.height = 240.f;
            break;
        case Page::InjectSuccess:
            m_target.width  = 360.f;
            m_target.height = 220.f;
            break;
    }

    if (!m_sizeInit) {
        m_curW = m_target.width;
        m_curH = m_target.height;
        m_sizeInit = true;
    }

    Theme& t = theme();
    core::spring(m_curW, m_velW, m_target.width,  160.f, 20.f, dt);
    core::spring(m_curH, m_velH, m_target.height, 160.f, 20.f, dt);

    float scale = (1.f - m_closeT * 0.04f) * core::lerp(0.96f, 1.f, m_fadeIn);
    float pw    = std::floor(m_curW * scale);
    float ph    = std::floor(m_curH * scale);
    Rect panel(std::floor((g().width  - pw) * 0.5f),
               std::floor((g().height - ph) * 0.5f),
               pw, ph);
    m_panel = panel;

    dl().shadow(panel.shrink(2.f), Col(0.f,0.f,0.f,0.50f), 32.f, t.radius, Vec2(0.f,14.f));
    dl().rect(panel, t.body, t.radius);
    dl().border(panel.shrink(0.5f), t.border, 1.f, t.radius - 0.5f);
    dl().rect(Rect(panel.x+1.f, panel.y+kBarH, panel.w-2.f, 1.f),
              Col::hex(0xFFFFFF, 0.04f), 0.f);

    float fa = m_fadeIn * m_fade;
    float keyA  = anim(id("p.key"),  0, m_page==Page::KeyInput      ? 1.f:0.f, 12.f);
    float verA  = anim(id("p.ver"),  0, m_page==Page::Verifying     ? 1.f:0.f, 12.f);
    float failA = anim(id("p.fail"), 0, m_page==Page::FailResult    ? 1.f:0.f, 12.f);
    float listA = anim(id("p.list"), 0, m_page==Page::GameList      ? 1.f:0.f, 12.f);
    float statA = anim(id("p.stat"), 0, m_page==Page::GameStatus    ? 1.f:0.f, 12.f);
    float dlA   = anim(id("p.dl"),   0, m_page==Page::Downloading   ? 1.f:0.f, 12.f);
    float dlfA  = anim(id("p.dlf"),  0, m_page==Page::DownloadFail  ? 1.f:0.f, 12.f);
    float dlsA  = anim(id("p.dls"),  0, m_page==Page::InjectSuccess ? 1.f:0.f, 12.f);

    screenKeyInput     (panel, keyA  * fa, dt);
    screenVerifying    (panel, verA  * fa, dt);
    screenFail         (panel, failA * fa, dt);
    screenList         (panel, listA * fa, dt);
    screenStatus       (panel, statA * fa, dt);
    screenDownloading  (panel, dlA   * fa, dt);
    screenDownloadFail (panel, dlfA  * fa, dt);
    screenInjectSuccess(panel, dlsA  * fa, dt);

    titleBar(panel, kBarH, "Swift", g().time);
    Rect closeBtn(panel.r()-32.f, panel.y+kBarH*0.5f-11.f, 22.f, 22.f);
    if (closeButton("chrome.close", closeBtn) && !m_closing)
        m_closing = true;
}

} // namespace app
