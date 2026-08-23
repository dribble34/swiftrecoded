#pragma once
#include <d3d11.h>
#include "gfx/image.h"

namespace app {

struct Assets {
    // Existing assets
    gfx::Image cs2Icon;
    gfx::Image cs2Hero;
    gfx::Image logo;

    // Verification icons
    gfx::Image iconHwid;
    gfx::Image iconSub;
    gfx::Image iconServer;
    gfx::Image iconFailed;

    void load(ID3D11Device* dev);
    void unload();
};

}
