#include "gfx/font.h"

#pragma comment(lib, "dwrite.lib")

namespace gfx {

static IDWriteFactory* g_dwrite = nullptr;
static IDWriteFactory5* g_dwrite5 = nullptr;
static IDWriteInMemoryFontFileLoader* g_memLoader = nullptr;

static IDWriteFactory* dwrite() {
    if (!g_dwrite) {
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                          (IUnknown**)&g_dwrite5)) &&
            g_dwrite5) {
            g_dwrite5->QueryInterface(__uuidof(IDWriteFactory), (void**)&g_dwrite);
        } else {
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwrite);
        }
    }
    return g_dwrite;
}

static IDWriteInMemoryFontFileLoader* memoryLoader() {
    dwrite();
    if (!g_dwrite5) return nullptr;
    if (!g_memLoader) {
        if (FAILED(g_dwrite5->CreateInMemoryFontFileLoader(&g_memLoader))) return nullptr;
        g_dwrite5->RegisterFontFileLoader(g_memLoader);
    }
    return g_memLoader;
}

int Font::decode(const char* text, int& i) {
    const uint8_t* s = (const uint8_t*)text;
    uint8_t c = s[i];
    if (c < 0x80) { i += 1; return c; }
    if ((c & 0xE0) == 0xC0) { int cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2; return cp; }
    if ((c & 0xF0) == 0xE0) { int cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3; return cp; }
    if ((c & 0xF8) == 0xF0) {
        int cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
        i += 4;
        return cp;
    }
    i += 1;
    return '?';
}

bool Font::create(ID3D11Device* dev, const wchar_t* family, float size, int weight) {
    m_dev = dev;
    m_size = size;

    IDWriteFactory* f = dwrite();
    if (!f) return false;

    IDWriteFontCollection* collection = nullptr;
    if (FAILED(f->GetSystemFontCollection(&collection, FALSE))) return false;

    UINT32 index = 0;
    BOOL exists = FALSE;
    collection->FindFamilyName(family, &index, &exists);
    if (!exists) {
        collection->FindFamilyName(L"Segoe UI", &index, &exists);
        if (!exists) { collection->Release(); return false; }
    }

    IDWriteFontFamily* fam = nullptr;
    collection->GetFontFamily(index, &fam);
    collection->Release();

    IDWriteFont* font = nullptr;
    fam->GetFirstMatchingFont((DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              &font);
    fam->Release();
    if (!font) return false;

    HRESULT hr = font->CreateFontFace(&m_face);
    font->Release();
    if (FAILED(hr)) return false;

    return finishSetup();
}

bool Font::createFromMemory(ID3D11Device* dev, const void* data, size_t size, float pixelSize) {
    m_dev = dev;
    m_size = pixelSize;

    IDWriteInMemoryFontFileLoader* loader = memoryLoader();
    if (!loader || !g_dwrite5) return false;

    IDWriteFontFile* file = nullptr;
    if (FAILED(loader->CreateInMemoryFontFileReference(g_dwrite5, data, (UINT32)size, nullptr, &file)))
        return false;

    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 faceCount = 0;
    file->Analyze(&supported, &fileType, &faceType, &faceCount);
    if (!supported || faceCount == 0) { file->Release(); return false; }

    HRESULT hr = g_dwrite5->CreateFontFace(faceType, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &m_face);
    file->Release();
    if (FAILED(hr)) return false;

    return finishSetup();
}

bool Font::finishSetup() {
    DWRITE_FONT_METRICS fm = {};
    m_face->GetMetrics(&fm);
    m_scale = m_size / (float)fm.designUnitsPerEm;
    m_ascent = fm.ascent * m_scale;
    m_descent = fm.descent * m_scale;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)m_atlasW;
    td.Height = (UINT)m_atlasH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<uint8_t> zero((size_t)m_atlasW * m_atlasH, 0);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = zero.data();
    sd.SysMemPitch = (UINT)m_atlasW;

    if (FAILED(m_dev->CreateTexture2D(&td, &sd, &m_tex))) return false;
    return SUCCEEDED(m_dev->CreateShaderResourceView(m_tex, nullptr, &m_srv));
}

