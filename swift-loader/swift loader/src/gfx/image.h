#pragma once
#include <d3d11.h>
#include <string>

namespace gfx {

struct Image {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
    bool valid() const { return srv != nullptr; }
    float aspect() const { return h > 0 ? (float)w / (float)h : 1.f; }
};

bool loadImage(ID3D11Device* dev, const std::wstring& path, Image& out);
bool loadImageMemory(ID3D11Device* dev, const void* data, size_t size, Image& out);
void releaseImage(Image& img);

}
