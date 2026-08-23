#include "gfx/drawlist.h"

namespace gfx {

void DrawList::reset(const Rect& viewport) {
    m_vtx.clear();
    m_idx.clear();
    m_cmds.clear();
    m_clipStack.clear();
    m_clipStack.push_back(viewport);

    Cmd cmd;
    cmd.clip = viewport;
    m_cmds.push_back(cmd);
}

void DrawList::reserve(int verts, int inds) {
    m_vtx.reserve(m_vtx.size() + verts);
    m_idx.reserve(m_idx.size() + inds);
}

void DrawList::pushClip(const Rect& r) {
    Rect merged = r.clipTo(m_clipStack.back());
    m_clipStack.push_back(merged);
    Cmd cmd;
    cmd.idxOffset = (uint32_t)m_idx.size();
    cmd.clip = merged;
    cmd.srv = m_cmds.back().srv;
    m_cmds.push_back(cmd);
}

void DrawList::popClip() {
    if (m_clipStack.size() <= 1) return;
    m_clipStack.pop_back();
    Cmd cmd;
    cmd.idxOffset = (uint32_t)m_idx.size();
    cmd.clip = m_clipStack.back();
    cmd.srv = m_cmds.back().srv;
    m_cmds.push_back(cmd);
}

void DrawList::setTexture(ID3D11ShaderResourceView* srv) {
    if (m_cmds.back().srv == srv) return;
    if (m_cmds.back().idxCount == 0) {
        m_cmds.back().srv = srv;
        return;
    }
    Cmd cmd;
    cmd.idxOffset = (uint32_t)m_idx.size();
    cmd.clip = m_clipStack.back();
    cmd.srv = srv;
    m_cmds.push_back(cmd);
}

void DrawList::quad(const Rect& dst, const Rect& uv, const Col& c0, const Col& c1, bool horizontal,
                    const Rect& shape, float radius, float soft, float border, Mode mode) {
    if (dst.w <= 0.f || dst.h <= 0.f) return;
    if (c0.a <= 0.001f && c1.a <= 0.001f) return;

    const uint32_t base = (uint32_t)m_vtx.size();
    const Vec2 sc = shape.center();
    const float hw = shape.w * 0.5f, hh = shape.h * 0.5f;

    Vertex v;
    v.cx = sc.x;
    v.cy = sc.y;
    v.hw = hw;
    v.hh = hh;
    v.radius = (std::min)(radius, (std::min)(hw, hh));
    v.soft = soft;
    v.border = border;
    v.mode = (float)mode;

    const uint32_t p0 = c0.packed();
    const uint32_t p1 = c1.packed();

    reserve(4, 6);
    v.col = p0;                   v.x = dst.x;   v.y = dst.y;   v.u = uv.x;        v.v = uv.y;        m_vtx.push_back(v);
    v.col = horizontal ? p1 : p0; v.x = dst.r(); v.y = dst.y;   v.u = uv.x + uv.w; v.v = uv.y;        m_vtx.push_back(v);
    v.col = p1;                   v.x = dst.r(); v.y = dst.b(); v.u = uv.x + uv.w; v.v = uv.y + uv.h; m_vtx.push_back(v);
    v.col = horizontal ? p0 : p1; v.x = dst.x;   v.y = dst.b(); v.u = uv.x;        v.v = uv.y + uv.h; m_vtx.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) m_idx.push_back(base + order[i]);
    m_cmds.back().idxCount += 6;
}

void DrawList::rect(const Rect& r, const Col& c, float radius, float soft) {
    setTexture(nullptr);
    quad(r.expand(1.f), Rect(0, 0, 0, 0), c, c, false, r, radius, soft, 0.f, MODE_FILL);
}

void DrawList::border(const Rect& r, const Col& c, float thickness, float radius) {
    if (thickness <= 0.f) return;
    setTexture(nullptr);
    quad(r.expand(thickness + 1.f), Rect(0, 0, 0, 0), c, c, false, r, radius, 0.f, thickness, MODE_BORDER);
}

