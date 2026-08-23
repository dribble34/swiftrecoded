#include "gfx/renderer.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace gfx {

static const char* kShader = R"(
cbuffer CB : register(b0) { float4 uScreen; };

struct VSIn {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR0;
    float4 shape : TEXCOORD1;
    float4 style : TEXCOORD2;
};

struct PSIn {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR0;
    float4 shape : TEXCOORD1;
    float4 style : TEXCOORD2;
};

PSIn vs_main(VSIn i) {
    PSIn o;
    o.pos   = float4(i.pos.x * uScreen.x * 2.0 - 1.0, 1.0 - i.pos.y * uScreen.y * 2.0, 0.0, 1.0);
    o.uv    = i.uv;
    o.col   = i.col;
    o.shape = i.shape;
    o.style = i.style;
    return o;
}

Texture2D tex0 : register(t0);
SamplerState smp0 : register(s0);

float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 0.00001));
    return length(pa - ba * h);
}

float4 ps_main(PSIn i) : SV_Target {
    float4 sampled = tex0.Sample(smp0, i.uv);

    float boxD  = sdRoundBox(i.pos.xy - i.shape.xy, i.shape.zw, i.style.x);
    float ringD = abs(boxD + i.style.z * 0.5) - i.style.z * 0.5;
    float boxAA = max(fwidth(boxD), 0.0001);

    float2 rel  = i.pos.xy - i.shape.xy;
    float dist  = length(rel);
    float arcD  = abs(dist - i.style.x) - i.style.z * 0.5;
    float arcAA = max(fwidth(arcD), 0.0001);
    float ang   = atan2(rel.y, rel.x);

    int mode = (int)(i.style.w + 0.5);
    float3 rgb = i.col.rgb;
    float a = i.col.a;

    if (mode == 6) {
    } else if (mode == 7) {
        float d = sdSegment(i.pos.xy, i.shape.xy, i.shape.zw) - i.style.x;
        a *= saturate(0.5 - d / max(fwidth(d), 0.0001));
    } else if (mode == 8) {
        float2 q = float2(i.pos.x, i.shape.w - abs(i.pos.y - i.shape.w));
        float d = sdSegment(q, i.shape.xy, i.shape.zw) - i.style.x;
        a *= saturate(0.5 - d / max(fwidth(d), 0.0001));
    } else if (mode == 4) {
        a *= sampled.r;
    } else if (mode == 5) {
        a *= saturate(0.5 - arcD / arcAA);
        float sweep = i.uv.y - i.uv.x;
        if (sweep < 6.2831853) {
            float t = ang - i.uv.x;
            t = t - 6.2831853 * floor(t / 6.2831853);
            float angAA = max(2.0 / max(i.style.x, 1.0), 0.0005);
            a *= min(smoothstep(0.0, angAA, t), smoothstep(0.0, angAA, sweep - t));
        }
    } else if (mode == 2) {
        float soft = max(i.style.y, 0.5);
        a *= 1.0 - smoothstep(-soft, soft, boxD);
    } else if (mode == 1) {
        a *= saturate(0.5 - ringD / boxAA);
    } else if (mode == 3) {
        a *= saturate(0.5 - boxD / boxAA);
        float lum = dot(sampled.rgb, float3(0.299, 0.587, 0.114));
        rgb *= lerp(sampled.rgb, lum.xxx, saturate(i.style.y));
        a *= sampled.a;
    } else {
        float soft = i.style.y;
        if (soft > 0.0) a *= 1.0 - smoothstep(-soft, soft, boxD);
        else a *= saturate(0.5 - boxD / boxAA);
    }

    return float4(rgb * a, a) * uScreen.z;
}
)";

