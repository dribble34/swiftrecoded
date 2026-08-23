#pragma once
#include "ui/ui.h"

namespace ui {

void titleBar(const Rect& panel, float barHeight, const char* title, float time);
void skyBackground(const Rect& area, const Rect& shape, float radius, float time, float alpha,
                   float starScale = 1.f);
bool closeButton(const char* name, const Rect& r);
bool gameRow(const char* name, const Rect& r, gfx::Image* icon, const char* title);
void chevron(const Vec2& center, float height, const Col& c);
void logoMark(gfx::Image* logo, const Rect& box, float alpha);
void spinnerRing(const Vec2& center, float radius, float thickness, const Col& from, const Col& to, float alpha);
void loadingLine(const Rect& r, float phase, float time, float alpha);

}
