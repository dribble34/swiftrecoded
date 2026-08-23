#include "ui/widgets.h"

namespace ui {

struct Star { float x, y, s, a, ph; };
static const Star kBarStars[] = {
    {0.10f, 0.32f, 4.0f, 0.85f, 0.0f}, {0.34f, 0.68f, 2.8f, 0.55f, 1.8f},
    {0.55f, 0.26f, 4.4f, 0.92f, 0.9f}, {0.78f, 0.64f, 3.0f, 0.62f, 2.6f},
    {0.94f, 0.34f, 3.6f, 0.76f, 1.3f},
};
static const Star kBarDust[] = {
    {0.04f, 0.66f, 0.9f, 0.35f, 0.4f}, {0.17f, 0.22f, 0.9f, 0.28f, 2.1f},
    {0.24f, 0.74f, 1.0f, 0.38f, 1.2f}, {0.42f, 0.20f, 0.9f, 0.26f, 3.0f},
    {0.47f, 0.76f, 0.9f, 0.32f, 0.7f}, {0.63f, 0.72f, 1.0f, 0.36f, 2.4f},
    {0.68f, 0.22f, 0.9f, 0.24f, 1.6f}, {0.86f, 0.76f, 0.9f, 0.30f, 0.2f},
    {0.90f, 0.18f, 0.9f, 0.28f, 2.8f},
};

void titleBar(const Rect& panel, float barHeight, const char* title, float time) {
    Theme& t = theme();
    Rect bar(panel.x, panel.y, panel.w, barHeight);
    dl().gradientHMasked(bar, t.bar, t.bar2, panel, t.radius);
    dl().gradientVMasked(bar, Col::hex(0xFFFFFF, 0.0f), Col::hex(0xFFFFFF, 0.03f), panel, t.radius);
    dl().pushClip(bar);
    float tx = bar.x + 18.f;
    float tw = fonts().title.measure(title);
    float sx0 = tx + tw + 16.f;
    float sx1 = bar.r() - 34.f;
    float sw = sx1 - sx0;
    if (sw > 12.f) {
        auto bandX = [&](float u){ return sx0 + sw * core::clamp01(u); };
        float arcY = bar.y + bar.h * 0.50f;
        int dots = 26;
        for (int i = 0; i < dots; ++i) {
            float u = (float)i / (float)(dots - 1);
            float curve = std::sin(u * core::kPi) * bar.h * 0.26f;
            float fade = std::sin(u * core::kPi);
            dl().circle(Vec2(bandX(u), arcY - curve), 0.7f, Col::hex(0xFFFFFF, 0.18f * fade));
        }
        for (auto &s : kBarDust) {
            float twk = 0.45f + 0.55f * std::sin(time * 0.62f + s.ph * 2.f);
            dl().circle(Vec2(bandX(s.x), bar.y + bar.h * s.y), s.s * 0.85f, Col::hex(0xFFFFFF, s.a * twk * 0.32f));
        }
        for (auto &s : kBarStars) {
            float twk = 0.62f + 0.38f * std::sin(time * 0.72f + s.ph);
            Vec2 c(bandX(s.x), bar.y + bar.h * s.y);
            float r = s.s * twk * 0.85f;
            dl().shadow(Rect(c.x - r*0.5f, c.y - r*0.5f, r, r), Col::hex(0xFFFFFF, 0.10f * twk), r*1.9f, r*0.5f, Vec2());
            dl().star(c, r, 0.85f, Col::hex(0xFFFFFF, s.a * twk));
        }
    }
    dl().popClip();
    Rect titleBox(tx, bar.y, 160.f, bar.h);
    ui::text(fonts().title, titleBox, title, t.text, AlignH::Left, AlignV::Middle, 0.6f);
}

void skyBackground(const Rect& area, const Rect& shape, float radius, float time, float alpha, float starScale) {
    Theme& t = theme();
    dl().rect(area, t.rowHover.alpha(alpha), radius);
}

bool closeButton(const char* name, const Rect& r) {
    const uint32_t wid = id(name);
    const bool over = hovered(wid, r);
    const bool result = clicked(wid, r);
    const float hoverT = anim(wid, 0, over ? 1.f : 0.f, 10.f);
    if (hoverT > 0.01f)
        dl().circle(r.center(), r.w * 0.5f, Col::hex(0xFFFFFF, 0.08f * hoverT));
    const Vec2 c = r.center();
    const float s = 4.2f;
    const Col line = Col::hex(0xF2F2F3, 0.72f + 0.28f * hoverT);
    dl().line(Vec2(c.x - s, c.y - s), Vec2(c.x + s, c.y + s), 1.5f, line);
    dl().line(Vec2(c.x + s, c.y - s), Vec2(c.x - s, c.y + s), 1.5f, line);
    return result;
}

void chevron(const Vec2& center, float height, const Col& c) {
    const float halfH = height * 0.5f;
    const float halfW = height * 0.552f * 0.5f;
    const float thickness = height * 0.145f;
    dl().chevronShape(Vec2(center.x - halfW, center.y - halfH), Vec2(center.x + halfW, center.y), thickness, c);
}

bool gameRow(const char* name, const Rect& r, gfx::Image* icon, const char* title) {
    Theme& t = theme();
    const uint32_t wid = id(name);
    const bool over = hovered(wid, r);
    const bool result = clicked(wid, r);
    const float hoverT = core::easeOutCubic(anim(wid, 0, over ? 1.f : 0.f, 9.f));
    const float pressT = anim(wid, 1, (over && g().input.down) ? 1.f : 0.f, 14.f);
    const float radius = 12.f;
    Rect box = r.offset(0.f, pressT * 1.f);
    if (hoverT > 0.004f)
        dl().shadow(box.shrink(1.f), Col(0.f, 0.f, 0.f, 0.20f * hoverT), 20.f, radius, Vec2(0.f, 6.f));
    Col bg = Col::hex(0x27272B, 1.f);
    dl().rect(box, bg, radius);
    if (hoverT > 0.02f) {
        dl().pushClip(box);
        for (auto &s : kBarDust) {
            float twk = 0.40f + 0.60f * std::sin(g().time * 0.58f + s.ph * 2.3f);
            dl().circle(Vec2(box.x + box.w * s.x, box.y + box.h * s.y), s.s * 0.75f, Col::hex(0xFFFFFF, s.a * twk * hoverT * 0.30f));
        }
        for (auto &s : kBarStars) {
            float twk = 0.62f + 0.38f * std::sin(g().time * 0.68f + s.ph);
            Vec2 c(box.x + box.w * s.x, box.y + box.h * s.y);
            float r2 = s.s * twk * 0.55f;
            dl().star(c, r2, 0.85f, Col::hex(0xFFFFFF, s.a * twk * hoverT * 0.55f));
        }
        dl().popClip();
    }
    dl().border(box.shrink(0.5f), hoverT > 0.01f ? t.borderHi.alpha(hoverT) : t.border, 1.f, radius - 0.5f);
    float textX = box.x + 14.f;
    if (icon && icon->valid()) {
        const float s = 22.f;
        Rect iconRect(box.x + 10.f, box.center().y - s * 0.5f, s, s);
        dl().rect(iconRect.expand(1.f), Col::hex(0xFFFFFF, 0.04f), 6.f);
        dl().image(icon->srv, iconRect, Col(1.f, 1.f, 1.f, 1.f), 6.f);
        textX = box.x + 42.f;
    }
    ui::text(fonts().body, Rect(textX, box.y, box.w * 0.7f, box.h), title, t.text, AlignH::Left, AlignV::Middle, 0.1f);
    {
        float a = 0.16f + hoverT * 0.30f;
        Vec2 pillC(box.r() - 18.f, box.center().y);
        dl().circle(pillC, 11.f, Col::hex(0xFFFFFF, a * 0.45f));
        chevron(Vec2(pillC.x + 0.5f, pillC.y), 8.f, Col::hex(0xF2F2F3, 0.75f + 0.25f * hoverT));
    }
    return result;
}

void loadingLine(const Rect& r, float phase, float time, float alpha) {
    const float radius = r.h * 0.5f;
    dl().rect(r, Col::hex(0x2A2A2E, 1.f * alpha), radius);
    const float segW = r.w * 0.38f;
    float pp = phase * 2.f;
    if (pp > 1.f) pp = 2.f - pp;
    const float travel = core::easeInOutCubic(pp);
    Rect seg(r.x + (r.w - segW) * travel, r.y + 1.f, segW, r.h - 2.f);
    dl().rect(seg, Col::hex(0xF2F2F3, 0.95f * alpha), radius);
}

void logoMark(gfx::Image* logo, const Rect& box, float alpha) {
    if (!logo || !logo->valid()) return;
    dl().image(logo->srv, box.offset(0.f, 2.5f), Col(0.f, 0.f, 0.f, 0.24f * alpha), 0.f, Rect(0,0,1,1), 1.f);
    dl().image(logo->srv, box, Col(1.f, 1.f, 1.f, alpha), 0.f, Rect(0,0,1,1), 1.f);
}

void spinnerRing(const Vec2& center, float radius, float thickness, const Col& from, const Col& to, float alpha) {
    const float t = g().time;
    dl().arc(center, radius, thickness, 0.f, core::kPi * 2.f, Col::hex(0xFFFFFF, 0.07f * alpha));
    const float rot = t * 0.55f;
    const float sweep = 0.95f * core::kPi;
    dl().arcGradient(center, radius, thickness, rot, rot + sweep, Col::hex(0xFFFFFF, 0.85f * alpha), Col::hex(0x9A9AA0, 0.55f * alpha));
    const Vec2 head(center.x + std::cos(rot + sweep) * radius, center.y + std::sin(rot + sweep) * radius);
    dl().circle(head, thickness * 0.52f, Col::hex(0xFFFFFF, 0.95f * alpha));
}

}
