#include "gfx/image.h"
#include <windows.h>
#include <wincodec.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")

namespace gfx {

static IWICImagingFactory* g_wic = nullptr;

static IWICImagingFactory* wic() {
    if (!g_wic)
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
    return g_wic;
}

static bool decodeToTexture(ID3D11Device* dev, IWICBitmapDecoder* decoder, Image& out) {
    IWICImagingFactory* factory = wic();
    if (!factory || !decoder) return false;

    UINT frameCount = 1;
    decoder->GetFrameCount(&frameCount);

    UINT bestFrame = 0, bestArea = 0;
    for (UINT i = 0; i < frameCount; ++i) {
        IWICBitmapFrameDecode* probe = nullptr;
        if (FAILED(decoder->GetFrame(i, &probe))) continue;
        UINT pw = 0, ph = 0;
        probe->GetSize(&pw, &ph);
        if (pw * ph > bestArea) { bestArea = pw * ph; bestFrame = i; }
        probe->Release();
    }

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(bestFrame, &frame))) return false;

    IWICFormatConverter* converter = nullptr;
    if (FAILED(factory->CreateFormatConverter(&converter))) { frame->Release(); return false; }
    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                     WICBitmapPaletteTypeCustom))) {
        converter->Release();
        frame->Release();
        return false;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    HRESULT hr = converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());

    converter->Release();
    frame->Release();
    if (FAILED(hr) || w == 0 || h == 0) return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = pixels.data();
    sd.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &sd, &tex))) return false;

    hr = dev->CreateShaderResourceView(tex, nullptr, &out.srv);
    tex->Release();
    if (FAILED(hr)) return false;

    out.w = (int)w;
    out.h = (int)h;
    return true;
}

bool loadImage(ID3D11Device* dev, const std::wstring& path, Image& out) {
    IWICImagingFactory* factory = wic();
    if (!factory) return false;

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;

    bool ok = decodeToTexture(dev, decoder, out);
    decoder->Release();
    return ok;
}

bool loadImageMemory(ID3D11Device* dev, const void* data, size_t size, Image& out) {
    IWICImagingFactory* factory = wic();
    if (!factory || !data || size == 0) return false;

    IWICStream* stream = nullptr;
    if (FAILED(factory->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory((BYTE*)const_cast<void*>(data), (DWORD)size))) {
        stream->Release();
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr)) return false;

    bool ok = decodeToTexture(dev, decoder, out);
    decoder->Release();
    return ok;
}

void releaseImage(Image& img) {
    if (img.srv) { img.srv->Release(); img.srv = nullptr; }
    img.w = img.h = 0;
}

}
