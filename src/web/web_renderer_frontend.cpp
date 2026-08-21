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
#include <qcommon/engine_lifecycle_trace.h>
#include <stringed/stringed_hooks.h>
#include <universal/com_math.h>
#include <universal/com_memory.h>
#include <universal/memfile.h>
#include <web/web_renderer.h>
#include <web/web_renderer_code_mesh.h>
#include <web/web_renderer_dobj_scene.h>
#include <web/web_renderer_fx_model_scene.h>
#include <web/web_renderer_particle_cloud_scene.h>
#include <web/web_renderer_static_model_scene.h>
#include <web/web_renderer_world_scene.h>
#include <web/web_system.h>
#include <xanim/dobj.h>
#include <xanim/dobj_utils.h>
#include <xanim/xmodel.h>
#include <xanim/xsurface_types.h>

#include <emscripten.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

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
bool g_gameDrivenFrameReported = false;
bool g_worldSceneSubmitted = false;
bool g_staticModelSceneSubmitted = false;
bool g_dynamicModelSceneReported = false;
bool g_uiSceneReported = false;
std::array<WebRendererDObjSubmission, 32> g_dobjSubmissions{};
std::uint32_t g_dobjSubmissionCount = 0u;
std::array<WebRendererFxModelSubmission,
    WEB_RENDERER_MAX_FX_MODEL_SUBMISSIONS> g_fxModelSubmissions{};
std::uint32_t g_fxModelSubmissionCount = 0u;
std::array<WebRendererParticleCloudSubmission,
    WEB_RENDERER_MAX_PARTICLE_CLOUD_SUBMISSIONS> g_particleCloudSubmissions{};
std::uint32_t g_particleCloudSubmissionCount = 0u;
std::uint32_t g_worldSceneSurfaceCount = 0u;
std::uint32_t g_worldSceneVertexCount = 0u;
std::uint32_t g_worldSceneIndexCount = 0u;
int g_sunLerpBeginTime = 0;
int g_sunLerpEndTime = 0;
trStatistics_t *g_statistics = nullptr;
std::array<bool, R_WARN_COUNT> g_warned{};
std::array<GfxPackedVertex, 65536> g_codeMeshVerts{};
std::array<r_double_index_t, 131072> g_codeMeshIndices{};
std::uint32_t g_codeMeshVertCount = 0;
std::uint32_t g_codeMeshIndexCount = 0;
bool g_processCodeMesh = false;
std::array<float, 256u * 4u> g_codeMeshArgs{};
std::uint32_t g_codeMeshArgCount = 0u;
std::vector<WebRendererSurfaceVertex> g_codeMeshRenderVertices;
std::vector<std::uint32_t> g_codeMeshRenderIndices;
std::vector<WebRendererWorldBatchDesc> g_codeMeshRenderBatches;
std::vector<WebRendererSurfaceVertex> g_uiVertices;
std::vector<std::uint32_t> g_uiIndices;
std::vector<WebRendererUiBatchDesc> g_uiBatches;

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

const GfxImage *FindUiImage(Material *material, std::uint8_t &sampler)
{
    if (!material || !material->textureTable) return nullptr;
    // Canonical HUD/font materials bind their atlas as TS_2D.  A few shared
    // materials instead expose an ordinary color map, so retain that as the
    // secondary choice without discarding the canonical material identity.
    const MaterialTextureDef *colorMap = nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 0u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
        if (!colorMap && texture.semantic == 2u && texture.u.image)
            colorMap = &texture;
    }
    if (colorMap)
    {
        sampler = colorMap->samplerState;
        return colorMap->u.image;
    }
    return nullptr;
}

void AppendUiQuadUvs(const float points[4][2], const float uvs[4][2],
    const float *color, Material *material)
{
    if (g_uiVertices.size() + 4u > WEB_RENDERER_MAX_UI_VERTICES ||
        g_uiIndices.size() + 6u > WEB_RENDERER_MAX_UI_INDICES)
        return;
    const float width = static_cast<float>(
        std::max<std::uint32_t>(1u, cls.vidConfig.displayWidth));
    const float height = static_cast<float>(
        std::max<std::uint32_t>(1u, cls.vidConfig.displayHeight));
    try
    {
        const std::uint32_t vertexBase =
            static_cast<std::uint32_t>(g_uiVertices.size());
        for (std::size_t index = 0u; index < 4u; ++index)
        {
            WebRendererSurfaceVertex vertex{};
            vertex.position[0] = points[index][0] * 2.0f / width - 1.0f;
            vertex.position[1] = 1.0f - points[index][1] * 2.0f / height;
            vertex.position[2] = 0.0f;
            vertex.color[0] = 1.0f;
            vertex.color[1] = 1.0f;
            vertex.color[2] = 1.0f;
            vertex.color[3] = 1.0f;
            vertex.textureCoordinate[0] = uvs[index][0];
            vertex.textureCoordinate[1] = uvs[index][1];
            g_uiVertices.push_back(vertex);
        }
        const std::uint32_t firstIndex =
            static_cast<std::uint32_t>(g_uiIndices.size());
        for (const std::uint32_t local : {0u, 1u, 2u, 0u, 2u, 3u})
            g_uiIndices.push_back(vertexBase + local);
        WebRendererUiBatchDesc batch{};
        batch.firstIndex = firstIndex;
        batch.indexCount = 6u;
        batch.materialIdentity = material;
        batch.materialName = material && material->info.name
            ? material->info.name : "<null-ui-material>";
        batch.image = FindUiImage(material, batch.samplerState);
        for (std::size_t component = 0u; component < 4u; ++component)
            batch.color[component] = color ? color[component] : 1.0f;
        g_uiBatches.push_back(batch);
    }
    catch (const std::bad_alloc &)
    {
        // A dropped HUD primitive is recoverable; the canonical 3D frame must
        // remain live if a transient overlay allocation cannot be satisfied.
    }
}

