// Browser renderer frontend closure for the canonical SP runtime. This owns
// presentation and scene APIs at the platform boundary; the DB-owned world
// remains s_world and the existing bounded WebGL2 adapter remains the only
// world conversion/submission path.

#include <client/client.h>
#include <database/database.h>
#include <EffectsCore/fx_system.h>
#include <gfx_d3d/gfx_light_types.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_cgame_api.h>
#include <gfx_d3d/r_bsp.h>
#include <gfx_d3d/r_client_api.h>
#include <gfx_d3d/r_dynentity_api.h>
#include <gfx_d3d/r_drawsurf.h>
#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_effects_api.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_runtime_api.h>
#include <gfx_d3d/r_scene_api.h>
#include <gfx_d3d/r_statistics.h>
#include <gfx_d3d/r_warning_types.h>
#include <qcommon/qcommon.h>
#include <qcommon/com_world_types.h>
#include <qcommon/com_world_runtime.h>
#include <qcommon/cmd.h>
#include <stringed/stringed_hooks.h>
#include <universal/com_math.h>
#include <universal/com_memory.h>
#include <universal/memfile.h>
#include <web/web_renderer.h>
#include <web/web_system.h>
#include <xanim/dobj.h>
#include <xanim/dobj_utils.h>
#include <xanim/xmodel.h>

#include <emscripten.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstring>

enum CubemapShot : int;

extern GfxWorld s_world;

// These owners live in the native window, warning, and material frontends.
// The browser replaces those frontends but consumes the canonical renderer
// dvar registry, so retain the same shared state at this platform boundary.
const dvar_t *vid_xpos = nullptr;
const dvar_t *vid_ypos = nullptr;
const dvar_t *r_fullscreen = nullptr;
const dvar_t *r_warningRepeatDelay = nullptr;
bool g_generateOverrideTechniques = true;

void __cdecl Material_PreventOverrideTechniqueGeneration()
{
    g_generateOverrideTechniques = false;
}

namespace
{
struct WebFog
{
    int startTime;
    int finishTime;
    std::uint32_t color;
    float fogStart;
    float density;
};
static_assert(sizeof(WebFog) == 20);

std::array<WebFog, 5> g_fogs{};
std::uint32_t g_fogIndex = 0;
float g_cullDistance = 0.0f;
float g_sunLightOverride[3]{};
float g_sunDirectionOverride[3]{};
float g_sunDirectionTarget[3]{};
bool g_hasSunLightOverride = false;
bool g_hasSunDirectionOverride = false;
bool g_rendererWorldReady = false;
int g_sunLerpBeginTime = 0;
int g_sunLerpEndTime = 0;
trStatistics_t *g_statistics = nullptr;
std::array<bool, R_WARN_COUNT> g_warned{};
std::array<GfxPackedVertex, 65536> g_codeMeshVerts{};
std::array<r_double_index_t, 131072> g_codeMeshIndices{};
std::uint32_t g_codeMeshVertCount = 0;
std::uint32_t g_codeMeshIndexCount = 0;
GfxParticleCloud g_particleCloud{};

EM_JS(void, Web_GetCanvasSize, (std::uint32_t *width, std::uint32_t *height), {
    const canvas = Module.canvas;
    HEAPU32[width >> 2] = canvas ? (canvas.width >>> 0) : 1280;
    HEAPU32[height >> 2] = canvas ? (canvas.height >>> 0) : 720;
});

const Glyph *GetGlyph(Font_s *font, std::uint32_t letter)
{
    if (letter >= 0x20 && letter <= 0x7f)
        return &font->glyphs[letter - 0x20];
    int bottom = 96;
    int top = font->glyphCount - 1;
    while (bottom <= top)
    {
        const int middle = (bottom + top) / 2;
        if (font->glyphs[middle].letter == letter)
            return &font->glyphs[middle];
        if (font->glyphs[middle].letter < letter)
            bottom = middle + 1;
        else
            top = middle - 1;
    }
    return &font->glyphs[14];
}
}