bool Font::pack(int w, int h, int& outX, int& outY) {
    if (m_penX + w + 1 > m_atlasW) {
        m_penX = 1;
        m_penY += m_rowH + 1;
        m_rowH = 0;
    }
    if (m_penY + h + 1 > m_atlasH) return false;
    outX = m_penX;
    outY = m_penY;
    m_penX += w + 1;
    if (h > m_rowH) m_rowH = h;
    return true;
}

void Font::upload(int x, int y, int w, int h, const std::vector<uint8_t>& gray) {
    ID3D11DeviceContext* ctx = nullptr;
    m_dev->GetImmediateContext(&ctx);
    D3D11_BOX box = {};
    box.left = (UINT)x;
    box.top = (UINT)y;
    box.right = (UINT)(x + w);
    box.bottom = (UINT)(y + h);
    box.back = 1;
    ctx->UpdateSubresource(m_tex, 0, &box, gray.data(), (UINT)w, 0);
    ctx->Release();
}

const Glyph* Font::glyph(uint32_t codepoint) {
    auto it = m_cache.find(codepoint);
    if (it != m_cache.end()) return &it->second;
    if (!m_face) return nullptr;

    UINT16 gi = 0;
    m_face->GetGlyphIndices(&codepoint, 1, &gi);

    DWRITE_GLYPH_METRICS gm = {};
    m_face->GetDesignGlyphMetrics(&gi, 1, &gm, FALSE);

    Glyph g;
    g.advance = gm.advanceWidth * m_scale;

    float zeroAdvance = 0.f;
    DWRITE_GLYPH_OFFSET zeroOffset = {};
    DWRITE_GLYPH_RUN run = {};
    run.fontFace = m_face;
    run.fontEmSize = m_size;
    run.glyphCount = 1;
    run.glyphIndices = &gi;
    run.glyphAdvances = &zeroAdvance;
    run.glyphOffsets = &zeroOffset;

    IDWriteGlyphRunAnalysis* analysis = nullptr;
    if (SUCCEEDED(dwrite()->CreateGlyphRunAnalysis(&run, 1.f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                   DWRITE_MEASURING_MODE_NATURAL, 0.f, 0.f, &analysis)) &&
        analysis) {
        RECT bounds = {};
        analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
        int w = bounds.right - bounds.left;
        int h = bounds.bottom - bounds.top;
        if (w > 0 && h > 0) {
            std::vector<uint8_t> rgb((size_t)w * h * 3, 0);
            if (SUCCEEDED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, rgb.data(),
                                                       (UINT)rgb.size()))) {
                std::vector<uint8_t> gray((size_t)w * h, 0);
                for (size_t p = 0; p < gray.size(); ++p)
                    gray[p] = (uint8_t)((rgb[p * 3] + rgb[p * 3 + 1] + rgb[p * 3 + 2]) / 3);

                int ax = 0, ay = 0;
                if (pack(w, h, ax, ay)) {
                    upload(ax, ay, w, h, gray);
                    g.u0 = (float)ax / m_atlasW;
                    g.v0 = (float)ay / m_atlasH;
                    g.u1 = (float)(ax + w) / m_atlasW;
                    g.v1 = (float)(ay + h) / m_atlasH;
                    g.w = (float)w;
                    g.h = (float)h;
                    g.bearingX = (float)bounds.left;
                    g.bearingY = (float)bounds.top;
                }
            }
        }
        analysis->Release();
    }

    m_cache[codepoint] = g;
    return &m_cache[codepoint];
}

float Font::measure(const char* text, int len) {
    if (!text) return 0.f;
    if (len < 0) len = (int)strlen(text);
    float x = 0.f;
    int i = 0;
    while (i < len) {
        int cp = decode(text, i);
        const Glyph* g = glyph((uint32_t)cp);
        if (g) x += g->advance;
    }
    return x;
}

void Font::destroy() {
    if (m_srv) { m_srv->Release(); m_srv = nullptr; }
    if (m_tex) { m_tex->Release(); m_tex = nullptr; }
    if (m_face) { m_face->Release(); m_face = nullptr; }
    m_cache.clear();
}

}