void DrawList::shadow(const Rect& r, const Col& c, float blur, float radius, const Vec2& offset) {
    setTexture(nullptr);
    Rect shape = r.offset(offset.x, offset.y);
    quad(shape.expand(blur + 2.f), Rect(0, 0, 0, 0), c, c, false, shape, radius, blur, 0.f, MODE_SHADOW);
}

void DrawList::circle(const Vec2& center, float radius, const Col& c) {
    rect(Rect(center.x - radius, center.y - radius, radius * 2.f, radius * 2.f), c, radius);
}

void DrawList::arc(const Vec2& center, float radius, float thickness, float a0, float a1, const Col& c) {
    arcGradient(center, radius, thickness, a0, a1, c, c);
}

void DrawList::arcGradient(const Vec2& center, float radius, float thickness, float a0, float a1, const Col& left,
                           const Col& right) {
    if (left.a <= 0.001f && right.a <= 0.001f) return;
    setTexture(nullptr);

    const float outer = radius + thickness * 0.5f + 2.f;
    Rect dst(center.x - outer, center.y - outer, outer * 2.f, outer * 2.f);

    const uint32_t base = (uint32_t)m_vtx.size();
    Vertex v;
    v.cx = center.x;
    v.cy = center.y;
    v.hw = radius;
    v.hh = radius;
    v.radius = radius;
    v.soft = 0.f;
    v.border = thickness;
    v.mode = (float)MODE_ARC;
    v.u = a0;
    v.v = a1;

    reserve(4, 6);
    v.col = left.packed();  v.x = dst.x;   v.y = dst.y;   m_vtx.push_back(v);
    v.col = right.packed(); v.x = dst.r(); v.y = dst.y;   m_vtx.push_back(v);
    v.col = right.packed(); v.x = dst.r(); v.y = dst.b(); m_vtx.push_back(v);
    v.col = left.packed();  v.x = dst.x;   v.y = dst.b(); m_vtx.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) m_idx.push_back(base + order[i]);
    m_cmds.back().idxCount += 6;
}

void DrawList::triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Col& col) {
    if (col.a <= 0.001f) return;
    setTexture(nullptr);

    const uint32_t base = (uint32_t)m_vtx.size();
    Vertex v;
    v.col = col.packed();
    v.u = v.v = 0.f;
    v.cx = v.cy = 0.f;
    v.hw = v.hh = 0.f;
    v.radius = v.soft = v.border = 0.f;
    v.mode = (float)MODE_RAW;

    reserve(3, 3);
    v.x = a.x; v.y = a.y; m_vtx.push_back(v);
    v.x = b.x; v.y = b.y; m_vtx.push_back(v);
    v.x = c.x; v.y = c.y; m_vtx.push_back(v);
    m_idx.push_back(base);
    m_idx.push_back(base + 1);
    m_idx.push_back(base + 2);
    m_cmds.back().idxCount += 3;
}

void DrawList::star(const Vec2& c, float radius, float sharpness, const Col& col) {
    if (col.a <= 0.001f || radius <= 0.05f) return;

    const int segments = 32;
    const float core = 0.10f;
    Vec2 prev;
    for (int i = 0; i <= segments; ++i) {
        const float a = (float)i / (float)segments * core::kPi * 2.f;
        const float lobe = std::pow(std::fabs(std::cos(a * 2.f)), sharpness);
        const float r = radius * (core + (1.f - core) * lobe);
        const Vec2 p(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        if (i > 0) triangle(c, prev, p, col);
        prev = p;
    }
}

void DrawList::line(const Vec2& a, const Vec2& b, float thickness, const Col& c) {
    if (c.a <= 0.001f) return;
    setTexture(nullptr);

    const float pad = thickness * 0.5f + 1.5f;
    Rect dst((std::min)(a.x, b.x) - pad, (std::min)(a.y, b.y) - pad,
             std::fabs(b.x - a.x) + pad * 2.f, std::fabs(b.y - a.y) + pad * 2.f);

    const uint32_t base = (uint32_t)m_vtx.size();
    Vertex v;
    v.col = c.packed();
    v.u = v.v = 0.f;
    v.cx = a.x;
    v.cy = a.y;
    v.hw = b.x;
    v.hh = b.y;
    v.radius = thickness * 0.5f;
    v.soft = 0.f;
    v.border = 0.f;
    v.mode = (float)MODE_SEGMENT;

    reserve(4, 6);
    v.x = dst.x;   v.y = dst.y;   m_vtx.push_back(v);
    v.x = dst.r(); v.y = dst.y;   m_vtx.push_back(v);
    v.x = dst.r(); v.y = dst.b(); m_vtx.push_back(v);
    v.x = dst.x;   v.y = dst.b(); m_vtx.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) m_idx.push_back(base + order[i]);
    m_cmds.back().idxCount += 6;
}

