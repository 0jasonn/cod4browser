#include <client/client.h>
#include <cgame/cg_main.h>
#include <gfx_d3d/r_dvars.h>
#include <qcommon/qcommon.h>
#include <qcommon/qcommon_math.h>
#include <ui/ui.h>
#include "web_display.h"

#include <emscripten.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cstdio>

namespace
{
const dvar_t *r_mode;
std::array<const char *, 13> modes{};

// Host layout size is platform data. The canonical r_mode chooses whether
// rendering follows it or uses a fixed resolution; JS owns no copy of that dvar.
EM_JS(void, GetBrowserSize, (std::uint32_t *width, std::uint32_t *height), {
    const size = Module['browserCanvasSize'] ??= [Module.canvas.width, Module.canvas.height];
    HEAPU32[width >> 2] = size[0];
    HEAPU32[height >> 2] = size[1];
});

EM_JS(void, SetRenderSize, (std::uint32_t width, std::uint32_t height, int fixed), {
    const canvas = Module.canvas;
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    const previous = Module['publishedDisplay'];
    if (!previous || previous[0] !== width || previous[1] !== height || previous[2] !== fixed) {
        Module['publishedDisplay'] = [width, height, fixed];
        globalThis.dispatchEvent(new CustomEvent('kisakcod:display', {
            detail: { width, height, fixed: Boolean(fixed) }
        }));
    }
});

void ResolveSize(std::uint32_t &width, std::uint32_t &height)
{
    GetBrowserSize(&width, &height);
    const bool fixed = r_mode && r_mode->current.integer != 0;
    if (fixed)
        std::sscanf(Dvar_EnumToString(r_mode), "%ux%u", &width, &height);
    SetRenderSize(width, height, fixed);
}

void StoreSize(vidConfig_t *config, std::uint32_t width, std::uint32_t height)
{
    config->sceneWidth = config->displayWidth = width;
    config->sceneHeight = config->displayHeight = height;
    // Native windowed aspect selection, including the canonical menu override.
    switch (r_aspectRatio ? r_aspectRatio->current.integer : 0)
    {
    case 1: config->aspectRatioWindow = 4.0f / 3.0f; break;
    case 2: config->aspectRatioWindow = 1.6f; break;
    case 3: config->aspectRatioWindow = 16.0f / 9.0f; break;
    default:
        const int ratio = SnapFloatToInt(height * 16.0f / width);
        config->aspectRatioWindow = ratio == 10 ? 1.6f :
            ratio >= 10 ? 4.0f / 3.0f : 16.0f / 9.0f;
        break;
    }
    config->aspectRatioScenePixel = height * config->aspectRatioWindow / width;
    config->aspectRatioDisplayPixel = 1.0f;
    config->isWideScreen = config->aspectRatioWindow != 4.0f / 3.0f;
    config->isHiDef = width >= 1280;
    if (com_wideScreen) Dvar_SetBool(com_wideScreen, config->isWideScreen != 0);
}
}

void WebDisplay_RegisterDvars()
{
    GLint textureLimit = 0, renderbufferLimit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbufferLimit);
    const unsigned limit = std::max(0, std::min(textureLimit, renderbufferLimit));
    modes.fill(nullptr);
    modes[0] = "Automatic";
    std::size_t count = 1;
    for (const char *mode : {"640x480", "800x600", "1024x768", "1280x720",
         "1280x800", "1440x900", "1600x900", "1920x1080", "1920x1200",
         "2560x1440", "3840x2160"})
    {
        unsigned width = 0, height = 0;
        std::sscanf(mode, "%ux%u", &width, &height);
        if (width <= limit && height <= limit) modes[count++] = mode;
    }
    r_mode = Dvar_RegisterEnum("r_mode", modes.data(), 0, DVAR_ARCHIVE | DVAR_LATCH,
        "Canvas render resolution; Automatic follows browser layout and pixel density");
    static const char *refresh[] = {"Browser controlled", nullptr};
    Dvar_RegisterEnum("r_displayRefresh", refresh, 0, DVAR_ROM,
        "The browser and operating system control monitor refresh rate");
}

void WebDisplay_Configure(vidConfig_t *configuration)
{
    WebDisplay_RegisterDvars();
    std::uint32_t width, height;
    ResolveSize(width, height);
    StoreSize(configuration, width, height);
    configuration->displayFrequency = 0.0f; // No monitor refresh-rate API.
    configuration->isFullscreen = 0;
}

void WebDisplay_Update()
{
    std::uint32_t width, height;
    ResolveSize(width, height);
    if (!cls.rendererStarted || (width == cls.vidConfig.displayWidth &&
        height == cls.vidConfig.displayHeight)) return;
    StoreSize(&cls.vidConfig, width, height);
    CL_UpdateScreenPlacement();
    if (cls.uiStarted) UI_UpdateScreenDimensions();
    if (CL_IsCgameInitialized(0)) CG_InitViewDimensions(0);
}