void __cdecl R_BeginRegistration(vidConfig_t *configuration)
{
    iassert(configuration);
    R_RegisterDvars();
    std::memset(configuration, 0, sizeof(*configuration));
    Web_GetCanvasSize(&configuration->displayWidth,
        &configuration->displayHeight);
    configuration->sceneWidth = configuration->displayWidth;
    configuration->sceneHeight = configuration->displayHeight;
    configuration->displayFrequency = 60.0f;
    configuration->isWideScreen =
        configuration->displayWidth * 3 >= configuration->displayHeight * 4;
    configuration->isHiDef = configuration->displayWidth >= 1280;
    configuration->isFullscreen = 0;
    configuration->aspectRatioWindow = configuration->displayHeight
        ? static_cast<float>(configuration->displayWidth) /
            static_cast<float>(configuration->displayHeight)
        : 1.0f;
    configuration->aspectRatioScenePixel = 1.0f;
    configuration->aspectRatioDisplayPixel = 1.0f;
    configuration->maxTextureSize = WEB_RENDERER_MAX_RGBA8_DIMENSION;
    configuration->maxTextureMaps = 16;
    configuration->deviceSupportsGamma = false;
}

void __cdecl R_Shutdown(int) {}
void R_ShutdownDirect3D() {}
void __cdecl R_SyncRenderThread() {}
void __cdecl R_BeginFrame()
{
    g_warned.fill(false);
    g_codeMeshVertCount = 0;
    g_codeMeshIndexCount = 0;
}
void __cdecl R_EndFrame() {}
void __cdecl R_BeginClientCmdList2D() {}
void __cdecl R_BeginSharedCmdList() {}
void __cdecl R_AddCmdEndOfList() {}
void __cdecl R_IssueRenderCommands(std::uint32_t) {}
void __cdecl R_AddCmdProjectionSet2D() {}

void __cdecl R_ClearFogs()
{
    g_fogs.fill({});
    g_fogIndex = 0;
}

void __cdecl R_LoadWorld(char *name, int *checksum, int)
{
    iassert(name && *name);
    GfxWorld *world = DB_FindXAssetHeader(
        ASSET_TYPE_GFXWORLD, name).gfxWorld;
    if (world != &s_world || !s_world.name)
        Com_Error(ERR_DROP,
            "R_LoadWorld: canonical GfxWorld '%s' is not published", name);
    if (checksum) *checksum = static_cast<int>(s_world.checksum);
    g_rendererWorldReady = true;
}

void __cdecl R_EndRegistration()
{
    iassert(g_rendererWorldReady);
}

Font_s *__cdecl R_RegisterFont(const char *name, int)
{
    return DB_FindXAssetHeader(ASSET_TYPE_FONT, name).font;
}

const Glyph *__cdecl R_GetCharacterGlyph(Font_s *font, std::uint32_t letter)
{
    iassert(font && font->glyphs);
    return GetGlyph(font, letter);
}

int __cdecl R_TextWidth(const char *text, int maxChars, Font_s *font)
{
    iassert(text && font);
    if (maxChars <= 0) maxChars = 0x7fffffff;
    int lineWidth = 0;
    int maxWidth = 0;
    int count = 0;
    while (*text && count < maxChars)
    {
        const std::uint32_t letter = SEH_ReadCharFromString(&text, 0);
        if (letter == '\r' || letter == '\n')
            lineWidth = 0;
        else if (letter == '^' && *text != '^' && *text >= '0' && *text <= '9')
            ++text;
        else
        {
            lineWidth += GetGlyph(font, letter)->dx;
            maxWidth = std::max(maxWidth, lineWidth);
            ++count;
        }
    }
    return maxWidth;
}

int __cdecl R_TextHeight(Font_s *font)
{
    iassert(font);
    return font->pixelHeight;
}

double __cdecl R_NormalizedTextScale(Font_s *font, float scale)
{
    iassert(font && font->pixelHeight > 0);
    return scale * 48.0 / static_cast<double>(font->pixelHeight);
}

