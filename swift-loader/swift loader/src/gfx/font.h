#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <unordered_map>
#include <string>
#include <vector>
#include "core/types.h"

namespace gfx {

struct Glyph {
    float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;
    float w = 0.f, h = 0.f;
    float bearingX = 0.f, bearingY = 0.f;
    float advance = 0.f;
};

class Font {
public:
    bool create(ID3D11Device* dev, const wchar_t* family, float size, int weight);
    bool createFromMemory(ID3D11Device* dev, const void* data, size_t size, float pixelSize);
    void destroy();

    const Glyph* glyph(uint32_t codepoint);
    float measure(const char* text, int len = -1);
    float ascent() const { return m_ascent; }
    float descent() const { return m_descent; }
    ID3D11ShaderResourceView* srv() const { return m_srv; }

    static int decode(const char* text, int& i);

private:
    bool finishSetup();
    bool pack(int w, int h, int& outX, int& outY);
    void upload(int x, int y, int w, int h, const std::vector<uint8_t>& gray);

    ID3D11Device* m_dev = nullptr;
    ID3D11Texture2D* m_tex = nullptr;
    ID3D11ShaderResourceView* m_srv = nullptr;
    IDWriteFontFace* m_face = nullptr;

    std::unordered_map<uint32_t, Glyph> m_cache;
    float m_size = 0.f, m_ascent = 0.f, m_descent = 0.f, m_scale = 0.f;
    int m_atlasW = 512, m_atlasH = 512;
    int m_penX = 1, m_penY = 1, m_rowH = 0;
};

}
