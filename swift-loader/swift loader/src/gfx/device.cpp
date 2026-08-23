#include "gfx/device.h"
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

namespace gfx {

static void safeRelease(IUnknown*& p) {
    if (p) { p->Release(); p = nullptr; }
}

bool Device::create(HWND hwnd, int width, int height) {
    m_w = width;
    m_h = height;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL got;

    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                 D3D11_SDK_VERSION, &m_dev, &got, &m_ctx)))
        return false;

    IDXGIDevice* dxgiDev = nullptr;
    if (FAILED(m_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev))) return false;

    IDXGIAdapter* adapter = nullptr;
    dxgiDev->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = (UINT)m_w;
    desc.Height = (UINT)m_h;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    HRESULT hr = factory->CreateSwapChainForComposition(m_dev, &desc, nullptr, &m_swap);
    safeRelease((IUnknown*&)factory);
    safeRelease((IUnknown*&)adapter);
    if (FAILED(hr)) { safeRelease((IUnknown*&)dxgiDev); return false; }

    hr = DCompositionCreateDevice(dxgiDev, __uuidof(IDCompositionDevice), (void**)&m_comp);
    safeRelease((IUnknown*&)dxgiDev);
    if (FAILED(hr)) return false;

    if (FAILED(m_comp->CreateTargetForHwnd(hwnd, TRUE, &m_compTarget))) return false;
    if (FAILED(m_comp->CreateVisual(&m_visual))) return false;

    m_visual->SetContent(m_swap);
    m_compTarget->SetRoot(m_visual);
    m_comp->Commit();

    createTarget();
    return m_rtv != nullptr;
}

void Device::createTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) {
        m_dev->CreateRenderTargetView(back, nullptr, &m_rtv);
        back->Release();
    }
}

void Device::releaseTarget() {
    safeRelease((IUnknown*&)m_rtv);
}

void Device::beginFrame() {
    const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    m_ctx->ClearRenderTargetView(m_rtv, clear);
    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_w;
    vp.Height = (float)m_h;
    vp.MaxDepth = 1.f;
    m_ctx->RSSetViewports(1, &vp);
}

void Device::present(bool vsync) {
    m_swap->Present(vsync ? 1 : 0, 0);
    if (m_comp) m_comp->Commit();
}

bool Device::captureBackbuffer(const std::wstring& path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(m_dev->CreateTexture2D(&desc, nullptr, &staging))) { back->Release(); return false; }
    m_ctx->CopyResource(staging, back);
    back->Release();

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(m_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) { staging->Release(); return false; }

    const int w = (int)desc.Width, h = (int)desc.Height;
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = (const uint8_t*)map.pData + (size_t)y * map.RowPitch;
        uint8_t* dst = pixels.data() + (size_t)(h - 1 - y) * w * 4;
        for (int x = 0; x < w; ++x) {
            float a = src[x * 4 + 3] / 255.f;
            float bg = 0.86f * (1.f - a);
            for (int c = 0; c < 3; ++c)
                dst[x * 4 + c] = (uint8_t)(std::min)(255.f, src[x * 4 + c] + bg * 255.f);
            dst[x * 4 + 3] = 255;
        }
    }
    m_ctx->Unmap(staging, 0);
    staging->Release();

    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)pixels.size();
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(file, &fh, sizeof(fh), &written, nullptr);
    WriteFile(file, &ih, sizeof(ih), &written, nullptr);
    WriteFile(file, pixels.data(), (DWORD)pixels.size(), &written, nullptr);
    CloseHandle(file);
    return true;
}

void Device::destroy() {
    releaseTarget();
    safeRelease((IUnknown*&)m_visual);
    safeRelease((IUnknown*&)m_compTarget);
    safeRelease((IUnknown*&)m_comp);
    safeRelease((IUnknown*&)m_swap);
    safeRelease((IUnknown*&)m_ctx);
    safeRelease((IUnknown*&)m_dev);
}

}
