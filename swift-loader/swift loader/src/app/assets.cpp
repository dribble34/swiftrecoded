#include "app/assets.h"
#include "app/embedded_assets.h"
#include "app/embedded_icons.h"
#include <windows.h>
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")

namespace app {

static bool tryLoad(ID3D11Device* dev, const std::wstring& path, gfx::Image& out) {
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    return gfx::loadImage(dev, path, out);
}

static std::wstring exeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

void Assets::load(ID3D11Device* dev) {
    const std::wstring dir = exeDir();

    if (!tryLoad(dev, dir + L"\\assets\\cs2_icon.png", cs2Icon))
        gfx::loadImageMemory(dev, kCs2IconPng, kCs2IconPng_size, cs2Icon);

    if (!tryLoad(dev, dir + L"\\assets\\cs2_hero.jpg", cs2Hero))
        gfx::loadImageMemory(dev, kCs2HeroJpg, kCs2HeroJpg_size, cs2Hero);

    if (!tryLoad(dev, dir + L"\\assets\\logo.png", logo))
        gfx::loadImageMemory(dev, kCsLogoPng, kCsLogoPng_size, logo);

    // Verification icons (always from embedded data)
    gfx::loadImageMemory(dev, kIconHwidCheck,  kIconHwidCheck_size,  iconHwid);
    gfx::loadImageMemory(dev, kIconSubCheck,   kIconSubCheck_size,   iconSub);
    gfx::loadImageMemory(dev, kIconServerCheck,kIconServerCheck_size,iconServer);
    gfx::loadImageMemory(dev, kIconFailed,     kIconFailed_size,     iconFailed);
}

void Assets::unload() {
    gfx::releaseImage(cs2Icon);
    gfx::releaseImage(cs2Hero);
    gfx::releaseImage(logo);
    gfx::releaseImage(iconHwid);
    gfx::releaseImage(iconSub);
    gfx::releaseImage(iconServer);
    gfx::releaseImage(iconFailed);
}

}
