#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace core {

struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2() = default;
    Vec2(float a, float b) : x(a), y(b) {}
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    Rect() = default;
    Rect(float a, float b, float c, float d) : x(a), y(b), w(c), h(d) {}
    float r() const { return x + w; }
    float b() const { return y + h; }
    Vec2 center() const { return { x + w * 0.5f, y + h * 0.5f }; }
    bool contains(const Vec2& p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    Rect expand(float m) const { return { x - m, y - m, w + m * 2.f, h + m * 2.f }; }
    Rect shrink(float m) const { return expand(-m); }
    Rect offset(float dx, float dy) const { return { x + dx, y + dy, w, h }; }
    Rect clipTo(const Rect& o) const {
        float nx = (std::max)(x, o.x), ny = (std::max)(y, o.y);
        float nr = (std::min)(r(), o.r()), nb = (std::min)(b(), o.b());
        return { nx, ny, (std::max)(0.f, nr - nx), (std::max)(0.f, nb - ny) };
    }
};

struct Col {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
    Col() = default;
    Col(float rr, float gg, float bb, float aa = 1.f) : r(rr), g(gg), b(bb), a(aa) {}
    static Col hex(uint32_t rgb, float alpha = 1.f) {
        return Col(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha);
    }
    Col alpha(float m) const { return Col(r, g, b, a * m); }
    uint32_t packed() const {
        auto q = [](float v) { return (uint32_t)((std::max)(0.f, (std::min)(1.f, v)) * 255.f + 0.5f); };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }
};

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float easeOutCubic(float t) { t = clamp01(t); float u = 1.f - t; return 1.f - u * u * u; }
inline float easeInOutCubic(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
}

inline float approach(float cur, float target, float speed, float dt) {
    return cur + (target - cur) * (1.f - std::exp(-speed * dt));
}

inline void spring(float& value, float& velocity, float target, float stiffness, float damping, float dt) {
    dt = (std::min)(dt, 0.032f);
    velocity += ((target - value) * stiffness - velocity * damping) * dt;
    value += velocity * dt;
}

constexpr float kPi = 3.14159265358979323846f;

}
