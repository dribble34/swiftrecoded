#include "ui/ui.h"

namespace ui {

static Context g_ctx;

Context& g() { return g_ctx; }

bool init(ID3D11Device* dev, const FontData& data) {
    Fonts& f = g_ctx.fonts;
    bool ok = true;

    if (data.bold && data.boldSize)
        ok &= f.title.createFromMemory(dev, data.bold, data.boldSize, 15.f);
    else
        ok &= f.title.create(dev, L"Segoe UI Variable Display", 15.f, 700);

    if (data.semiBold && data.semiBoldSize)
        ok &= f.body.createFromMemory(dev, data.semiBold, data.semiBoldSize, 13.f);
    else
        ok &= f.body.create(dev, L"Segoe UI Variable Text", 13.f, 600);
    return ok;
}

void shutdown() {
    g_ctx.fonts.title.destroy();
    g_ctx.fonts.body.destroy();
    g_ctx.anims.clear();
}

void newFrame(const Input& in, float dt, float width, float height) {
    g_ctx.input = in;
    g_ctx.dt = dt;
    g_ctx.time += dt;
    g_ctx.width = width;
    g_ctx.height = height;
    g_ctx.hot = g_ctx.hotNext;
    g_ctx.hotNext = 0;
    g_ctx.draw.reset(Rect(0.f, 0.f, width, height));
}

void endFrame() {
    if (!g_ctx.input.down) g_ctx.active = 0;
}

uint32_t id(const char* str) {
    uint32_t h = 2166136261u;
    for (const char* p = str; *p; ++p) {
        h ^= (uint32_t)(uint8_t)*p;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static AnimState& state(uint32_t widget, int slot) {
    return g_ctx.anims[((uint64_t)widget << 8) | (uint64_t)(uint8_t)slot];
}

float anim(uint32_t widget, int slot, float target, float speed) {
    AnimState& s = state(widget, slot);
    if (!s.initialized) {
        s.initialized = true;
        s.value = target;
        return s.value;
    }
    s.value = core::approach(s.value, target, speed, g_ctx.dt);
    if (std::fabs(s.value - target) < 0.0005f) s.value = target;
    return s.value;
}

bool hovered(uint32_t widget, const Rect& r) {
    bool inside = r.contains(g_ctx.input.mouse) && r.clipTo(g_ctx.draw.currentClip()).contains(g_ctx.input.mouse);
    if (inside && (g_ctx.active == 0 || g_ctx.active == widget)) g_ctx.hotNext = widget;
    return inside && g_ctx.hot == widget;
}

bool clicked(uint32_t widget, const Rect& r) {
    bool over = hovered(widget, r);
    if (over && g_ctx.input.pressed) g_ctx.active = widget;
    if (g_ctx.active == widget && g_ctx.input.released) {
        g_ctx.active = 0;
        return over;
    }
    return false;
}

void textShadow(gfx::Font& font, const Rect& box, const char* str, const Col& col, AlignH h, AlignV v,
                float tracking, float shadowAlpha, float offsetY) {
    text(font, box.offset(0.f, offsetY), str, Col::hex(0x000000, shadowAlpha * col.a), h, v, tracking);
    text(font, box, str, col, h, v, tracking);
}

void text(gfx::Font& font, const Rect& box, const char* str, const Col& col, AlignH h, AlignV v, float tracking) {
    if (!str || !*str) return;
    float w = font.measure(str);
    if (tracking != 0.f) {
        int count = 0;
        for (int i = 0; str[i];) { gfx::Font::decode(str, i); ++count; }
        if (count > 0) w += tracking * (count - 1);
    }

    float x = box.x;
    if (h == AlignH::Center) x = box.x + (box.w - w) * 0.5f;
    else if (h == AlignH::Right) x = box.r() - w;

    float y = box.y;
    if (v == AlignV::Middle) y = box.y + (box.h - (font.ascent() + font.descent())) * 0.5f;
    else if (v == AlignV::Bottom) y = box.b() - (font.ascent() + font.descent());

    g_ctx.draw.text(&font, Vec2(std::floor(x), std::floor(y)), col, str, tracking);
}

}