const char *__cdecl R_TextLineWrapPosition(
    const char *text, int bufferSize, int pixelsAvailable,
    Font_s *font, float scale)
{
    iassert(text);
    if (bufferSize <= 0) bufferSize = 0x7fffffff;
    int pixelsUsed = 0;
    const char *wrap = nullptr;
    const char *parse = text;
    const char *preLetter = text;
    while (*parse)
    {
        preLetter = parse;
        const std::uint32_t letter = SEH_ReadCharFromString(&parse, 0);
        if (letter == '\r') pixelsUsed = 0;
        else if (letter == '\n') return parse;
        else if (letter == '^' && *parse != '^' && *parse >= '0' && *parse <= '9')
            ++parse;
        else
        {
            if (font) pixelsUsed += GetGlyph(font, letter)->dx;
            if (preLetter != text && letter < 0x100 && std::isspace(letter))
                wrap = preLetter;
        }
        if (wrap && pixelsUsed * scale > pixelsAvailable) return wrap;
        if (parse - text > bufferSize) return wrap ? wrap : preLetter;
    }
    return parse - text != bufferSize ? parse : (wrap ? wrap : preLetter);
}

int __cdecl R_ConsoleTextWidth(
    const char *textPool, int poolSize, int firstChar, int charCount,
    Font_s *font)
{
    iassert(IsPowerOf2(poolSize));
    const int mask = poolSize - 1;
    const int stop = mask & (charCount + firstChar);
    int position = firstChar;
    int width = 0;
    while (position != stop)
    {
        int used = 0;
        const std::uint32_t letter = SEH_DecodeLetter(
            textPool[position], textPool[mask & (position + 1)], &used, 0);
        position = mask & (position + used);
        if (letter == '^' && textPool[position] != '^' &&
            textPool[position] >= '0' && textPool[position] <= '9')
            position = mask & (position + 1);
        else
            width += GetGlyph(font, letter)->dx;
    }
    return width;
}

void __cdecl R_AddCmdDrawStretchPic(float, float, float, float,
    float, float, float, float, const float *, Material *) {}
void __cdecl R_AddCmdDrawStretchPicFlipST(float, float, float, float,
    float, float, float, float, const float *, Material *) {}
void __cdecl R_AddCmdDrawStretchPicRotateXY(float, float, float, float,
    float, float, float, float, float, const float *, Material *) {}
void __cdecl R_AddCmdDrawStretchPicRotateST(float, float, float, float,
    float, float, float, float, float, float, const float *, Material *) {}
void __cdecl R_AddCmdDrawQuadPic(const float (*)[2], const float *, Material *) {}
void __cdecl R_AddCmdDrawText(const char *, int, Font_s *, float, float,
    float, float, float, const float *, int) {}
void __cdecl R_AddCmdDrawTextSubtitle(const char *, int, Font_s *, float,
    float, float, float, float, const float *, int, const float *, bool) {}
void __cdecl R_AddCmdDrawTextWithCursor(const char *, int, Font_s *, float,
    float, float, float, float, const float *, int, int, char) {}
void __cdecl R_AddCmdDrawTextWithEffects(const char *, int, Font_s *, float,
    float, float, float, float, const float *, int, const float *, Material *,
    Material *, int, int, int, int) {}
void __cdecl R_AddCmdDrawConsoleText(char *, int, int, int, Font_s *, float,
    float, float, float, const float *, int) {}
void __cdecl R_AddCmdDrawConsoleTextSubtitle(char *, int, int, int, Font_s *,
    float, float, float, float, const float *, int, const float *) {}
void __cdecl R_AddCmdDrawConsoleTextPulseFX(char *, int, int, int, Font_s *,
    float, float, float, float, const float *, int, const float *, int, int,
    int, int, Material *, Material *) {}
void __cdecl R_AddCmdDrawProfile() {}
void __cdecl R_AddCmdClearScreen(int, const float *, float, std::uint8_t) {}
void __cdecl R_AddCmdSaveScreen(std::uint32_t) {}
void __cdecl R_AddCmdSaveScreenSection(float, float, float, float,
    std::uint32_t) {}
void __cdecl R_AddCmdBlendSavedScreenShockBlurred(int, float, float, float,
    float, std::uint32_t) {}
void __cdecl R_AddCmdBlendSavedScreenShockFlashed(float, float, float, float,
    float, float) {}

