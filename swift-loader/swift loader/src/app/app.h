#pragma once
#include <d3d11.h>
#include <string>
#include "core/types.h"
#include "app/assets.h"
#include "app/inject.h"
#include "app/keyauth.hpp"
#include "app/download.h"

namespace app {

struct PanelSize { float width=0.f, height=0.f; };
constexpr float kTitleBarHeight = 32.f;

enum class Page {
    KeyInput,
    Verifying,   // sub-pages: 0=init logo, 1=hwid icon, 2=sub icon, 3=server icon
    FailResult,
    GameList,
    GameStatus,
    Downloading,
    DownloadFail,
    InjectSuccess,
};

struct VerifState {
    volatile LONG  step = 0;   // 1=hwid done, 2=sub done, 3=srv done
    volatile bool  done = false;
    keyauth::Result result;
};

class App {
public:
    bool  init(ID3D11Device* dev);
    void  shutdown();
    void  frame(float dt);

    core::Rect panelRect()  const { return m_panel; }
    void requestClose()           { m_closing = true; }
    bool finished()         const { return m_closing && m_closeT >= 1.f; }
    float fadeAlpha()       const { return m_fadeIn * m_fade; }
    void debugStartInject()       { m_page = Page::GameList; }

private:
    void screenKeyInput(const core::Rect& panel, float alpha, float dt);
    void screenVerifying(const core::Rect& panel, float alpha, float dt);
    void screenFail(const core::Rect& panel, float alpha, float dt);
    void screenList(const core::Rect& panel, float alpha, float dt);
    void screenStatus(const core::Rect& panel, float alpha, float dt);
    void screenDownloading(const core::Rect& panel, float alpha, float dt);
    void screenDownloadFail(const core::Rect& panel, float alpha, float dt);
    void screenInjectSuccess(const core::Rect& panel, float alpha, float dt);
    void startVerification();
    void startDownload();

    Assets     m_assets;
    InjectFlow m_inject;

    Page        m_page = Page::KeyInput;

    // Key input
    std::string m_key;
    std::string m_keyError;
    bool        m_fieldFocused = false; // clears placeholder on first click

    // Verification — sequential sub-pages
    int   m_verifShown = 0;       // which icon page is currently displayed (0..3)
    float m_verifPageT = 0.f;     // 0→1 cross-fade timer for current icon page
    float m_verifDelay = 0.f;     // small hold before advancing

    // Fail
    std::string m_failMsg;
    float       m_failTimer = 3.99f;

    // Download
    DownloadState m_dlState;
    std::string   m_dlFailMsg;
    float         m_dlFailTimer = 4.99f;
    float         m_successCloseTimer = 3.99f;

    // Panel spring
    PanelSize  m_target;
    core::Rect m_panel;
    float m_curW=0.f, m_curH=0.f, m_velW=0.f, m_velH=0.f;
    bool  m_sizeInit = false;

    // Transitions
    float m_openT=0.f, m_fadeIn=0.f;
    float m_closeT=0.f, m_fade=1.f;
    bool  m_closing = false;
};

}