void DrawList::chevronShape(const Vec2& tip, const Vec2& vertex, float thickness, const Col& c) {
    if (c.a <= 0.001f) return;
    setTexture(nullptr);

    const float pad = thickness * 0.5f + 1.5f;
    const float halfH = std::fabs(vertex.y - tip.y);
    Rect dst(tip.x - pad, vertex.y - halfH - pad, (vertex.x - tip.x) + pad * 2.f, halfH * 2.f + pad * 2.f);

    const uint32_t base = (uint32_t)m_vtx.size();
    Vertex v;
    v.col = c.packed();
    v.u = v.v = 0.f;
    v.cx = tip.x;
    v.cy = tip.y;
    v.hw = vertex.x;
    v.hh = vertex.y;
    v.radius = thickness * 0.5f;
    v.soft = 0.f;
    v.border = 0.f;
    v.mode = (float)MODE_CHEVRON;

    reserve(4, 6);
    v.x = dst.x;   v.y = dst.y;   m_vtx.push_back(v);
    v.x = dst.r(); v.y = dst.y;   m_vtx.push_back(v);
    v.x = dst.r(); v.y = dst.b(); m_vtx.push_back(v);
    v.x = dst.x;   v.y = dst.b(); m_vtx.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) m_idx.push_back(base + order[i]);
    m_cmds.back().idxCount += 6;
}

void DrawList::gradientVMasked(const Rect& r, const Col& top, const Col& bottom, const Rect& shape, float radius) {
    setTexture(nullptr);
    quad(r, Rect(0, 0, 0, 0), top, bottom, false, shape, radius, 0.f, 0.f, MODE_FILL);
}

void DrawList::gradientHMasked(const Rect& r, const Col& left, const Col& right, const Rect& shape, float radius) {
    setTexture(nullptr);
    quad(r, Rect(0, 0, 0, 0), left, right, true, shape, radius, 0.f, 0.f, MODE_FILL);
}

void DrawList::image(ID3D11ShaderResourceView* srv, const Rect& dst, const Col& tint, float radius, const Rect& uv,
                     float desaturate) {
    if (!srv) return;
    setTexture(srv);
    quad(dst, uv, tint, tint, false, dst, radius, desaturate, 0.f, MODE_IMAGE);
}

void DrawList::imageShaped(ID3D11ShaderResourceView* srv, const Rect& dst, const Col& tint, const Rect& shape,
                           float radius, const Rect& uv, float desaturate) {
    if (!srv) return;
    setTexture(srv);
    quad(dst, uv, tint, tint, false, shape, radius, desaturate, 0.f, MODE_IMAGE);
}

void DrawList::text(Font* font, const Vec2& pos, const Col& c, const char* txt, float tracking, int len) {
    if (!font || !txt || c.a <= 0.001f) return;
    if (len < 0) len = (int)strlen(txt);
    setTexture(font->srv());

    float x = pos.x;
    const float baseline = pos.y + font->ascent();
    int i = 0;
    while (i < len) {
        int cp = Font::decode(txt, i);
        const Glyph* g = font->glyph((uint32_t)cp);
        if (!g) continue;
        if (g->w > 0.f && g->h > 0.f) {
            Rect dst(std::floor(x + g->bearingX), std::floor(baseline + g->bearingY), g->w, g->h);
            Rect uv(g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0);
            setTexture(font->srv());
            quad(dst, uv, c, c, false, dst, 0.f, 0.f, 0.f, MODE_TEXT);
        }
        x += g->advance + tracking;
    }
}

}