std::uint32_t __cdecl R_GetLocalClientNum() { return 0; }
void __cdecl R_ClearScene(std::uint32_t) {}

void __cdecl R_InitSceneData(int localClientNum)
{
    iassert(localClientNum == 0 && g_rendererWorldReady);
    // Native DPVS scene buffers are renderer-backend storage. The WebGL2
    // backend owns visibility submission and does not expose D3D scene bits.
}

void __cdecl R_InitPrimaryLights(GfxLight *primaryLights)
{
    iassert(primaryLights && comWorld.isInUse);
    for (std::uint32_t index = 0; index < comWorld.primaryLightCount; ++index)
    {
        const ComPrimaryLight &input = comWorld.primaryLights[index];
        GfxLight &output = primaryLights[index];
        std::memset(&output, 0, sizeof(output));
        output.type = input.type;
        output.canUseShadowMap = input.canUseShadowMap;
        std::memcpy(output.color, input.color, sizeof(output.color));
        std::memcpy(output.dir, input.dir, sizeof(output.dir));
        std::memcpy(output.origin, input.origin, sizeof(output.origin));
        output.radius = input.radius;
        output.cosHalfFovOuter = input.cosHalfFovOuter;
        output.cosHalfFovInner = input.cosHalfFovInner;
        output.exponent = input.exponent;
        output.def = input.defName
            ? DB_FindXAssetHeader(ASSET_TYPE_LIGHT_DEF, input.defName).lightDef
            : nullptr;
    }
    if (s_world.sunPrimaryLightIndex && s_world.sunLight)
        primaryLights[s_world.sunPrimaryLightIndex] = *s_world.sunLight;
}

void __cdecl R_ClearShadowedPrimaryLightHistory(int) {}

char __cdecl R_ReserveCodeMeshVerts(int count, std::uint16_t *baseVertex)
{
    if (count < 0 || !baseVertex ||
        g_codeMeshVertCount + static_cast<std::uint32_t>(count) >
            g_codeMeshVerts.size())
        return 0;
    *baseVertex = static_cast<std::uint16_t>(g_codeMeshVertCount);
    g_codeMeshVertCount += count;
    return 1;
}

char __cdecl R_ReserveCodeMeshIndices(int count,
    r_double_index_t **indicesOut)
{
    if (count < 0 || !indicesOut ||
        g_codeMeshIndexCount + static_cast<std::uint32_t>(count) >
            g_codeMeshIndices.size())
        return 0;
    *indicesOut = &g_codeMeshIndices[g_codeMeshIndexCount];
    g_codeMeshIndexCount += count;
    return 1;
}

GfxPackedVertex *__cdecl R_GetCodeMeshVerts(std::uint16_t baseVertex)
{
    return &g_codeMeshVerts[baseVertex];
}

void __cdecl R_AddCodeMeshDrawSurf(Material *, r_double_index_t *,
    std::uint32_t, std::uint32_t, std::uint32_t, const char *) {}

GfxParticleCloud *__cdecl R_AddParticleCloudToScene(Material *)
{
    return &g_particleCloud;
}

void __cdecl R_FilterXModelIntoScene(const XModel *,
    const GfxScaledPlacement *, std::uint16_t, std::uint16_t *) {}

void __cdecl R_AddOmniLightToScene(const float *, float, float, float, float) {}
void __cdecl R_AddSpotLightToScene(const float *, const float *, float,
    float, float, float) {}
void __cdecl R_SetLodOrigin(const refdef_s *) {}
void __cdecl R_RenderScene(const refdef_s *) {}

GfxBrushModel *__cdecl R_GetBrushModel(std::uint32_t index)
{
    return index < s_world.modelCount ? &s_world.models[index] : nullptr;
}

void __cdecl R_AddBrushModelToSceneFromAngles(
    const GfxBrushModel *, const float *, const float *, std::uint16_t) {}
void __cdecl R_AddDObjToScene(const DObj_s *, const cpose_t *,
    std::uint32_t, std::uint32_t, float *, float) {}