void AppendUiQuad(const float points[4][2], float s0, float t0,
    float s1, float t1, const float *color, Material *material)
{
    const float uvs[4][2] = {
        {s0, t0}, {s1, t0}, {s1, t1}, {s0, t1}};
    AppendUiQuadUvs(points, uvs, color, material);
}

void AppendUiRect(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1,
    const float *color, Material *material)
{
    const float points[4][2] = {
        {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    AppendUiQuad(points, s0, t0, s1, t1, color, material);
}

void AppendUiRotatedRect(float x, float y, float w, float h,
    float rotation, float s0, float t0, float s1, float t1,
    const float *color, Material *material)
{
    const float radians = rotation * (3.14159265358979323846f / 180.0f);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float centerX = x + w * 0.5f;
    const float centerY = y + h * 0.5f;
    const float local[4][2] = {
        {-w * 0.5f, -h * 0.5f}, {w * 0.5f, -h * 0.5f},
        {w * 0.5f, h * 0.5f}, {-w * 0.5f, h * 0.5f}};
    float points[4][2]{};
    for (std::size_t index = 0u; index < 4u; ++index)
    {
        points[index][0] = centerX +
            local[index][0] * cosine - local[index][1] * sine;
        points[index][1] = centerY +
            local[index][0] * sine + local[index][1] * cosine;
    }
    AppendUiQuad(points, s0, t0, s1, t1, color, material);
}

void AppendUiText(const char *text, int maxChars, Font_s *font,
    float x, float y, float xScale, float yScale, float rotation,
    const float *color)
{
    if (!text || !font || !font->glyphs || !font->material) return;
    if (maxChars <= 0) maxChars = 0x7fffffff;
    const float startX = x;
    int count = 0;
    while (*text && count < maxChars)
    {
        const std::uint32_t letter = SEH_ReadCharFromString(&text, 0);
        if (letter == '\n')
        {
            x = startX;
            y += static_cast<float>(font->pixelHeight) * yScale;
            continue;
        }
        if (letter == '\r')
        {
            x = startX;
            continue;
        }
        if (letter == '^' && *text >= '0' && *text <= '9')
        {
            ++text;
            continue;
        }
        const Glyph *glyph = GetGlyph(font, letter);
        AppendUiRotatedRect(
            x + static_cast<float>(glyph->x0) * xScale,
            y + static_cast<float>(glyph->y0) * yScale,
            static_cast<float>(glyph->pixelWidth) * xScale,
            static_cast<float>(glyph->pixelHeight) * yScale,
            rotation, glyph->s0, glyph->t0, glyph->s1, glyph->t1,
            color, font->material);
        x += static_cast<float>(glyph->dx) * xScale;
        ++count;
    }
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
    g_processCodeMesh = false;
    g_codeMeshVertCount = 0;
    g_codeMeshIndexCount = 0;
    g_codeMeshArgCount = 0u;
    g_codeMeshRenderVertices.clear();
    g_codeMeshRenderIndices.clear();
    g_codeMeshRenderBatches.clear();
    g_uiVertices.clear();
    g_uiIndices.clear();
    g_uiBatches.clear();
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
    g_gameDrivenFrameReported = false;
    g_worldSceneSubmitted = false;
    g_staticModelSceneSubmitted = false;
    g_dynamicModelSceneReported = false;
    g_uiSceneReported = false;
    g_worldSceneSurfaceCount = 0u;
    g_worldSceneVertexCount = 0u;
    g_worldSceneIndexCount = 0u;
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

void __cdecl R_AddCmdDrawStretchPic(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1, const float *color,
    Material *material)
{
    AppendUiRect(x, y, w, h, s0, t0, s1, t1, color, material);
}
void __cdecl R_AddCmdDrawStretchPicFlipST(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1, const float *color,
    Material *material)
{
    AppendUiRect(x, y, w, h, t0, s0, t1, s1, color, material);
}
void __cdecl R_AddCmdDrawStretchPicRotateXY(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1, float rotation,
    const float *color, Material *material)
{
    AppendUiRotatedRect(x, y, w, h, rotation, s0, t0, s1, t1,
        color, material);
}
void __cdecl R_AddCmdDrawStretchPicRotateST(float x, float y, float w, float h,
    float centerS, float centerT, float radiusST, float scaleFinalS,
    float scaleFinalT, float rotation,
    const float *color, Material *material)
{
    const float radians = rotation * (3.14159265358979323846f / 180.0f);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float stepS[2] = {
        radiusST * cosine * scaleFinalS,
        radiusST * sine * scaleFinalT};
    const float stepT[2] = {
        -radiusST * sine * scaleFinalS,
        radiusST * cosine * scaleFinalT};
    const float points[4][2] = {
        {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    const float uvs[4][2] = {
        {centerS - stepS[0] - stepT[0], centerT - stepS[1] - stepT[1]},
        {centerS + stepS[0] - stepT[0], centerT + stepS[1] - stepT[1]},
        {centerS + stepS[0] + stepT[0], centerT + stepS[1] + stepT[1]},
        {centerS - stepS[0] + stepT[0], centerT - stepS[1] + stepT[1]}};
    AppendUiQuadUvs(points, uvs, color, material);
}
void __cdecl R_AddCmdDrawQuadPic(const float (*verts)[2], const float *color,
    Material *material)
{
    if (verts) AppendUiQuad(verts, 0.0f, 0.0f, 1.0f, 1.0f,
        color, material);
}
void __cdecl R_AddCmdDrawText(const char *text, int maxChars, Font_s *font,
    float x, float y, float xScale, float yScale, float rotation,
    const float *color, int)
{
    AppendUiText(text, maxChars, font, x, y, xScale, yScale, rotation, color);
}
void __cdecl R_AddCmdDrawTextSubtitle(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int, const float *, bool)
{
    AppendUiText(text, maxChars, font, x, y, xScale, yScale, rotation, color);
}
void __cdecl R_AddCmdDrawTextWithCursor(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int, int cursorPos, char cursor)
{
    AppendUiText(text, maxChars, font, x, y, xScale, yScale, rotation, color);
    if (cursorPos >= 0 && text)
    {
        const int prefixCount = std::min<int>(cursorPos,
            static_cast<int>(std::strlen(text)));
        const float cursorX = x + static_cast<float>(
            R_TextWidth(text, prefixCount, font)) * xScale;
        const char cursorText[2] = {cursor, '\0'};
        AppendUiText(cursorText, 1, font, cursorX, y, xScale, yScale,
            rotation, color);
    }
}
void __cdecl R_AddCmdDrawTextWithEffects(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int, const float *, Material *,
    Material *, int, int, int, int)
{
    AppendUiText(text, maxChars, font, x, y, xScale, yScale, rotation, color);
}
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
void __cdecl R_ClearScene(std::uint32_t)
{
    g_dobjSubmissionCount = 0u;
    WebRenderer_ClearFxModelSubmissions(&g_fxModelSubmissionCount);
    WebRenderer_ClearParticleCloudSubmissions(&g_particleCloudSubmissionCount);
}

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
    if (!g_processCodeMesh || count < 0 || !baseVertex ||
        static_cast<std::uint32_t>(count) >
            g_codeMeshVerts.size() - g_codeMeshVertCount)
        return 0;
    *baseVertex = static_cast<std::uint16_t>(g_codeMeshVertCount);
    g_codeMeshVertCount += count;
    return 1;
}

char __cdecl R_ReserveCodeMeshIndices(int count,
    r_double_index_t **indicesOut)
{
    if (!g_processCodeMesh || count < 0 || !indicesOut || (count & 1) != 0 ||
        static_cast<std::uint32_t>(count) >
            WEB_RENDERER_MAX_CODE_MESH_INDICES - g_codeMeshIndexCount)
        return 0;
    *indicesOut = &g_codeMeshIndices[g_codeMeshIndexCount / 2u];
    g_codeMeshIndexCount += count;
    return 1;
}

GfxPackedVertex *__cdecl R_GetCodeMeshVerts(std::uint16_t baseVertex)
{
    return g_processCodeMesh && baseVertex < g_codeMeshVertCount
        ? &g_codeMeshVerts[baseVertex] : nullptr;
}

char __cdecl R_ReserveCodeMeshArgs(int count, std::uint32_t *argOffsetOut)
{
    if (!g_processCodeMesh || count < 0 || !argOffsetOut ||
        static_cast<std::uint32_t>(count) > 256u - g_codeMeshArgCount)
        return 0;
    *argOffsetOut = g_codeMeshArgCount;
    g_codeMeshArgCount += static_cast<std::uint32_t>(count);
    return 1;
}

float (*__cdecl R_GetCodeMeshArgs(std::uint32_t argOffset))[4]
{
    return g_processCodeMesh && argOffset < g_codeMeshArgCount
        ? reinterpret_cast<float (*)[4]>(
        g_codeMeshArgs.data() + static_cast<std::size_t>(argOffset) * 4u)
        : nullptr;
}

void __cdecl R_BeginCodeMeshVerts()
{
    iassert(!g_processCodeMesh);
    g_processCodeMesh = true;
}

void __cdecl R_EndCodeMeshVerts()
{
    g_processCodeMesh = false;
}

void __cdecl R_AddCodeMeshDrawSurf(Material *material,
    r_double_index_t *indices, std::uint32_t indexCount,
    std::uint32_t argOffset, std::uint32_t argCount, const char *fxName)
{
    (void)fxName;
    if (!material || !indices || indexCount == 0u || (indexCount & 1u) != 0u ||
        argOffset >= 256u || argCount > 256u - argOffset)
        return;

    const std::uintptr_t indexAddress =
        reinterpret_cast<std::uintptr_t>(indices);
    const std::uintptr_t storageBegin = reinterpret_cast<std::uintptr_t>(
        g_codeMeshIndices.data());
    const std::uintptr_t storageEnd = storageBegin +
        sizeof(g_codeMeshIndices);
    if (indexAddress < storageBegin || indexAddress >= storageEnd ||
        (indexAddress - storageBegin) % sizeof(r_double_index_t) != 0u)
        return;
    const std::size_t physicalOffset = static_cast<std::size_t>(
        (indexAddress - storageBegin) / sizeof(r_double_index_t));
    const std::size_t pairCount = indexCount / 2u;
    if (physicalOffset > g_codeMeshIndices.size() ||
        pairCount > g_codeMeshIndices.size() - physicalOffset ||
        physicalOffset > g_codeMeshIndexCount / 2u ||
        pairCount > (g_codeMeshIndexCount / 2u) - physicalOffset)
        return;

    const std::size_t originalVertexCount = g_codeMeshRenderVertices.size();
    const std::size_t originalIndexCount = g_codeMeshRenderIndices.size();
    try
    {
        const WebRendererCodeMeshResult conversion =
            WebRenderer_AppendCodeMeshBatch(
                g_codeMeshVerts.data(), g_codeMeshVertCount,
                reinterpret_cast<const std::uint32_t *>(indices), indexCount,
                g_codeMeshRenderVertices, g_codeMeshRenderIndices);
        if (conversion != WebRendererCodeMeshResult::Success)
            return;

        WebRendererWorldBatchDesc batch{};
        batch.firstIndex = static_cast<std::uint32_t>(
            g_codeMeshRenderIndices.size() - indexCount);
        batch.indexCount = indexCount;
        batch.surfaceCount = 1u;
        batch.firstSurfaceIndex = 0u;
        batch.lastSurfaceIndex = 0u;
        batch.materialIdentity = material;
        batch.materialName = material->info.name ? material->info.name : "<fx>";
        batch.modelName = "<fx-code-mesh>";
        batch.firstInstanceIndex = UINT32_MAX;
        batch.lastInstanceIndex = UINT32_MAX;
        batch.sourceKind = WebRendererSceneBatchKind::FxCodeMesh;
        batch.samplerState = 0u;
        if (material->textureTable)
        {
            const MaterialTextureDef *fallback = nullptr;
            for (std::uint32_t textureIndex = 0u;
                 textureIndex < material->textureCount; ++textureIndex)
            {
                const MaterialTextureDef &texture =
                    material->textureTable[textureIndex];
                if (!fallback && texture.semantic == 0u && texture.u.image)
                    fallback = &texture;
                if (texture.semantic == 2u && texture.u.image)
                {
                    batch.baseImage = texture.u.image;
                    batch.samplerState = texture.samplerState;
                    break;
                }
            }
            if (!batch.baseImage && fallback)
            {
                batch.baseImage = fallback->u.image;
                batch.samplerState = fallback->samplerState;
            }
        }
        constexpr std::uint32_t FX_TECHNIQUE_INDEX = 5u;
        const std::uint8_t stateEntry = material->stateBitsEntry[
            FX_TECHNIQUE_INDEX];
        if (material->stateBitsTable && stateEntry != 0xffu &&
            stateEntry < material->stateBitsCount)
        {
            batch.stateBits[0] = material->stateBitsTable[stateEntry].loadBits[0];
            batch.stateBits[1] = material->stateBitsTable[stateEntry].loadBits[1];
        }
        else
        {
            // Ordinary FX sprites are translucent and should test depth
            // without writing it. This is the deterministic fallback when a
            // synthetic or partially loaded material has no emissive state.
            batch.stateBits[0] = WEB_RENDERER_FX_FALLBACK_STATE_BITS0;
            batch.stateBits[1] = WEB_RENDERER_FX_FALLBACK_STATE_BITS1;
        }
        batch.technique = batch.baseImage
            ? WebRendererWorldTechnique::BaseTexture
            : WebRendererWorldTechnique::BackendFallback;
        g_codeMeshRenderBatches.push_back(batch);
    }
    catch (const std::bad_alloc &)
    {
        g_codeMeshRenderVertices.resize(originalVertexCount);
        g_codeMeshRenderIndices.resize(originalIndexCount);
        return;
    }
}

GfxParticleCloud *__cdecl R_AddParticleCloudToScene(Material *material)
{
    GfxParticleCloud *cloud = nullptr;
    const WebRendererParticleCloudRetainResult retained =
        WebRenderer_RetainParticleCloudSubmission(
            g_particleCloudSubmissions.data(),
            &g_particleCloudSubmissionCount, material, &cloud);
    if (retained == WebRendererParticleCloudRetainResult::LimitReached)
        R_WarnOncePerFrame(R_WARN_MAX_CLOUDS);
    return cloud;
}

void __cdecl R_FilterXModelIntoScene(
    const XModel *model, const GfxScaledPlacement *placement,
    std::uint16_t, std::uint16_t *)
{
    const WebRendererFxModelRetainResult retained =
        WebRenderer_RetainFxModelSubmission(
            g_fxModelSubmissions.data(), &g_fxModelSubmissionCount,
            model, placement, 0u);
    if (retained == WebRendererFxModelRetainResult::LimitReached)
    {
        R_WarnOncePerFrame(R_WARN_MAX_SCENE_MODEL_REFS);
        return;
    }
    if (retained == WebRendererFxModelRetainResult::InvalidSubmission)
    {
        R_WarnOncePerFrame(R_WARN_UNKNOWN_XMODEL_SHADER);
        return;
    }
}

void __cdecl R_AddOmniLightToScene(const float *, float, float, float, float) {}
void __cdecl R_AddSpotLightToScene(const float *, const float *, float,
    float, float, float) {}
void __cdecl R_SetLodOrigin(const refdef_s *) {}
void __cdecl R_RenderScene(const refdef_s *refdef)
{
    iassert(refdef->tanHalfFovX > 0.0f);
    iassert(refdef->tanHalfFovY > 0.0f);
    iassert(refdef->height > 0u);
    iassert(refdef->width > 0u);
    iassert(refdef->localClientNum == 0);
    if (!g_rendererWorldReady || !s_world.name)
        Com_Error(ERR_DROP, "R_RenderScene: NULL worldmodel");

    WebRendererSceneViewDesc view{};
    view.x = refdef->x;
    view.y = refdef->y;
    view.width = refdef->width;
    view.height = refdef->height;
    view.tanHalfFovX = refdef->tanHalfFovX;
    view.tanHalfFovY = refdef->tanHalfFovY;
    std::memcpy(view.viewOrigin, refdef->vieworg, sizeof(view.viewOrigin));
    std::memcpy(view.viewAxis, refdef->viewaxis, sizeof(view.viewAxis));
    view.time = refdef->time;
    view.zNear = refdef->zNear > 0.0f
        ? refdef->zNear
        : std::max(0.01f, r_znear ? r_znear->current.value : 4.0f);
    view.localClientNum = refdef->localClientNum;
    view.worldName = s_world.name;

    mat4x4 viewMatrix{};
    mat4x4 projectionMatrix{};
    mat4x4 d3dViewProjectionMatrix{};
    mat4x4 depthRangeConversion{};
    mat4x4 webglViewProjectionMatrix{};
    MatrixForViewer(viewMatrix, view.viewOrigin, view.viewAxis);
    InfinitePerspectiveMatrix(
        projectionMatrix, view.tanHalfFovX, view.tanHalfFovY, view.zNear);
    MatrixMultiply44(
        viewMatrix, projectionMatrix, d3dViewProjectionMatrix);
    // Kisak's D3D9 projection emits NDC depth [0, 1], while WebGL clips in
    // [-1, 1]. Preserve the canonical view/projection and apply only the
    // graphics-API depth-range conversion at the backend boundary.
    depthRangeConversion[0][0] = 1.0f;
    depthRangeConversion[1][1] = 1.0f;
    depthRangeConversion[2][2] = 2.0f;
    depthRangeConversion[3][2] = -1.0f;
    depthRangeConversion[3][3] = 1.0f;
    MatrixMultiply44(d3dViewProjectionMatrix, depthRangeConversion,
        webglViewProjectionMatrix);
    std::memcpy(view.viewProjectionMatrix, webglViewProjectionMatrix,
        sizeof(view.viewProjectionMatrix));

    if (!g_gameDrivenFrameReported)
    {
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Cgame view axes: forward=(%.3f %.3f %.3f), "
            "right=(%.3f %.3f %.3f), up=(%.3f %.3f %.3f).\n",
            view.viewAxis[0][0], view.viewAxis[0][1], view.viewAxis[0][2],
            view.viewAxis[1][0], view.viewAxis[1][1], view.viewAxis[1][2],
            view.viewAxis[2][0], view.viewAxis[2][1], view.viewAxis[2][2]);
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer world bounds: mins=(%.1f %.1f %.1f), "
            "maxs=(%.1f %.1f %.1f), surfaces=%d, model surfaces=%u, sky=%d.\n",
            s_world.mins[0], s_world.mins[1], s_world.mins[2],
            s_world.maxs[0], s_world.maxs[1], s_world.maxs[2],
            s_world.surfaceCount,
            s_world.modelCount > 0 ? s_world.models[0].surfaceCount : 0u,
            s_world.skySurfCount);
        std::uint32_t indexedLightmapSurfaces = 0u;
        std::uint32_t litTechniqueSurfaces = 0u;
        std::uint32_t primaryLightmapSamplerSurfaces = 0u;
        std::uint32_t primaryLightmapAnyPassSurfaces = 0u;
        const std::uint32_t worldSurfaceCount = s_world.modelCount > 0
            ? std::min<std::uint32_t>(s_world.models[0].surfaceCount,
                static_cast<std::uint32_t>(s_world.surfaceCount))
            : 0u;
        for (std::uint32_t index = 0u; index < worldSurfaceCount; ++index)
        {
            const GfxSurface &surface = s_world.dpvs.surfaces[index];
            if (surface.lightmapIndex != 31u) ++indexedLightmapSurfaces;
            const MaterialTechnique *lit = surface.material &&
                surface.material->techniqueSet
                ? surface.material->techniqueSet->techniques[7] : nullptr;
            if (!lit) continue;
            ++litTechniqueSurfaces;
            if (lit->passCount > 0u &&
                (lit->passArray[0].customSamplerFlags & 2u) != 0u)
            {
                ++primaryLightmapSamplerSurfaces;
            }
            bool anyPrimary = false;
            for (std::uint32_t pass = 0u; pass < lit->passCount; ++pass)
                anyPrimary = anyPrimary ||
                    (lit->passArray[pass].customSamplerFlags & 2u) != 0u;
            if (anyPrimary) ++primaryLightmapAnyPassSurfaces;
        }
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical world lightmap inventory: %u/%u indexed, "
            "%u lit-technique, %u primary-first-pass, %u primary-any-pass, "
            "%u lightmap pairs.\n",
            indexedLightmapSurfaces, worldSurfaceCount, litTechniqueSurfaces,
            primaryLightmapSamplerSurfaces,
            primaryLightmapAnyPassSurfaces, s_world.lightmapCount);
    }

    if (!g_worldSceneSubmitted)
    {
        WebRendererWorldSceneCommand command;
        const WebRendererWorldSceneResult build =
            WebRenderer_BuildWorldSceneCommand(s_world, view, command);
        if (build == WebRendererWorldSceneResult::NoVisibleSurface)
        {
            g_worldSceneSurfaceCount = 0u;
            g_worldSceneVertexCount = 0u;
            g_worldSceneIndexCount = 0u;
        }
        else if (build != WebRendererWorldSceneResult::Success)
        {
            Com_Error(ERR_DROP, "R_RenderScene: %s",
                WebRenderer_WorldSceneResultString(build));
            return;
        }
        else
        {
            const WebRendererWorldSurfaceDesc surface{
                command.vertices.data(),
                static_cast<std::uint32_t>(command.vertices.size()),
                command.indices.data(),
                static_cast<std::uint32_t>(command.indices.size()),
                command.batches.data(),
                static_cast<std::uint32_t>(command.batches.size()),
            };
            const WebRendererSurfaceResult submission =
                WebRenderer_SetWorldSurface(surface);
            if (submission != WebRendererSurfaceResult::Success)
            {
                Com_Error(ERR_DROP,
                    "R_RenderScene: portable world command %s",
                    WebRenderer_SurfaceResultString(submission));
                return;
            }
            g_worldSceneSurfaceCount = command.surfaceCount;
            g_worldSceneVertexCount =
                static_cast<std::uint32_t>(command.vertices.size());
            g_worldSceneIndexCount =
                static_cast<std::uint32_t>(command.indices.size());
            g_worldSceneSubmitted = true;
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Renderer frontend submitted %u opaque world "
                "surfaces in %zu canonical material/lightmap batches "
                "(%u vertices, %u indices) from cgame view.\n",
                g_worldSceneSurfaceCount, command.batches.size(),
                g_worldSceneVertexCount, g_worldSceneIndexCount);
        }
    }

    if (!g_staticModelSceneSubmitted)
    {
        WebRendererStaticModelSceneCommand command;
        const WebRendererStaticModelSceneResult build =
            WebRenderer_BuildStaticModelSceneCommand(s_world, command);
        if (build == WebRendererStaticModelSceneResult::NoStaticModels)
        {
            g_staticModelSceneSubmitted = true;
        }
        else if (build != WebRendererStaticModelSceneResult::Success)
        {
            Com_Error(ERR_DROP, "R_RenderScene static XModels: %s",
                WebRenderer_StaticModelSceneResultString(build));
            return;
        }
        else
        {
            const WebRendererStaticModelSceneDesc scene{
                command.vertices.data(),
                static_cast<std::uint32_t>(command.vertices.size()),
                command.indices.data(),
                static_cast<std::uint32_t>(command.indices.size()),
                command.instances.data(),
                static_cast<std::uint32_t>(command.instances.size()),
                command.batches.data(),
                static_cast<std::uint32_t>(command.batches.size()),
                command.modelCount,
                command.surfaceCount,
            };
            const WebRendererSurfaceResult submission =
                WebRenderer_SetStaticModelScene(scene);
            if (submission != WebRendererSurfaceResult::Success)
            {
                Com_Error(ERR_DROP,
                    "R_RenderScene: portable static XModel command %s",
                    WebRenderer_SurfaceResultString(submission));
                return;
            }
            g_staticModelSceneSubmitted = true;
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Renderer frontend submitted %u canonical "
                "static XModels as %zu shared XSurface batches (%zu vertices, "
                "%zu indices, %zu instances).\n",
                command.modelCount,
                command.batches.size(),
                command.vertices.size(),
                command.indices.size(),
                command.instances.size());
        }
    }

    WebRendererDObjSceneCommand dynamicCommand;
    const WebRendererDObjSceneResult dynamicBuild =
        WebRenderer_BuildDObjSceneCommand(
            g_dobjSubmissions.data(), g_dobjSubmissionCount,
            dynamicCommand);
    if (dynamicBuild == WebRendererDObjSceneResult::NoDObj)
    {
        // Keep the command empty for now; canonical code meshes below may
        // provide the only dynamic pass for this frame.
    }
    else if (dynamicBuild != WebRendererDObjSceneResult::Success)
    {
        Com_Error(ERR_DROP, "R_RenderScene dynamic DObj: %s",
            WebRenderer_DObjSceneResultString(dynamicBuild));
        return;
    }
    else
    {
        // Canonical DObj skeleton translations are maintained relative to
        // refdef.viewOffset for floating-origin precision. Native skinned
        // draws restore scene.def.viewOffset as their placement; do the same
        // at this portable frontend/backend boundary.
        for (WebRendererSurfaceVertex &vertex : dynamicCommand.vertices)
        {
            vertex.position[0] += refdef->viewOffset[0];
            vertex.position[1] += refdef->viewOffset[1];
            vertex.position[2] += refdef->viewOffset[2];
        }
    }

    // The exact native R_SkinXModel LOD ramp depends on renderer-global FOV
    // dvars that are not compiled into this frontend. Select the same model
    // thresholds from active refdef distance, scaled by placement, as the
    // deterministic rigid FX compatibility subset.
    std::uint32_t selectedFxModels = 0u;
    std::uint32_t droppedFxSelections = 0u;
    for (std::uint32_t index = 0u; index < g_fxModelSubmissionCount; ++index)
    {
        WebRendererFxModelSubmission submission = g_fxModelSubmissions[index];
        const int lod = WebRenderer_SelectFxModelLod(
            submission.model, submission.placement, refdef->vieworg);
        if (lod < 0)
        {
            if (droppedFxSelections != UINT32_MAX) ++droppedFxSelections;
            continue;
        }
        submission.lod = static_cast<std::uint16_t>(lod);
        g_fxModelSubmissions[selectedFxModels++] = submission;
    }
    g_fxModelSubmissionCount = selectedFxModels;
    if (droppedFxSelections != 0u)
        R_WarnOncePerFrame(R_WARN_UNKNOWN_XMODEL_SHADER);

    WebRendererFxModelSceneCommand fxModelCommand;
    std::uint32_t droppedFxModels = 0u;
    WebRendererFxModelSceneResult fxModelBuild =
        WebRenderer_BuildFxModelSceneCommand(
            g_fxModelSubmissions.data(), g_fxModelSubmissionCount,
            fxModelCommand, &droppedFxModels);
    if (droppedFxModels != 0u)
        R_WarnOncePerFrame(R_WARN_UNKNOWN_XMODEL_SHADER);
    if (fxModelBuild != WebRendererFxModelSceneResult::Success &&
        fxModelBuild != WebRendererFxModelSceneResult::NoFxModel)
    {
        // FX model presentation is optional. Geometry/output exhaustion is a
        // family-level drop, so DObj and code-mesh commands still render and
        // the backend receives a fresh dynamic scene below.
        R_WarnOncePerFrame(R_WARN_MAX_SCENE_MODEL_REFS);
        fxModelCommand = {};
        fxModelBuild = WebRendererFxModelSceneResult::NoFxModel;
    }

    // EffectsCore remains the sole producer of FX geometry. Append its
    // already-converted canonical code-mesh spans to the existing dynamic
    // command so the backend retains one ordered, copied scene submission.
    const bool hasCodeMesh = !g_codeMeshRenderBatches.empty();
    bool hasFxModel = fxModelBuild ==
        WebRendererFxModelSceneResult::Success;
    if (hasFxModel)
    {
        const WebRendererFxModelAppendResult admission =
            WebRenderer_ValidateFxModelAdmissionCounts(
                dynamicCommand.vertices.size(),
                dynamicCommand.indices.size(),
                dynamicCommand.batches.size(),
                dynamicCommand.surfaceCount,
                fxModelCommand.vertices.size(),
                fxModelCommand.indices.size(),
                fxModelCommand.batches.size(),
                fxModelCommand.surfaceCount,
                g_codeMeshRenderVertices.size(),
                g_codeMeshRenderIndices.size(),
                g_codeMeshRenderBatches.size(),
                static_cast<std::uint32_t>(g_codeMeshRenderBatches.size()));
        const WebRendererFxModelAppendResult appendResult = admission ==
            WebRendererFxModelAppendResult::Success
            ? WebRenderer_AppendFxModelSceneCommand(
                fxModelCommand,
                dynamicCommand.vertices,
                dynamicCommand.indices,
                dynamicCommand.batches,
                dynamicCommand.surfaceCount)
            : admission;
        if (appendResult != WebRendererFxModelAppendResult::Success)
        {
            R_WarnOncePerFrame(R_WARN_MAX_SCENE_MODEL_REFS);
            hasFxModel = false;
        }
    }
    if (hasCodeMesh)
    {
        try
        {
            const std::uint32_t vertexBase = static_cast<std::uint32_t>(
                dynamicCommand.vertices.size());
            const std::uint32_t indexBase = static_cast<std::uint32_t>(
                dynamicCommand.indices.size());
            if (vertexBase > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                    g_codeMeshRenderVertices.size() ||
                indexBase > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
                    g_codeMeshRenderIndices.size())
            {
                Com_Error(ERR_DROP,
                    "R_RenderScene canonical code mesh exceeds dynamic limits");
                return;
            }
            dynamicCommand.vertices.insert(dynamicCommand.vertices.end(),
                g_codeMeshRenderVertices.begin(), g_codeMeshRenderVertices.end());
            for (const std::uint32_t index : g_codeMeshRenderIndices)
                dynamicCommand.indices.push_back(vertexBase + index);
            for (WebRendererWorldBatchDesc batch : g_codeMeshRenderBatches)
            {
                batch.firstIndex += indexBase;
                dynamicCommand.batches.push_back(batch);
            }
            dynamicCommand.surfaceCount += static_cast<std::uint32_t>(
                g_codeMeshRenderBatches.size());
        }
        catch (const std::bad_alloc &)
        {
            Com_Error(ERR_DROP,
                "R_RenderScene canonical code mesh allocation failed");
            return;
        }
    }

    // EffectsCore remains the sole producer of particle-cloud state. Expand
    // each retained canonical slot at this renderer boundary only after the
    // required DObj, FX-model, and code-mesh families have been admitted.
    // Each cloud is an all-or-nothing 1024-quad batch, so optional clouds can
    // be dropped without displacing earlier canonical geometry.
    WebRendererParticleCloudView particleCloudView{};
    std::memcpy(particleCloudView.origin, refdef->vieworg,
        sizeof(particleCloudView.origin));
    std::memcpy(particleCloudView.axis, refdef->viewaxis,
        sizeof(particleCloudView.axis));
    bool hasParticleCloud = false;
    std::uint32_t droppedParticleClouds = 0u;
    for (std::uint32_t index = 0u;
         index < g_particleCloudSubmissionCount; ++index)
    {
        WebRendererParticleCloudSceneCommand cloudCommand;
        const WebRendererParticleCloudSceneResult build =
            WebRenderer_BuildParticleCloudCommand(
                g_particleCloudSubmissions[index], particleCloudView,
                cloudCommand);
        if (build != WebRendererParticleCloudSceneResult::Success)
        {
            if (droppedParticleClouds != UINT32_MAX) ++droppedParticleClouds;
            continue;
        }
        const WebRendererParticleCloudAppendResult append =
            WebRenderer_AppendParticleCloudCommand(
                cloudCommand,
                dynamicCommand.vertices,
                dynamicCommand.indices,
                dynamicCommand.batches,
                dynamicCommand.surfaceCount);
        if (append == WebRendererParticleCloudAppendResult::Success)
            hasParticleCloud = true;
        else if (droppedParticleClouds != UINT32_MAX)
            ++droppedParticleClouds;
    }
    if (droppedParticleClouds != 0u)
        R_WarnOncePerFrame(R_WARN_MAX_CLOUDS);

    if (dynamicBuild == WebRendererDObjSceneResult::Success ||
        hasFxModel || hasCodeMesh || hasParticleCloud)
    {
        const WebRendererWorldSurfaceDesc scene{
            dynamicCommand.vertices.data(),
            static_cast<std::uint32_t>(dynamicCommand.vertices.size()),
            dynamicCommand.indices.data(),
            static_cast<std::uint32_t>(dynamicCommand.indices.size()),
            dynamicCommand.batches.data(),
            static_cast<std::uint32_t>(dynamicCommand.batches.size()),
        };
        const WebRendererSurfaceResult submission =
            WebRenderer_SetDynamicModelScene(scene);
        if (submission != WebRendererSurfaceResult::Success)
        {
            Com_Error(ERR_DROP, "R_RenderScene dynamic/canonical FX command %s",
                WebRenderer_SurfaceResultString(submission));
            return;
        }
    }
    else
    {
        WebRenderer_SetDynamicModelScene({});
    }

    if (dynamicBuild == WebRendererDObjSceneResult::Success)
    {
        if (!g_dynamicModelSceneReported)
        {
            g_dynamicModelSceneReported = true;
            float mins[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
            float maxs[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (const WebRendererSurfaceVertex &vertex :
                 dynamicCommand.vertices)
            {
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    mins[axis] = std::min(mins[axis], vertex.position[axis]);
                    maxs[axis] = std::max(maxs[axis], vertex.position[axis]);
                }
            }
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical dynamic DObj scene: %u DObjs, "
                "%u models, %u surfaces; bounds=(%.1f %.1f %.1f)-(%.1f "
                "%.1f %.1f), first model='%s', first material='%s', "
                "techniques=%u/%u/%u, base=%d/%d/%d.\n",
                dynamicCommand.dobjCount, dynamicCommand.modelCount,
                dynamicCommand.surfaceCount,
                mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2],
                dynamicCommand.batches[0].modelName,
                dynamicCommand.batches[0].materialName,
                static_cast<unsigned int>(
                    dynamicCommand.batches[0].technique),
                dynamicCommand.batches.size() > 1u
                    ? static_cast<unsigned int>(
                        dynamicCommand.batches[1].technique) : 99u,
                dynamicCommand.batches.size() > 2u
                    ? static_cast<unsigned int>(
                        dynamicCommand.batches[2].technique) : 99u,
                dynamicCommand.batches[0].baseImage != nullptr,
                dynamicCommand.batches.size() > 1u &&
                    dynamicCommand.batches[1].baseImage != nullptr,
                dynamicCommand.batches.size() > 2u &&
                    dynamicCommand.batches[2].baseImage != nullptr);
        }
    }
    if (g_uiBatches.empty())
    {
        WebRenderer_SetUiScene({});
    }
    else
    {
        const WebRendererUiSceneDesc uiScene{
            g_uiVertices.data(),
            static_cast<std::uint32_t>(g_uiVertices.size()),
            g_uiIndices.data(),
            static_cast<std::uint32_t>(g_uiIndices.size()),
            g_uiBatches.data(),
            static_cast<std::uint32_t>(g_uiBatches.size()),
        };
        const WebRendererSurfaceResult uiSubmission =
            WebRenderer_SetUiScene(uiScene);
        if (uiSubmission != WebRendererSurfaceResult::Success)
        {
            Com_Error(ERR_DROP, "R_RenderScene canonical 2D command %s",
                WebRenderer_SurfaceResultString(uiSubmission));
            return;
        }
        if (!g_uiSceneReported)
        {
            g_uiSceneReported = true;
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Renderer frontend submitted the first "
                "canonical 2D scene (%zu quads, %zu batches).\n",
                g_uiVertices.size() / 4u, g_uiBatches.size());
            std::array<const Material *, 12> reportedMaterials{};
            std::size_t reportedCount = 0u;
            for (const WebRendererUiBatchDesc &batch : g_uiBatches)
            {
                bool duplicate = false;
                for (std::size_t index = 0u; index < reportedCount; ++index)
                    duplicate |= reportedMaterials[index] ==
                        batch.materialIdentity;
                if (duplicate || reportedCount == reportedMaterials.size())
                    continue;
                reportedMaterials[reportedCount++] = batch.materialIdentity;
                Web_Log(WebLogLevel::Info,
                    "[kisakcod-web] Canonical 2D material '%s': image='%s' "
                    "map=%u size=%ux%u sampler=0x%02x.\n",
                    batch.materialName,
                    batch.image && batch.image->name
                        ? batch.image->name : "<solid/fallback>",
                    batch.image ? batch.image->mapType : 0u,
                    batch.image ? batch.image->width : 0u,
                    batch.image ? batch.image->height : 0u,
                    batch.samplerState);
            }
        }
    }
    view.worldSurfaceCount = g_worldSceneSurfaceCount;
    view.worldVertexCount = g_worldSceneVertexCount;
    view.worldIndexCount = g_worldSceneIndexCount;
    view.geometrySubmitted = g_worldSceneSubmitted;
    if (!WebRenderer_SubmitSceneView(view))
        Com_Error(ERR_DROP, "R_RenderScene: invalid cgame view command");

    if (!g_gameDrivenFrameReported)
    {
        g_gameDrivenFrameReported = true;
        EmitEngineLifecycleTrace(
            EngineLifecycleStage::GameDrivenFrame, s_world.name);
    }
}

