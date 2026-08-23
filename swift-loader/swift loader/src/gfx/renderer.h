#pragma once
#include <d3d11.h>
#include "gfx/drawlist.h"

namespace gfx {

class Renderer {
public:
    bool create(ID3D11Device* dev);
    void destroy();
    void render(ID3D11DeviceContext* ctx, const DrawList& list, int screenW, int screenH,
                float globalAlpha = 1.f);

private:
    bool ensureBuffers(size_t vtxCount, size_t idxCount);

    ID3D11Device* m_dev = nullptr;
    ID3D11VertexShader* m_vs = nullptr;
    ID3D11PixelShader* m_ps = nullptr;
    ID3D11InputLayout* m_layout = nullptr;
    ID3D11Buffer* m_vb = nullptr;
    ID3D11Buffer* m_ib = nullptr;
    ID3D11Buffer* m_cb = nullptr;
    ID3D11BlendState* m_blend = nullptr;
    ID3D11RasterizerState* m_raster = nullptr;
    ID3D11SamplerState* m_sampler = nullptr;
    size_t m_vbCapacity = 0, m_ibCapacity = 0;
};

}