void __cdecl R_LinkDObjEntity(std::uint32_t, std::uint32_t, float *, float) {}
void __cdecl R_LinkBModelEntity(std::uint32_t, std::uint32_t,
    GfxBrushModel *) {}
void __cdecl R_UnlinkEntity(std::uint32_t, std::uint32_t) {}
void __cdecl R_UnlinkDynEnt(std::uint32_t, DynEntityDrawType) {}
void __cdecl R_LinkDynEnt(std::uint32_t, DynEntityDrawType, float *, float *) {}

void R_DObjReplaceMaterial(
    DObj_s *obj, int lod, int surfaceIndex, Material *material)
{
    iassert(obj && lod >= 0);
    int currentSurface = 0;
    for (std::uint32_t modelIndex = 0;
         modelIndex < DObjGetNumModels(obj); ++modelIndex)
    {
        const XModel *model = DObjGetModel(obj, modelIndex);
        const std::uint32_t surfaceCount = XModelGetSurfCount(model, lod);
        Material **skins = XModelGetSkins(model, lod);
        for (std::uint32_t local = 0; local < surfaceCount;
             ++local, ++currentSurface)
        {
            if (currentSurface == surfaceIndex)
            {
                skins[local] = material;
                return;
            }
        }
    }
}

double __cdecl R_GetFarPlaneDist() { return g_cullDistance; }
void R_SetCullDist(float distance) { g_cullDistance = distance; }

void R_SetSunLightOverride(float *color)
{
    iassert(color);
    std::memcpy(g_sunLightOverride, color, sizeof(g_sunLightOverride));
    g_hasSunLightOverride = true;
}
void R_ResetSunLightOverride() { g_hasSunLightOverride = false; }
void R_SetSunDirectionOverride(float *direction)
{
    iassert(direction);
    std::memcpy(g_sunDirectionOverride, direction,
        sizeof(g_sunDirectionOverride));
    g_hasSunDirectionOverride = true;
    g_sunLerpBeginTime = g_sunLerpEndTime = 0;
}
void R_LerpSunDirectionOverride(float *begin, float *end,
    int beginTime, int endTime)
{
    R_SetSunDirectionOverride(begin);
    std::memcpy(g_sunDirectionTarget, end, sizeof(g_sunDirectionTarget));
    g_sunLerpBeginTime = beginTime;
    g_sunLerpEndTime = endTime;
}
void R_ResetSunDirectionOverride() { g_hasSunDirectionOverride = false; }
void R_ResetSunLightParseParams()
{
    R_ResetSunLightOverride();
    R_ResetSunDirectionOverride();
}

void __cdecl R_InterpretSunLightParseParams(SunLightParseParams *sunParse)
{
    if (!sunParse || !s_world.sunLight) return;
    float direction[3]{};
    AngleVectors(sunParse->angles, direction, nullptr, nullptr);
    float color[3]{};
    Vec3Scale(sunParse->sunColor,
        (1.0f - sunParse->diffuseFraction) *
            (sunParse->sunLight - sunParse->ambientScale), color);
    std::memset(s_world.sunLight, 0, sizeof(*s_world.sunLight));
    s_world.sunLight->type = 1;
    Vec3Copy(direction, s_world.sunLight->dir);
    Vec3Copy(color, s_world.sunLight->color);
    Vec3Copy(color, s_world.sunColorFromBsp);
}

void __cdecl R_SetFogFromServer(float start, std::uint8_t r,
    std::uint8_t g, std::uint8_t b, float density)
{
    g_fogs[1].color = (static_cast<std::uint32_t>(r) << 16) |
        (static_cast<std::uint32_t>(g) << 8) | b | 0xff000000u;
    g_fogs[1].fogStart = start;
    g_fogs[1].density = density;
}

void __cdecl R_SwitchFog(
    std::uint32_t fog, int startTime, int transitionTime)
{
    iassert(fog < g_fogs.size());
    g_fogIndex = fog;
    g_fogs[3] = g_fogs[2].density == 0.0f ? g_fogs[fog] : g_fogs[2];
    g_fogs[4] = g_fogs[fog];
    g_fogs[4].startTime = transitionTime ? startTime : 0;
    g_fogs[4].finishTime = transitionTime ? startTime + transitionTime : 0;
}