bool Renderer::create(ID3D11Device* dev) {
    m_dev = dev;

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* err = nullptr;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

    if (FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "vs_main", "vs_4_0", flags, 0,
                          &vsBlob, &err))) {
        if (err) { MessageBoxA(nullptr, (const char*)err->GetBufferPointer(), "VS", MB_OK); err->Release(); }
        return false;
    }
    if (FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "ps_main", "ps_4_0", flags, 0,
                          &psBlob, &err))) {
        if (err) { MessageBoxA(nullptr, (const char*)err->GetBufferPointer(), "PS", MB_OK); err->Release(); }
        vsBlob->Release();
        return false;
    }

    dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
    dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps);

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    dev->CreateInputLayout(elems, ARRAYSIZE(elems), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_layout);
    vsBlob->Release();
    psBlob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = 16;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cbd, nullptr, &m_cb);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, &m_blend);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    rd.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rd, &m_raster);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &m_sampler);

    return m_vs && m_ps && m_layout;
}

bool Renderer::ensureBuffers(size_t vtxCount, size_t idxCount) {
    if (vtxCount > m_vbCapacity) {
        if (m_vb) { m_vb->Release(); m_vb = nullptr; }
        m_vbCapacity = vtxCount + 4096;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (UINT)(m_vbCapacity * sizeof(Vertex));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_vb))) return false;
    }
    if (idxCount > m_ibCapacity) {
        if (m_ib) { m_ib->Release(); m_ib = nullptr; }
        m_ibCapacity = idxCount + 8192;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (UINT)(m_ibCapacity * sizeof(uint32_t));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_ib))) return false;
    }
    return true;
}

void Renderer::render(ID3D11DeviceContext* ctx, const DrawList& list, int screenW, int screenH,
                      float globalAlpha) {
    const auto& vtx = list.vertices();
    const auto& idx = list.indices();
    if (vtx.empty() || idx.empty()) return;
    if (!ensureBuffers(vtx.size(), idx.size())) return;

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (SUCCEEDED(ctx->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, vtx.data(), vtx.size() * sizeof(Vertex));
        ctx->Unmap(m_vb, 0);
    }
    if (SUCCEEDED(ctx->Map(m_ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, idx.data(), idx.size() * sizeof(uint32_t));
        ctx->Unmap(m_ib, 0);
    }
    if (SUCCEEDED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float data[4] = { 1.f / (float)screenW, 1.f / (float)screenH, globalAlpha, 0.f };
        memcpy(map.pData, data, sizeof(data));
        ctx->Unmap(m_cb, 0);
    }

    UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetInputLayout(m_layout);
    ctx->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    ctx->IASetIndexBuffer(m_ib, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_vs, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &m_cb);
    ctx->PSSetShader(m_ps, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &m_cb);
    ctx->PSSetSamplers(0, 1, &m_sampler);
    ctx->RSSetState(m_raster);

    const float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendState(m_blend, blendFactor, 0xFFFFFFFF);

    for (const Cmd& cmd : list.commands()) {
        if (cmd.idxCount == 0) continue;
        if (cmd.clip.w <= 0.f || cmd.clip.h <= 0.f) continue;

        D3D11_RECT scissor;
        scissor.left = (LONG)cmd.clip.x;
        scissor.top = (LONG)cmd.clip.y;
        scissor.right = (LONG)std::ceil(cmd.clip.r());
        scissor.bottom = (LONG)std::ceil(cmd.clip.b());
        ctx->RSSetScissorRects(1, &scissor);

        ID3D11ShaderResourceView* srv = cmd.srv;
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->DrawIndexed(cmd.idxCount, cmd.idxOffset, 0);
    }
}

void Renderer::destroy() {
    auto rel = [](IUnknown* p) { if (p) p->Release(); };
    rel(m_sampler); rel(m_raster); rel(m_blend);
    rel(m_cb); rel(m_ib); rel(m_vb);
    rel(m_layout); rel(m_ps); rel(m_vs);
    m_sampler = nullptr; m_raster = nullptr; m_blend = nullptr;
    m_cb = nullptr; m_ib = nullptr; m_vb = nullptr;
    m_layout = nullptr; m_ps = nullptr; m_vs = nullptr;
}

}
