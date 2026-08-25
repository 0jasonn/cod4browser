#include <gfx_d3d/r_cinematic_api.h>
#include <universal/q_shared.h>

#include <emscripten.h>

#include <cstring>

namespace
{
EM_JS(void, PublishCinematicOmission, (const char *name), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:cinematic", {
        detail: {
            state: "skipped",
            name: name ? UTF8ToString(name) : "",
            reason: "native-bink-unavailable",
            message: "Cinematic playback is unavailable in the browser build; continuing gameplay"
        }
    }));
});

char g_currentName[256]{};
char g_nextName[256]{};
unsigned int g_nextFlags = 0;
bool g_started = false;
// Match the canonical unavailable-cinematic backend: with no native Bink item
// active, presentation is already complete rather than pending forever.
bool g_finished = true;
}

void __cdecl R_Cinematic_SyncNow()
{
}

bool R_Cinematic_GetPlaybackInfo(char *name, std::size_t nameSize,
    std::uint32_t *timeInMsec)
{
    if (name && nameSize)
    {
        name[0] = '\0';
    }
    if (timeInMsec)
    {
        *timeInMsec = 0;
    }
    return g_started;
}

bool R_Cinematic_IsNextReady() { return g_nextName[0] != '\0'; }
bool R_Cinematic_IsPending() { return g_nextName[0] != '\0'; }
bool R_Cinematic_IsStarted() { return g_started; }
bool R_Cinematic_IsFinished() { return g_finished; }

void __cdecl R_Cinematic_StartPlayback(
    char *name, unsigned int, float)
{
    I_strncpyz(g_currentName, name ? name : "", sizeof(g_currentName));
    // Native Bink is intentionally unavailable in Wasm. Treat the selected
    // cinematic as a graceful, immediately completed presentation item.
    g_started = true;
    g_finished = true;
    PublishCinematicOmission(g_currentName);
}

void R_Cinematic_SetNextPlayback(const char *name, unsigned int flags)
{
    I_strncpyz(g_nextName, name ? name : "", sizeof(g_nextName));
    g_nextFlags = flags;
}

void __cdecl R_Cinematic_StartNextPlayback()
{
    char name[sizeof(g_nextName)]{};
    I_strncpyz(name, g_nextName, sizeof(name));
    const unsigned int flags = g_nextFlags;
    g_nextName[0] = '\0';
    R_Cinematic_StartPlayback(name, flags, 1.0f);
}

void R_Cinematic_StopPlayback()
{
    g_currentName[0] = '\0';
    g_started = false;
    g_finished = true;
}

void R_Cinematic_UnsetNextPlayback() { g_nextName[0] = '\0'; }
void __cdecl R_Cinematic_DrawStretchPic_Letterboxed() {}
void __cdecl R_Cinematic_SetPaused(CinematicEnum) {}

#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_DiagnosticCinematicOmission()
{
    char name[] = "diagnostic_intro";
    R_Cinematic_StartPlayback(name, 0u, 1.0f);
    return R_Cinematic_IsFinished() ? 1 : 0;
}
#endif
