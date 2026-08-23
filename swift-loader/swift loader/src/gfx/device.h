#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <string>

namespace gfx {

class Device {
public:
    bool create(HWND hwnd, int width, int height);
    void destroy();
    void beginFrame();
    void present(bool vsync);
    bool captureBackbuffer(const std::wstring& path);

    ID3D11Device* dev() const { return m_dev; }
    ID3D11DeviceContext* ctx() const { return m_ctx; }

private:
    void createTarget();
    void releaseTarget();

    ID3D11Device* m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    IDXGISwapChain1* m_swap = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;
    IDCompositionDevice* m_comp = nullptr;
    IDCompositionTarget* m_compTarget = nullptr;
    IDCompositionVisual* m_visual = nullptr;
    int m_w = 0, m_h = 0;
};

}