void __cdecl R_ArchiveFogState(MemoryFile *memory)
{
    MemFile_ArchiveData(memory, sizeof(g_fogs), g_fogs.data());
    MemFile_ArchiveData(memory, sizeof(g_fogIndex), &g_fogIndex);
}

void __cdecl R_GetAverageLightingAtPoint(const float *, std::uint8_t *color)
{
    color[0] = color[1] = color[2] = color[3] = 255;
}

void R_WarnOncePerFrame(GfxWarningType type, ...)
{
    if (type < 0 || type >= R_WARN_COUNT || g_warned[type]) return;
    g_warned[type] = true;
    Com_DPrintf(8, "renderer warning %d\n", static_cast<int>(type));
}

void __cdecl R_UpdateSpotLightEffect(FxCmd *cmd) { FX_UpdateSpotLight(cmd); }
void __cdecl R_UpdateNonDependentEffects(FxCmd *cmd)
{
    FX_UpdateNonDependent(cmd);
}
void __cdecl R_UpdateRemainingEffects(FxCmd *cmd)
{
    FX_UpdateRemaining(cmd);
    FX_EndUpdate(cmd->localClientNum);
}

void __cdecl R_TrackStatistics(trStatistics_t *statistics)
{
    g_statistics = statistics;
    if (statistics) std::memset(statistics, 0, sizeof(*statistics));
}

void __cdecl R_DebugAlloc(void **memory, int size, const char *name)
{
    iassert(memory && !*memory && size > 0 && !(size & 3));
    *memory = Z_TryVirtualAlloc(size, name, 0);
}

void __cdecl R_DebugFree(void **memory)
{
    if (memory && *memory)
    {
        Z_VirtualFree(*memory);
        *memory = nullptr;
    }
}

void __cdecl R_ShutdownDebug() {}

void __cdecl R_CreateDevGui()
{
    Cbuf_InsertText(0, "exec devgui_renderer");
    Cbuf_InsertText(0, "exec devgui_visibility");
}

std::uint32_t R_GetDebugReflectionProbeLocs(
    float (*locations)[3], std::uint32_t maximum)
{
    const std::uint32_t available = s_world.reflectionProbeCount > 0
        ? s_world.reflectionProbeCount - 1 : 0;
    const std::uint32_t count = std::min(maximum, available);
    for (std::uint32_t index = 0; index < count; ++index)
        Vec3Copy(s_world.reflectionProbes[index + 1].origin,
            locations[index]);
    return count;
}

void __cdecl R_CalcCubeMapViewValues(refdef_s *, CubemapShot, int) {}
int __cdecl R_PickMaterial(int, const float *, const float *, char *, char *,
    char *, std::uint32_t) { return 0; }

void __cdecl R_MarkFragments_Begin(MarkInfo *info,
    MarkFragmentsAgainstEnum, const float *, const float (*)[3], float,
    const float *, Material *)
{
    if (info) std::memset(info, 0, sizeof(*info));
}
char __cdecl R_MarkFragments_AddDObj(
    MarkInfo *, DObj_s *, cpose_t *, std::uint16_t) { return 0; }
char __cdecl R_MarkFragments_AddBModel(
    MarkInfo *, GfxBrushModel *, cpose_t *, std::uint16_t) { return 0; }
void __cdecl R_MarkFragments_Go(MarkInfo *,
    void(__cdecl *)(void *, int, FxMarkTri *, int, FxMarkPoint *,
        const float *, const float *),
    void *, int, FxMarkTri *, int, FxMarkPoint *) {}

void __cdecl Material_DirtyTechniqueSetOverrides() {}

Material *Material_RegisterRawImage(const char *, int)
{
    return DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, "$default").material;
}

Material *__cdecl Material_Duplicate(Material *source, char *name)
{
    iassert(source && name);
    if (Material *existing =
        DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, name).material)
        return existing;
    // Dynamic UI aliases are renderer-owned in native Kisak. Until the
    // browser frontend admits its canonical alias hash, retaining the source
    // handle preserves rendering without publishing browser-owned DB state.
    return source;
}