GfxBrushModel *__cdecl R_GetBrushModel(std::uint32_t index)
{
    return index < s_world.modelCount ? &s_world.models[index] : nullptr;
}

void __cdecl R_AddBrushModelToSceneFromAngles(
    const GfxBrushModel *, const float *, const float *, std::uint16_t) {}
void __cdecl R_AddDObjToScene(const DObj_s *obj, const cpose_t *pose,
    std::uint32_t entityNumber, std::uint32_t renderFlags, float *, float)
{
    // The canonical weapon path marks first-person DObjs with flags 3. Keep
    // the callback identity and pose intact until R_RenderScene consumes it
    // synchronously; ordinary entity DObjs can follow through this seam later.
    if ((renderFlags & 3u) != 3u || !obj || !pose ||
        g_dobjSubmissionCount >= g_dobjSubmissions.size())
    {
        return;
    }
    g_dobjSubmissions[g_dobjSubmissionCount++] = {
        obj, pose, entityNumber, renderFlags};
}
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
    FX_AddNonSpriteDrawSurfs(cmd);

    // Native queues mark generation between non-sprite and FX-vertex work.
    // The web renderer has no portable mark-mesh destination yet, so retain
    // the canonical producer order while leaving that optional family out.
    FxGenerateVertsCmd generateVertsCmd{};
    FX_FillGenerateVertsCmd(cmd->localClientNum, &generateVertsCmd);
    FX_GenerateVerts(&generateVertsCmd);
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
