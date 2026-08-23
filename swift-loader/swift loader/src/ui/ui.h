#pragma once
#include <string>
#include <unordered_map>
#include "core/types.h"
#include "gfx/drawlist.h"
#include "gfx/font.h"
#include "gfx/image.h"

namespace ui {

using core::Col;
using core::Rect;
using core::Vec2;

enum class AlignH { Left, Center, Right };
enum class AlignV { Top, Middle, Bottom };

struct Input {
    Vec2 mouse;
    bool down      = false;
    bool pressed   = false;
    bool released  = false;
    char lastChar  = 0;     // printable ASCII typed this frame
    bool backspace = false; // VK_BACK pressed
    bool enterKey  = false; // VK_RETURN pressed
    std::string pasteText;  // Ctrl+V clipboard content (empty if none)
};

struct Theme {
    Col bg = Col::hex(0x0E0E10, 1.f);
    Col body = Col::hex(0x1E1E21, 1.f);
    Col bodyHi = Col::hex(0x252529, 1.f);
    Col row = Col::hex(0x27272B, 1.f);
    Col rowHover = Col::hex(0x2F2F33, 1.f);
    Col bar = Col::hex(0x1A1A1E, 1.f);
    Col bar2 = Col::hex(0x232326, 1.f);
    Col border = Col::hex(0xFFFFFF, 0.06f);
    Col borderHi = Col::hex(0xFFFFFF, 0.10f);
    Col text = Col::hex(0xF2F2F3, 1.f);
    Col sub = Col::hex(0xA1A1A6, 1.f);
    Col accent = Col::hex(0xEDEDEF, 1.f);
    Col accent2 = Col::hex(0x9A9AA0, 1.f);
    float radius = 16.f;
    float barRadius = 16.f;
};

struct Fonts {
    gfx::Font title;
    gfx::Font body;
};

struct AnimState {
    float value = 0.f;
    float velocity = 0.f;
    bool initialized = false;
};

struct Context {
    gfx::DrawList draw;
    Input input;
    Theme theme;
    Fonts fonts;
    float dt = 0.f;
    float time = 0.f;
    float width = 0.f;
    float height = 0.f;
    uint32_t hot = 0;
    uint32_t active = 0;
    uint32_t hotNext = 0;
    std::unordered_map<uint64_t, AnimState> anims;
};

struct FontData {
    const void* semiBold = nullptr;
    size_t semiBoldSize = 0;
    const void* bold = nullptr;
    size_t boldSize = 0;
};

Context& g();
bool init(ID3D11Device* dev, const FontData& fonts);
void shutdown();
void newFrame(const Input& in, float dt, float width, float height);
void endFrame();

inline gfx::DrawList& dl() { return g().draw; }
inline Theme& theme() { return g().theme; }
inline Fonts& fonts() { return g().fonts; }

uint32_t id(const char* str);

float anim(uint32_t widget, int slot, float target, float speed);
bool hovered(uint32_t widget, const Rect& r);
bool clicked(uint32_t widget, const Rect& r);

void text(gfx::Font& font, const Rect& box, const char* str, const Col& col, AlignH h = AlignH::Left,
          AlignV v = AlignV::Middle, float tracking = 0.f);
void textShadow(gfx::Font& font, const Rect& box, const char* str, const Col& col, AlignH h = AlignH::Left,
                AlignV v = AlignV::Middle, float tracking = 0.f, float shadowAlpha = 0.55f,
                float offsetY = 1.f);

}
