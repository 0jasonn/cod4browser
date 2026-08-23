// Browser renderer frontend closure for the canonical SP runtime. This owns
// presentation and scene APIs at the platform boundary; the DB-owned world
// remains s_world and the existing bounded WebGL2 adapter remains the only
// world conversion/submission path.

#include <client/client.h>
#include <database/database.h>
#include <DynEntity/DynEntity_client.h>
#include <EffectsCore/fx_system.h>
#include <gfx_d3d/fxprimitives.h>
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
#include <qcommon/com_bsp.h>
#include <qcommon/com_world_types.h>
#include <qcommon/com_world_runtime.h>
#include <qcommon/cmd.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <stringed/stringed_hooks.h>
#include <universal/com_math.h>
#include <universal/com_memory.h>
#include <universal/memfile.h>
#include <web/web_renderer.h>
#include <web/web_renderer_image_reference.h>
#include <web/web_renderer_code_mesh.h>
#include <web/web_renderer_dobj_scene.h>
#include <web/web_renderer_fx_model_scene.h>
#include <web/web_renderer_lighting.h>
#include <web/web_renderer_mark_mesh.h>
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
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

enum CubemapShot : int;

extern GfxWorld s_world;

namespace
{
EM_JS(
    void,
    DispatchRendererVisionLighting,
    (bool filmEnabled,
     float filmBrightness,
     float filmContrast,
     float filmDesaturation,
     bool filmInvert,
     const float *filmTintDark,
     const float *filmTintLight,
     bool glowEnabled,
     float glowBloomCutoff,
     float glowBloomDesaturation,
     float glowBloomIntensity,
     float glowRadius,
     float gamma),
    {
        const detail = {
            source: "canonical-refdef",
            film: {
                enabled: Boolean(filmEnabled),
                brightness: filmBrightness,
                contrast: filmContrast,
                desaturation: filmDesaturation,
                invert: Boolean(filmInvert),
                tintDark: [HEAPF32[filmTintDark >> 2],
                    HEAPF32[(filmTintDark >> 2) + 1],
                    HEAPF32[(filmTintDark >> 2) + 2]],
                tintLight: [HEAPF32[filmTintLight >> 2],
                    HEAPF32[(filmTintLight >> 2) + 1],
                    HEAPF32[(filmTintLight >> 2) + 2]]
            },
            glow: {
                enabled: Boolean(glowEnabled),
                bloomCutoff: glowBloomCutoff,
                bloomDesaturation: glowBloomDesaturation,
                bloomIntensity: glowBloomIntensity,
                radius: glowRadius
            },
            gamma,
            normalized: true,
            containsGpuHandles: false,
            containsObjectAddresses: false
        };
        globalThis.__KISAKCOD_RENDERER_VISION_LIGHTING__ = detail;
        globalThis.dispatchEvent(new CustomEvent(
            "kisakcod:renderer-vision-lighting", { detail }));
    });

bool CanonicalLightGridSampleVisible(
    const float samplePosition[3], const float gridPosition[3],
    void *) noexcept
{
    float direction[3]{
        samplePosition[0] - gridPosition[0],
        samplePosition[1] - gridPosition[1],
        samplePosition[2] - gridPosition[2],
    };
    const float lengthSquared = direction[0] * direction[0] +
        direction[1] * direction[1] + direction[2] * direction[2];
    if (lengthSquared > 0.000001f)
    {
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        for (float &component : direction) component *= inverseLength;
    }
    const float nudgedGridPosition[3]{
        gridPosition[0] + direction[0] * 0.01f,
        gridPosition[1] + direction[1] * 0.01f,
        gridPosition[2] + direction[2] * 0.01f,
    };
    return CM_BoxSightTrace(
        0, samplePosition, nudgedGridPosition,
        vec3_origin, vec3_origin, 0u, 8193) == 0;
}

Material *ResolveRendererMaterial(Material *material) noexcept
{
    const char *lookupName = WebRenderer_MaterialLookupName(material);
    if (!lookupName) return material;
    if (Material *canonical = DB_FindXAssetHeader(
            ASSET_TYPE_MATERIAL, lookupName).material)
        return canonical;
    return material;
}

const GfxImage *FindRendererImage(const char *lookupName) noexcept
{
    GfxImage *canonical = DB_FindXAssetHeader(
        ASSET_TYPE_IMAGE, lookupName).image;
    return canonical && canonical->name &&
            I_stricmp(canonical->name, lookupName) == 0
        ? canonical : nullptr;
}

const GfxImage *ResolveRendererImage(const GfxImage *image) noexcept
{
    return WebRenderer_ResolveImageReference(image, FindRendererImage);
}

void ResolveRendererBatchImages(WebRendererWorldBatchDesc &batch) noexcept
{
    batch.baseImage = ResolveRendererImage(batch.baseImage);
    batch.normalImage = ResolveRendererImage(batch.normalImage);
    batch.lightmapImage = ResolveRendererImage(batch.lightmapImage);
    batch.secondaryLightmapImage =
        ResolveRendererImage(batch.secondaryLightmapImage);
    batch.reflectionProbeImage =
        ResolveRendererImage(batch.reflectionProbeImage);
}

bool BuildRendererPrimaryLights(
    WebRendererWorldSceneCommand &command) noexcept
{
    if (!comWorld.isInUse || !comWorld.primaryLights ||
        comWorld.primaryLightCount > WEB_RENDERER_MAX_PRIMARY_LIGHTS)
    {
        return false;
    }
    try
    {
        command.primaryLights.clear();
        command.primaryLights.resize(comWorld.primaryLightCount);
        command.sunPrimaryLightIndex = s_world.sunPrimaryLightIndex;
        for (std::uint32_t index = 0u;
             index < comWorld.primaryLightCount; ++index)
        {
            const ComPrimaryLight &source = comWorld.primaryLights[index];
            WebRendererPrimaryLightDesc &destination =
                command.primaryLights[index];
            destination.type = source.type;
            destination.exponent = source.exponent;
            std::copy_n(source.color, 3u, destination.color);
            std::copy_n(source.dir, 3u, destination.direction);
            std::copy_n(source.origin, 3u, destination.origin);
            destination.radius = source.radius;
            destination.cosHalfFovOuter = source.cosHalfFovOuter;
            destination.cosHalfFovInner = source.cosHalfFovInner;
            if (source.defName)
            {
                const GfxLightDef *definition = DB_FindXAssetHeader(
                    ASSET_TYPE_LIGHT_DEF, source.defName).lightDef;
                if (definition)
                {
                    destination.attenuationImage = ResolveRendererImage(
                        definition->attenuation.image);
                    destination.samplerState =
                        definition->attenuation.samplerState;
                }
            }
        }
        if (s_world.sunPrimaryLightIndex < command.primaryLights.size() &&
            s_world.sunLight)
        {
            WebRendererPrimaryLightDesc &sun =
                command.primaryLights[s_world.sunPrimaryLightIndex];
            sun.type = s_world.sunLight->type;
            std::copy_n(s_world.sunLight->color, 3u, sun.color);
            std::copy_n(s_world.sunLight->dir, 3u, sun.direction);
        }
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    return true;
}

bool CanonicalPrimaryLightInfluences(
    std::uint32_t primaryLightIndex, const float position[3],
    void *) noexcept
{
    if (primaryLightIndex == 0u ||
        primaryLightIndex >= Com_GetPrimaryLightCount())
        return false;
    const ComPrimaryLight *light =
        &comWorld.primaryLights[primaryLightIndex];
    if (light->type == 1u) return true;
    return Com_CanPrimaryLightAffectPoint(
        light, position) != 0;
}

const WebRendererModelLightingCallbacks MODEL_LIGHTING_CALLBACKS{
    CanonicalLightGridSampleVisible,
    CanonicalPrimaryLightInfluences,
    nullptr,
};

void __cdecl CollectTechniqueSet(XAssetHeader header, void *context)
{
    if (!header.techniqueSet || !context) return;
    static_cast<std::vector<MaterialTechniqueSet *> *>(context)->push_back(
        header.techniqueSet);
}

std::uint32_t ResolveTechniqueSetRemaps()
{
    std::vector<MaterialTechniqueSet *> techniqueSets;
    DB_EnumXAssets(ASSET_TYPE_TECHNIQUE_SET,
        CollectTechniqueSet, &techniqueSets, true);

    std::uint32_t remappedCount = 0u;
    for (MaterialTechniqueSet *source : techniqueSets)
    {
        if (!source || !source->name ||
            std::strncmp(source->name, "sm2/", 4u) == 0)
        {
            if (source) source->remappedTechniqueSet = source;
            continue;
        }
        const std::string targetName = std::string("sm2/") + source->name;
        const auto target = std::find_if(techniqueSets.begin(),
            techniqueSets.end(), [&targetName](const MaterialTechniqueSet *set)
            {
                return set && set->name && targetName == set->name;
            });
        if (target == techniqueSets.end())
        {
            source->remappedTechniqueSet = source;
            continue;
        }
        source->remappedTechniqueSet = *target;
        ++remappedCount;
    }
    return remappedCount;
}
}

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
std::array<WebRendererFog, 5> g_fogs{};
std::uint32_t g_fogIndex = 0;
float g_cullDistance = 0.0f;
float g_sunLightOverride[3]{};
float g_sunDirectionOverride[3]{};
float g_sunDirectionTarget[3]{};
bool g_hasSunLightOverride = false;
bool g_hasSunDirectionOverride = false;
bool g_rendererWorldReady = false;
bool g_gameDrivenFrameReported = false;
bool g_visionLightingReported = false;
bool g_sceneBlurReported = false;
bool g_worldSceneSubmitted = false;
bool g_staticModelSceneSubmitted = false;
bool g_dynamicModelSceneReported = false;
bool g_dynamicBrushSceneReported = false;
bool g_dynamicEntityModelSceneReported = false;
bool g_uiSceneReported = false;
std::array<WebRendererDObjSubmission,
    WEB_RENDERER_MAX_DYNAMIC_DOBJ_SUBMISSIONS> g_dobjSubmissions{};
std::uint32_t g_dobjSubmissionCount = 0u;
std::array<WebRendererBrushModelSubmission,
    WEB_RENDERER_MAX_DYNAMIC_BMODEL_SUBMISSIONS> g_brushModelSubmissions{};
std::uint32_t g_brushModelSubmissionCount = 0u;
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
std::array<const Material *, 24u> g_reportedCodeMeshMaterials{};
std::uint32_t g_reportedCodeMeshMaterialCount = 0u;
std::array<GfxWorldVertex, WEB_RENDERER_MAX_MARK_MESH_VERTICES>
    g_markMeshVerts{};
std::array<r_double_index_t, WEB_RENDERER_MAX_MARK_MESH_INDICES / 2u>
    g_markMeshIndices{};
std::uint32_t g_markMeshVertCount = 0u;
std::uint32_t g_markMeshIndexCount = 0u;
bool g_processMarkMesh = false;
struct WebPendingMarkDraw
{
    Material *material;
    GfxMarkContext context;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
};
constexpr std::uint32_t WEB_RENDERER_MAX_MARK_DRAWS = 0x600u;
std::array<WebPendingMarkDraw, WEB_RENDERER_MAX_MARK_DRAWS>
    g_pendingMarkDraws{};
std::uint32_t g_pendingMarkDrawCount = 0u;
std::uint32_t g_pendingMarkDrawBegin = 0u;
std::vector<WebRendererSurfaceVertex> g_markMeshRenderVertices;
std::vector<std::uint32_t> g_markMeshRenderIndices;
std::vector<WebRendererWorldBatchDesc> g_markMeshRenderBatches;
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

const GfxImage *FindFxImage(Material *material, std::uint8_t &sampler)
{
    if (!material || !material->textureTable) return nullptr;
    const MaterialTextureDef *fallback = nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (!fallback && texture.semantic == 0u && texture.u.image)
            fallback = &texture;
        if (texture.semantic == 2u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    if (!fallback) return nullptr;
    sampler = fallback->samplerState;
    return fallback->u.image;
}

std::uint32_t HashFxPixelShaderProgram(
    const MaterialPixelShader *shader) noexcept
{
    if (!shader || !shader->prog.loadDef.program ||
        shader->prog.loadDef.programSize == 0u)
        return 0u;
    constexpr std::uint32_t FNV_OFFSET = 2166136261u;
    constexpr std::uint32_t FNV_PRIME = 16777619u;
    std::uint32_t hash = FNV_OFFSET;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(
        shader->prog.loadDef.program);
    const std::size_t byteCount =
        static_cast<std::size_t>(shader->prog.loadDef.programSize) *
        sizeof(std::uint32_t);
    for (std::size_t index = 0u; index < byteCount; ++index)
        hash = (hash ^ bytes[index]) * FNV_PRIME;
    return hash;
}

const MaterialTechnique *FindMarkTechnique(const Material *material) noexcept
{
    constexpr std::uint32_t MARK_TECHNIQUE_INDEX = 7u;
    if (!material || !material->techniqueSet) return nullptr;
    const MaterialTechniqueSet *set =
        material->techniqueSet->remappedTechniqueSet
            ? material->techniqueSet->remappedTechniqueSet
            : material->techniqueSet;
    return set->techniques[MARK_TECHNIQUE_INDEX];
}

void ConfigureMarkBatch(WebRendererWorldBatchDesc &batch,
    Material *material, const GfxMarkContext &context) noexcept
{
    constexpr std::uint32_t MARK_TECHNIQUE_INDEX = 7u;
    batch.materialIdentity = material;
    batch.materialName = material && material->info.name
        ? material->info.name : "<mark>";
    batch.modelName = "<fx-mark-mesh>";
    batch.firstInstanceIndex = UINT32_MAX;
    batch.lastInstanceIndex = UINT32_MAX;
    batch.sourceKind = WebRendererSceneBatchKind::FxMarkMesh;
    batch.lightmapIndex = context.lmapIndex;
    batch.baseImage = FindFxImage(material, batch.samplerState);
    const MaterialTechnique *technique = FindMarkTechnique(material);
    batch.techniqueName = technique && technique->name
        ? technique->name : "<unsupported-mark-technique>";
    batch.techniqueType = MARK_TECHNIQUE_INDEX;
    if (technique)
    {
        batch.techniqueFlags = technique->flags;
        for (std::uint32_t pass = 0u; pass < technique->passCount; ++pass)
            batch.customSamplerFlags |=
                technique->passArray[pass].customSamplerFlags;
        if (technique->passCount != 0u)
        {
            const MaterialPixelShader *shader =
                technique->passArray[0u].pixelShader;
            batch.pixelShaderName = shader && shader->name
                ? shader->name : "<unavailable-pixel-shader>";
            batch.pixelShaderProgramHash = HashFxPixelShaderProgram(shader);
        }
    }
    if (material && material->stateBitsTable)
    {
        const std::uint8_t entry =
            material->stateBitsEntry[MARK_TECHNIQUE_INDEX];
        if (entry != 0xffu && entry < material->stateBitsCount)
        {
            batch.stateBits[0] = material->stateBitsTable[entry].loadBits[0];
            batch.stateBits[1] = material->stateBitsTable[entry].loadBits[1];
        }
    }
    if (context.lmapIndex != 31u && context.lmapIndex < s_world.lightmapCount &&
        s_world.lightmaps)
    {
        if ((batch.customSamplerFlags & 2u) != 0u)
            batch.lightmapImage = s_world.lightmaps[context.lmapIndex].primary;
        if ((batch.customSamplerFlags & 4u) != 0u)
            batch.secondaryLightmapImage =
                s_world.lightmaps[context.lmapIndex].secondary;
        if (batch.secondaryLightmapImage)
            batch.lightingMode =
                WebRendererWorldLightingMode::SecondaryDirectional;
    }
    if (!batch.baseImage || !technique)
        batch.technique = WebRendererWorldTechnique::BackendFallback;
    else if (batch.lightingMode ==
            WebRendererWorldLightingMode::SecondaryDirectional)
        batch.technique = WebRendererWorldTechnique::BaseTextureLightmap;
    else
        batch.technique = WebRendererWorldTechnique::BaseTexture;
}

void ConvertPendingMarkDraws(std::uint32_t firstDraw) noexcept
{
    for (std::uint32_t drawIndex = firstDraw;
         drawIndex < g_pendingMarkDrawCount; ++drawIndex)
    {
        const WebPendingMarkDraw &draw = g_pendingMarkDraws[drawIndex];
        const std::size_t originalVertexCount =
            g_markMeshRenderVertices.size();
        const std::size_t originalIndexCount = g_markMeshRenderIndices.size();
        const WebRendererMarkMeshResult converted =
            WebRenderer_AppendMarkMeshBatch(
                g_markMeshVerts.data(), g_markMeshVertCount,
                reinterpret_cast<const std::uint16_t *>(
                    g_markMeshIndices.data()) + draw.firstIndex,
                draw.indexCount,
                (draw.context.modelTypeAndSurf & 0xc0u) == 0u,
                g_markMeshRenderVertices, g_markMeshRenderIndices);
        if (converted != WebRendererMarkMeshResult::Success) continue;
        try
        {
            WebRendererWorldBatchDesc batch{};
            batch.firstIndex = static_cast<std::uint32_t>(
                g_markMeshRenderIndices.size() - draw.indexCount);
            batch.indexCount = draw.indexCount;
            batch.surfaceCount = 1u;
            batch.firstSurfaceIndex = draw.context.modelIndex;
            batch.lastSurfaceIndex = draw.context.modelIndex;
            ConfigureMarkBatch(batch, draw.material, draw.context);
            g_markMeshRenderBatches.push_back(batch);
        }
        catch (const std::bad_alloc &)
        {
            g_markMeshRenderVertices.resize(originalVertexCount);
            g_markMeshRenderIndices.resize(originalIndexCount);
        }
    }
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
    // WeaponDef reticles can retain a DB reference body serialized in a later
    // zone. Resolve it at material evaluation so the prerequisite-zone asset
    // supplies its texture, sampler, and authored unlit state.
    material = ResolveRendererMaterial(material);
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
        batch.hasMaterialState = WebRenderer_UnlitMaterialStateBits(
            material, batch.stateBits);
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

void SubmitUiScene()
{
    if (g_uiBatches.empty())
    {
        WebRenderer_SetUiScene({});
        return;
    }

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
        Com_Error(ERR_DROP, "R_EndFrame canonical 2D command %s",
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
    g_processMarkMesh = false;
    g_markMeshVertCount = 0u;
    g_markMeshIndexCount = 0u;
    g_pendingMarkDrawCount = 0u;
    g_pendingMarkDrawBegin = 0u;
    g_markMeshRenderVertices.clear();
    g_markMeshRenderIndices.clear();
    g_markMeshRenderBatches.clear();
    g_uiVertices.clear();
    g_uiIndices.clear();
    g_uiBatches.clear();
}
void __cdecl R_EndFrame()
{
    // UI_Refresh runs after the cgame's R_RenderScene call and is also the
    // only draw producer on fullscreen/script-menu frames. Publish 2D work at
    // the real frame boundary so menus cannot capture input without becoming
    // visible, and so HUD commands belong to the frame that produced them.
    SubmitUiScene();
}
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
    const std::uint32_t remappedTechniqueSets =
        ResolveTechniqueSetRemaps();
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Resolved %u canonical technique-set SM2 aliases "
        "after zone publication.\n",
        remappedTechniqueSets);
    g_rendererWorldReady = true;
    g_gameDrivenFrameReported = false;
    g_visionLightingReported = false;
    g_sceneBlurReported = false;
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
    g_brushModelSubmissionCount = 0u;
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
    if (!material || !indices || indexCount == 0u || (indexCount & 1u) != 0u ||
        argOffset >= 256u || argCount > 256u - argOffset)
        return;

    // Native DB_LinkXAssetEntry resolves dependency records before
    // EffectsCore observes them. Keep the browser compatibility lookup at
    // this material-evaluation seam rather than changing DB ownership.
    material = ResolveRendererMaterial(material);

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
        bool materialReported = false;
        for (std::uint32_t index = 0u;
             index < g_reportedCodeMeshMaterialCount; ++index)
            materialReported |= g_reportedCodeMeshMaterials[index] == material;
        if (!materialReported &&
            g_reportedCodeMeshMaterialCount <
                g_reportedCodeMeshMaterials.size())
        {
            g_reportedCodeMeshMaterials[g_reportedCodeMeshMaterialCount++] =
                material;
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical FX material '%s': effect='%s' "
                "image='%s' map=%u size=%ux%u semantic=%u sampler=0x%02x "
                "state=0x%08x/0x%08x.\n",
                batch.materialName, fxName ? fxName : "<unnamed-effect>",
                batch.baseImage && batch.baseImage->name
                    ? batch.baseImage->name : "<missing>",
                batch.baseImage ? batch.baseImage->mapType : 0u,
                batch.baseImage ? batch.baseImage->width : 0u,
                batch.baseImage ? batch.baseImage->height : 0u,
                batch.baseImage ? batch.baseImage->semantic : 0u,
                batch.samplerState, batch.stateBits[0], batch.stateBits[1]);
        }
        g_codeMeshRenderBatches.push_back(batch);
    }
    catch (const std::bad_alloc &)
    {
        g_codeMeshRenderVertices.resize(originalVertexCount);
        g_codeMeshRenderIndices.resize(originalIndexCount);
        return;
    }
}

char __cdecl R_ReserveMarkMeshVerts(int count, std::uint16_t *baseVertex)
{
    if (!g_processMarkMesh || count < 0 || !baseVertex ||
        static_cast<std::uint32_t>(count) >
            g_markMeshVerts.size() - g_markMeshVertCount)
    {
        R_WarnOncePerFrame(R_WARN_MAX_MARK_VERTS);
        return 0;
    }
    *baseVertex = static_cast<std::uint16_t>(g_markMeshVertCount);
    g_markMeshVertCount += static_cast<std::uint32_t>(count);
    return 1;
}

char __cdecl R_ReserveMarkMeshIndices(int count,
    r_double_index_t **indicesOut)
{
    if (!g_processMarkMesh || count < 0 || !indicesOut || (count & 1) != 0 ||
        static_cast<std::uint32_t>(count) >
            WEB_RENDERER_MAX_MARK_MESH_INDICES - g_markMeshIndexCount)
    {
        R_WarnOncePerFrame(R_WARN_MAX_MARK_INDS);
        return 0;
    }
    *indicesOut = &g_markMeshIndices[g_markMeshIndexCount / 2u];
    g_markMeshIndexCount += static_cast<std::uint32_t>(count);
    return 1;
}

GfxWorldVertex *__cdecl R_GetMarkMeshVerts(std::uint16_t baseVertex)
{
    return g_processMarkMesh && baseVertex < g_markMeshVertCount
        ? &g_markMeshVerts[baseVertex] : nullptr;
}

void __cdecl R_BeginMarkMeshVerts()
{
    iassert(!g_processMarkMesh);
    g_processMarkMesh = true;
    g_pendingMarkDrawBegin = g_pendingMarkDrawCount;
}

void __cdecl R_EndMarkMeshVerts()
{
    iassert(g_processMarkMesh);
    ConvertPendingMarkDraws(g_pendingMarkDrawBegin);
    g_processMarkMesh = false;
}

void __cdecl R_AddMarkMeshDrawSurf(Material *material,
    const GfxMarkContext *context, std::uint16_t *indices,
    std::uint32_t indexCount)
{
    if (!g_processMarkMesh || !material || !context || !indices ||
        indexCount == 0u || indexCount % 3u != 0u)
        return;
    if (!FindMarkTechnique(material))
    {
        R_WarnOncePerFrame(R_WARN_NONLIGHTMAP_MARK_MATERIAL);
        return;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(indices);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(
        g_markMeshIndices.data());
    const std::uintptr_t usedEnd = begin +
        static_cast<std::uintptr_t>(g_markMeshIndexCount) *
        sizeof(std::uint16_t);
    if (address < begin || address >= usedEnd ||
        (address - begin) % sizeof(std::uint16_t) != 0u)
        return;
    const std::uint32_t firstIndex = static_cast<std::uint32_t>(
        (address - begin) / sizeof(std::uint16_t));
    if (firstIndex > g_markMeshIndexCount ||
        indexCount > g_markMeshIndexCount - firstIndex)
        return;
    if (g_pendingMarkDrawCount >= g_pendingMarkDraws.size())
    {
        R_WarnOncePerFrame(R_WARN_GFX_MARK_MESH_LIMIT);
        return;
    }
    g_pendingMarkDraws[g_pendingMarkDrawCount++] = {
        material, *context, firstIndex, indexCount};
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
    WebRendererFog frameFog{};
    view.fogEnabled = WebRenderer_UpdateFrameFog(
        g_fogs, g_fogIndex, refdef->time, frameFog) &&
        (!r_fog || r_fog->current.enabled);
    view.fogStart = frameFog.fogStart;
    view.fogDensity = frameFog.density;
    constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;
    view.fogColor[0] = static_cast<float>(
        (frameFog.color >> 16u) & 0xffu) * BYTE_TO_UNIT;
    view.fogColor[1] = static_cast<float>(
        (frameFog.color >> 8u) & 0xffu) * BYTE_TO_UNIT;
    view.fogColor[2] = static_cast<float>(
        frameFog.color & 0xffu) * BYTE_TO_UNIT;
    view.fogColor[3] = static_cast<float>(
        (frameFog.color >> 24u) & 0xffu) * BYTE_TO_UNIT;

    WebRendererFilmSettings film{};
    if (r_filmUseTweaks && r_filmUseTweaks->current.enabled)
    {
        film.enabled = r_filmTweakEnable->current.enabled;
        film.brightness = r_filmTweakBrightness->current.value;
        film.contrast = r_filmTweakContrast->current.value;
        film.desaturation = r_filmTweakDesaturation->current.value;
        film.invert = r_filmTweakInvert->current.enabled;
        std::copy_n(r_filmTweakDarkTint->current.vector, 3u,
            film.tintDark);
        std::copy_n(r_filmTweakLightTint->current.vector, 3u,
            film.tintLight);
    }
    else
    {
        film.enabled = refdef->film.enabled;
        film.brightness = refdef->film.brightness;
        film.contrast = refdef->film.contrast;
        film.desaturation = refdef->film.desaturation;
        film.invert = refdef->film.invert;
        std::copy_n(refdef->film.tintDark, 3u, film.tintDark);
        std::copy_n(refdef->film.tintLight, 3u, film.tintLight);
    }
    const float globalDesaturation = r_desaturation
        ? r_desaturation->current.value : 1.0f;
    const float desaturationScale =
        (1.0f - film.desaturation) * globalDesaturation +
        film.desaturation;
    film.desaturation *= desaturationScale;
    film.contrast *= r_contrast ? r_contrast->current.value : 1.0f;
    film.brightness += r_brightness ? r_brightness->current.value : 0.0f;
    WebRendererColorManipulationConstants colorManipulation{};
    if (!WebRenderer_CalculateColorManipulationConstants(
            film, colorManipulation))
    {
        Com_Error(ERR_DROP,
            "R_RenderScene: invalid canonical film color constants");
        return;
    }
    std::copy_n(colorManipulation.colorBias, 4u, view.colorBias);
    std::copy_n(colorManipulation.colorTintBase, 4u,
        view.colorTintBase);
    std::copy_n(colorManipulation.colorTintDelta, 4u,
        view.colorTintDelta);
    view.filmEnabled = colorManipulation.enabled;
    view.blurRadius = refdef->blurRadius;
    if (!g_sceneBlurReported && refdef->blurRadius > 0.0f)
    {
        g_sceneBlurReported = true;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical resolved-scene blur active: "
            "radius=%.6f before 2D composition.\n",
            refdef->blurRadius);
    }
    const float displayGamma = r_gamma ? r_gamma->current.value : 0.8f;
    // R_SetColorMappings is gated by vidConfig.deviceSupportsGamma. The
    // browser registration deliberately reports false because a composited
    // WebGL canvas cannot install the D3D9 display LUT. Preserve r_gamma for
    // diagnostics/screenshots, but do not darken the scene in a shader when
    // the canonical device capability says the hardware ramp is unavailable.
    view.displayGammaExponent = cls.vidConfig.deviceSupportsGamma &&
            (!r_ignoreHwGamma || !r_ignoreHwGamma->current.enabled)
        ? 1.0f / std::max(displayGamma, 0.001f)
        : 1.0f;
    if (!g_visionLightingReported)
    {
        g_visionLightingReported = true;
        DispatchRendererVisionLighting(
            refdef->film.enabled, refdef->film.brightness,
            refdef->film.contrast, refdef->film.desaturation,
            refdef->film.invert, refdef->film.tintDark,
            refdef->film.tintLight, refdef->glow.enabled,
            refdef->glow.bloomCutoff,
            refdef->glow.bloomDesaturation,
            refdef->glow.bloomIntensity, refdef->glow.radius,
            r_gamma ? r_gamma->current.value : 0.8f);
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical vision lighting: film=%d "
            "brightness=%.6f contrast=%.6f desaturation=%.6f invert=%d "
            "dark=(%.6f %.6f %.6f) light=(%.6f %.6f %.6f); "
            "glow=%d cutoff=%.6f desaturation=%.6f intensity=%.6f "
            "radius=%.6f; blur=%.6f gamma=%.6f; fog=%d start=%.6f density=%.9f "
            "color=(%.6f %.6f %.6f %.6f).\n",
            refdef->film.enabled, refdef->film.brightness,
            refdef->film.contrast, refdef->film.desaturation,
            refdef->film.invert,
            refdef->film.tintDark[0], refdef->film.tintDark[1],
            refdef->film.tintDark[2], refdef->film.tintLight[0],
            refdef->film.tintLight[1], refdef->film.tintLight[2],
            refdef->glow.enabled, refdef->glow.bloomCutoff,
            refdef->glow.bloomDesaturation,
            refdef->glow.bloomIntensity, refdef->glow.radius,
            refdef->blurRadius,
            r_gamma ? r_gamma->current.value : 0.8f,
            view.fogEnabled, view.fogStart, view.fogDensity,
            view.fogColor[0], view.fogColor[1], view.fogColor[2],
            view.fogColor[3]);
    }
    view.localClientNum = refdef->localClientNum;
    view.worldName = s_world.name;
    view.sunShadowEnabled = sm_enable && sm_enable->current.enabled &&
        s_world.sunLight && s_world.sunLight->type == 1u;
    if (view.sunShadowEnabled)
    {
        std::copy_n(s_world.sunLight->dir, 3u, view.sunDirection);
        std::copy_n(s_world.sunLight->color, 3u, view.sunColor);
        std::copy_n(s_world.mins, 3u, view.worldMins);
        std::copy_n(s_world.maxs, 3u, view.worldMaxs);
    }

    mat4x4 viewMatrix{};
    mat4x4 projectionMatrix{};
    mat4x4 depthHackProjectionMatrix{};
    mat4x4 d3dViewProjectionMatrix{};
    mat4x4 d3dDepthHackViewProjectionMatrix{};
    mat4x4 depthRangeConversion{};
    mat4x4 webglViewProjectionMatrix{};
    mat4x4 webglDepthHackViewProjectionMatrix{};
    MatrixForViewer(viewMatrix, view.viewOrigin, view.viewAxis);
    InfinitePerspectiveMatrix(
        projectionMatrix, view.tanHalfFovX, view.tanHalfFovY, view.zNear);
    InfinitePerspectiveMatrix(depthHackProjectionMatrix,
        view.tanHalfFovX, view.tanHalfFovY,
        std::max(0.01f,
            r_znear_depthhack ? r_znear_depthhack->current.value : 0.1f));
    MatrixMultiply44(
        viewMatrix, projectionMatrix, d3dViewProjectionMatrix);
    MatrixMultiply44(viewMatrix, depthHackProjectionMatrix,
        d3dDepthHackViewProjectionMatrix);
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
    MatrixMultiply44(d3dDepthHackViewProjectionMatrix,
        depthRangeConversion, webglDepthHackViewProjectionMatrix);
    std::memcpy(view.viewProjectionMatrix, webglViewProjectionMatrix,
        sizeof(view.viewProjectionMatrix));
    std::memcpy(view.depthHackViewProjectionMatrix,
        webglDepthHackViewProjectionMatrix,
        sizeof(view.depthHackViewProjectionMatrix));

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
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical world draw ranges: lit=[%u,%u), "
            "decal=[%u,%u), emissive=[%u,%u), model-no-decal=%u.\n",
            s_world.dpvs.litSurfsBegin, s_world.dpvs.litSurfsEnd,
            s_world.dpvs.decalSurfsBegin, s_world.dpvs.decalSurfsEnd,
            s_world.dpvs.emissiveSurfsBegin, s_world.dpvs.emissiveSurfsEnd,
            s_world.modelCount > 0
                ? s_world.models[0].surfaceCountNoDecal : 0u);
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
            if (!BuildRendererPrimaryLights(command))
            {
                Com_Error(ERR_DROP,
                    "R_RenderScene: invalid canonical primary lights");
                return;
            }
            for (WebRendererWorldBatchDesc &batch : command.batches)
                ResolveRendererBatchImages(batch);
            std::array<std::uint32_t, WEB_RENDERER_MAX_PRIMARY_LIGHTS>
                primaryLightSurfaceCounts{};
            for (const WebRendererWorldBatchDesc &batch : command.batches)
            {
                if (batch.primaryLightIndex <
                    primaryLightSurfaceCounts.size())
                {
                    primaryLightSurfaceCounts[batch.primaryLightIndex] +=
                        batch.surfaceCount;
                }
            }
            std::uint32_t spotLightCount = 0u;
            std::uint32_t omniLightCount = 0u;
            std::uint32_t resolvedAttenuationCount = 0u;
            std::uint32_t assignedLocalLightCount = 0u;
            std::uint32_t assignedLocalSurfaceCount = 0u;
            for (std::uint32_t index = 0u;
                 index < command.primaryLights.size(); ++index)
            {
                const WebRendererPrimaryLightDesc &light =
                    command.primaryLights[index];
                if (light.type == 2u) ++spotLightCount;
                if (light.type == 3u) ++omniLightCount;
                if (light.attenuationImage) ++resolvedAttenuationCount;
                if ((light.type == 2u || light.type == 3u) &&
                    index != command.sunPrimaryLightIndex &&
                    primaryLightSurfaceCounts[index] != 0u)
                {
                    ++assignedLocalLightCount;
                    assignedLocalSurfaceCount +=
                        primaryLightSurfaceCounts[index];
                    Web_Log(WebLogLevel::Info,
                        "[kisakcod-web] Canonical primary light %u: "
                        "type=%u surfaces=%u origin=(%.3f %.3f %.3f) "
                        "radius=%.3f color=(%.3f %.3f %.3f) "
                        "attenuation='%s'.\n",
                        index, static_cast<unsigned int>(light.type),
                        primaryLightSurfaceCounts[index],
                        light.origin[0], light.origin[1], light.origin[2],
                        light.radius, light.color[0], light.color[1],
                        light.color[2],
                        light.attenuationImage &&
                                light.attenuationImage->name
                            ? light.attenuationImage->name : "<none>");
                }
            }
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical primary-light inventory: "
                "%zu total, sun=%u, spot=%u, omni=%u, "
                "%u attenuation images, %u local lights assigned to "
                "%u world surfaces.\n",
                command.primaryLights.size(),
                command.sunPrimaryLightIndex, spotLightCount,
                omniLightCount, resolvedAttenuationCount,
                assignedLocalLightCount, assignedLocalSurfaceCount);
            const WebRendererWorldSurfaceDesc surface{
                command.vertices.data(),
                static_cast<std::uint32_t>(command.vertices.size()),
                command.indices.data(),
                static_cast<std::uint32_t>(command.indices.size()),
                command.batches.data(),
                static_cast<std::uint32_t>(command.batches.size()),
                nullptr,
                command.primaryLights.data(),
                static_cast<std::uint32_t>(command.primaryLights.size()),
                command.sunPrimaryLightIndex,
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
            const WebRendererTextureResult skySubmission =
                WebRenderer_SetSkyImage(
                    s_world.skySurfCount > 0 ? s_world.skyImage : nullptr,
                    s_world.skySamplerState);
            if (skySubmission != WebRendererTextureResult::Success)
            {
                Web_Log(WebLogLevel::Error,
                    "[kisakcod-web] Canonical sky submission failed: %s.\n",
                    WebRenderer_TextureResultString(skySubmission));
            }
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical special-surface inventory: "
                "water=%u surfaces/%u materials, resolved-scene=%u, "
                "resolved-post-sun=%u.\n",
                command.waterSurfaceCount, command.waterMaterialCount,
                command.resolvedSceneSurfaceCount,
                command.resolvedPostSunSurfaceCount);
            std::vector<const Material *> reportedWaterMaterials;
            for (const WebRendererWorldBatchDesc &batch : command.batches)
            {
                const Material *material = batch.materialIdentity;
                if (!material || !material->textureTable ||
                    std::find(reportedWaterMaterials.begin(),
                        reportedWaterMaterials.end(), material) !=
                        reportedWaterMaterials.end())
                {
                    continue;
                }
                const water_t *water = nullptr;
                std::uint8_t waterSampler = 0u;
                for (std::uint32_t textureIndex = 0u;
                    textureIndex < material->textureCount; ++textureIndex)
                {
                    const MaterialTextureDef &texture =
                        material->textureTable[textureIndex];
                    if (texture.semantic == 11u && texture.u.water)
                    {
                        water = texture.u.water;
                        waterSampler = texture.samplerState;
                        break;
                    }
                }
                if (!water) continue;
                reportedWaterMaterials.push_back(material);
                Web_Log(WebLogLevel::Info,
                    "[kisakcod-web] Canonical water material '%s': "
                    "technique='%s' shader='%s' type=%u sampler=0x%02x "
                    "grid=%dx%d world=(%.3f %.3f) gravity=%.3f "
                    "wind=%.3f dir=(%.3f %.3f) amplitude=%.6f "
                    "code=(%.6f %.6f %.6f %.6f) image='%s'.\n",
                    batch.materialName ? batch.materialName : "<unnamed>",
                    batch.techniqueName ? batch.techniqueName : "<none>",
                    batch.pixelShaderName ? batch.pixelShaderName : "<none>",
                    batch.techniqueType, waterSampler, water->M, water->N,
                    water->Lx, water->Lz, water->gravity, water->windvel,
                    water->winddir[0], water->winddir[1], water->amplitude,
                    water->codeConstant[0], water->codeConstant[1],
                    water->codeConstant[2], water->codeConstant[3],
                    water->image && water->image->name
                        ? water->image->name : "<none>");
                for (std::uint32_t textureIndex = 0u;
                    textureIndex < material->textureCount; ++textureIndex)
                {
                    const MaterialTextureDef &texture =
                        material->textureTable[textureIndex];
                    const GfxImage *image = texture.semantic == 11u
                        ? (texture.u.water ? texture.u.water->image : nullptr)
                        : texture.u.image;
                    Web_Log(WebLogLevel::Info,
                        "[kisakcod-web] Canonical water texture[%u]: "
                        "hash=0x%08x name='%c%c' semantic=%u sampler=0x%02x "
                        "image='%s' map=%u size=%ux%u.\n",
                        textureIndex, texture.nameHash,
                        texture.nameStart ? texture.nameStart : '?',
                        texture.nameEnd ? texture.nameEnd : '?',
                        texture.semantic, texture.samplerState,
                        image && image->name ? image->name : "<none>",
                        image ? image->mapType : 0u,
                        image ? image->width : 0u,
                        image ? image->height : 0u);
                }
                for (std::uint32_t constantIndex = 0u;
                    constantIndex < material->constantCount; ++constantIndex)
                {
                    const MaterialConstantDef &constant =
                        material->constantTable[constantIndex];
                    Web_Log(WebLogLevel::Info,
                        "[kisakcod-web] Canonical water constant[%u]: "
                        "hash=0x%08x name='%.12s' value=(%.6f %.6f %.6f %.6f).\n",
                        constantIndex, constant.nameHash, constant.name,
                        constant.literal[0], constant.literal[1],
                        constant.literal[2], constant.literal[3]);
                }
                const MaterialTechniqueSet *techniqueSet =
                    material->techniqueSet &&
                        material->techniqueSet->remappedTechniqueSet
                    ? material->techniqueSet->remappedTechniqueSet
                    : material->techniqueSet;
                const MaterialTechnique *technique = techniqueSet &&
                        batch.techniqueType < 34u
                    ? techniqueSet->techniques[batch.techniqueType] : nullptr;
                if (technique && technique->passCount > 0u)
                {
                    const MaterialPass &pass = technique->passArray[0u];
                    const std::uint32_t argumentCount =
                        static_cast<std::uint32_t>(pass.perPrimArgCount) +
                        pass.perObjArgCount + pass.stableArgCount;
                    Web_Log(WebLogLevel::Info,
                        "[kisakcod-web] Canonical water pass: args=%u "
                        "perPrim=%u perObj=%u stable=%u customSamplers=0x%02x "
                        "vertexShader='%s' vertexDwords=%u pixelDwords=%u.\n",
                        argumentCount, pass.perPrimArgCount,
                        pass.perObjArgCount, pass.stableArgCount,
                        pass.customSamplerFlags,
                        pass.vertexShader && pass.vertexShader->name
                            ? pass.vertexShader->name : "<none>",
                        pass.vertexShader
                            ? pass.vertexShader->prog.loadDef.programSize : 0u,
                        pass.pixelShader
                            ? pass.pixelShader->prog.loadDef.programSize : 0u);
                    for (std::uint32_t argumentIndex = 0u;
                        argumentIndex < argumentCount && pass.args;
                        ++argumentIndex)
                    {
                        const MaterialShaderArgument &argument =
                            pass.args[argumentIndex];
                        Web_Log(WebLogLevel::Info,
                            "[kisakcod-web] Canonical water arg[%u]: "
                            "type=%u dest=%u payload=0x%08x.\n",
                            argumentIndex, argument.type, argument.dest,
                            argument.u.nameHash);
                    }
                }
            }
            std::vector<const Material *> unsupportedMaterials;
            for (const WebRendererWorldBatchDesc &batch : command.batches)
            {
                if (batch.technique !=
                        WebRendererWorldTechnique::BackendFallback ||
                    !batch.materialIdentity ||
                    std::find(unsupportedMaterials.begin(),
                        unsupportedMaterials.end(), batch.materialIdentity) !=
                        unsupportedMaterials.end())
                {
                    continue;
                }
                unsupportedMaterials.push_back(batch.materialIdentity);
                const Material *material = batch.materialIdentity;
                const MaterialTechniqueSet *direct = material->techniqueSet;
                const MaterialTechniqueSet *remapped = direct
                    ? direct->remappedTechniqueSet : nullptr;
                std::uint64_t directMask = 0u;
                std::uint64_t remappedMask = 0u;
                for (std::uint32_t type = 0u; type < 34u; ++type)
                {
                    if (direct && direct->techniques[type])
                        directMask |= std::uint64_t{1u} << type;
                    if (remapped && remapped->techniques[type])
                        remappedMask |= std::uint64_t{1u} << type;
                }
                Web_Log(WebLogLevel::Info,
                    "[kisakcod-web] Unsupported world material '%s': "
                    "sort=%u stateCount=%u entries(unlit=%u emissive=%u "
                    "lit=%u) techniqueMasks(direct=%08x remapped=%08x), "
                    "techniqueSet='%s', remapped='%s'.\n",
                    material->info.name ? material->info.name : "<unnamed>",
                    material->info.sortKey, material->stateBitsCount,
                    material->stateBitsEntry[4],
                    material->stateBitsEntry[5],
                    material->stateBitsEntry[7],
                    static_cast<std::uint32_t>(directMask),
                    static_cast<std::uint32_t>(remappedMask),
                    direct && direct->name ? direct->name : "<none>",
                    remapped && remapped->name ? remapped->name : "<none>");
            }
            g_worldSceneSurfaceCount = command.surfaceCount;
            g_worldSceneVertexCount =
                static_cast<std::uint32_t>(command.vertices.size());
            g_worldSceneIndexCount =
                static_cast<std::uint32_t>(command.indices.size());
            g_worldSceneSubmitted = true;
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Renderer frontend submitted %u canonical world "
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
            WebRenderer_BuildStaticModelSceneCommand(
                s_world, command, &MODEL_LIGHTING_CALLBACKS,
                ResolveRendererMaterial);
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
            for (WebRendererStaticModelBatchDesc &batch : command.batches)
                ResolveRendererBatchImages(batch.draw);
            const WebRendererModelLightingAtlasDesc lightingAtlas{
                command.modelLightingAtlas.pixels.data(),
                command.modelLightingAtlas.width,
                command.modelLightingAtlas.height,
                command.modelLightingAtlas.depth,
                command.modelLightingAtlas.entryCount,
                command.modelLightingAtlas.pixels.size(),
            };
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
                command.modelLightingAtlas.pixels.empty()
                    ? nullptr : &lightingAtlas,
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
                "%zu indices, %zu instances; model-lighting source=%s, "
                "failures=%u).\n",
                command.modelCount,
                command.batches.size(),
                command.vertices.size(),
                command.indices.size(),
                command.instances.size(),
                command.modelLightingSourceAvailable ? "yes" : "no",
                command.modelLightingFailureCount);
        }
    }

    WebRendererDObjSceneCommand dynamicCommand;
    const WebRendererDObjSceneResult dynamicBuild =
        WebRenderer_BuildDObjSceneCommand(
            g_dobjSubmissions.data(), g_dobjSubmissionCount,
            dynamicCommand, refdef->vieworg, &s_world.lightGrid,
            &MODEL_LIGHTING_CALLBACKS, ResolveRendererMaterial);
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

    // Native R_RenderScene advances the EffectsCore and DynEntity physics
    // worlds before it consumes their current poses for scene submission.
    // Keep that runtime ownership and ordering at the browser frontend seam.
    FX_RunPhysics(refdef->localClientNum);
    DynEntCl_ProcessEntities(refdef->localClientNum);

    std::vector<WebRendererBrushModelSubmission> activeBrushModels;
    try
    {
        activeBrushModels.assign(g_brushModelSubmissions.begin(),
            g_brushModelSubmissions.begin() + g_brushModelSubmissionCount);
        const std::uint16_t dynamicBrushCount =
            DynEnt_GetEntityCount(DYNENT_COLL_CLIENT_BRUSH);
        activeBrushModels.reserve(activeBrushModels.size() +
            dynamicBrushCount);
        for (std::uint32_t dynEntId = 0u;
             dynEntId < dynamicBrushCount; ++dynEntId)
        {
            const DynEntityClient *client = DynEnt_GetClientEntity(
                static_cast<std::uint16_t>(dynEntId), DYNENT_DRAW_BRUSH);
            const DynEntityDef *definition = DynEnt_GetEntityDef(
                static_cast<std::uint16_t>(dynEntId), DYNENT_DRAW_BRUSH);
            DynEntityPose *pose = DynEnt_GetClientPose(
                static_cast<std::uint16_t>(dynEntId), DYNENT_DRAW_BRUSH);
            if (!client || !definition || !pose ||
                (client->flags & DYNENT_CL_VISIBLE) == 0u ||
                definition->brushModel >= s_world.modelCount ||
                activeBrushModels.size() >=
                    WEB_RENDERER_MAX_DYNAMIC_BMODEL_SUBMISSIONS)
            {
                continue;
            }
            WebRendererBrushModelSubmission submission{};
            submission.model = &s_world.models[definition->brushModel];
            submission.entityNumber = static_cast<std::uint16_t>(dynEntId);
            std::copy_n(pose->pose.origin, 3u, submission.origin);
            UnitQuatToAxis(pose->pose.quat, submission.axis);
            activeBrushModels.push_back(submission);
        }
    }
    catch (const std::bad_alloc &)
    {
        Com_Error(ERR_DROP,
            "R_RenderScene canonical brush collection allocation failed");
        return;
    }

    WebRendererBrushModelSceneCommand brushCommand;
    const WebRendererWorldSceneResult brushBuild =
        WebRenderer_BuildBrushModelSceneCommand(
            s_world, activeBrushModels.data(),
            static_cast<std::uint32_t>(activeBrushModels.size()),
            brushCommand);
    const bool hasBrushModels =
        brushBuild == WebRendererWorldSceneResult::Success;
    if (brushBuild != WebRendererWorldSceneResult::Success &&
        brushBuild != WebRendererWorldSceneResult::NoVisibleSurface)
    {
        Com_Error(ERR_DROP, "R_RenderScene dynamic brush model: %s",
            WebRenderer_WorldSceneResultString(brushBuild));
        return;
    }
    if (hasBrushModels)
    {
        try
        {
            const std::uint32_t vertexBase = static_cast<std::uint32_t>(
                dynamicCommand.vertices.size());
            const std::uint32_t indexBase = static_cast<std::uint32_t>(
                dynamicCommand.indices.size());
            if (vertexBase > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                    brushCommand.vertices.size() ||
                indexBase > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
                    brushCommand.indices.size())
            {
                Com_Error(ERR_DROP,
                    "R_RenderScene canonical brush models exceed dynamic limits");
                return;
            }
            dynamicCommand.vertices.insert(dynamicCommand.vertices.end(),
                brushCommand.vertices.begin(), brushCommand.vertices.end());
            for (const std::uint32_t index : brushCommand.indices)
                dynamicCommand.indices.push_back(vertexBase + index);
            for (WebRendererWorldBatchDesc batch : brushCommand.batches)
            {
                batch.firstIndex += indexBase;
                dynamicCommand.batches.push_back(batch);
            }
            dynamicCommand.surfaceCount += brushCommand.surfaceCount;
        }
        catch (const std::bad_alloc &)
        {
            Com_Error(ERR_DROP,
                "R_RenderScene canonical brush model allocation failed");
            return;
        }
    }

    std::vector<WebRendererFxModelSubmission> dynamicEntityModels;
    std::vector<std::uint16_t> dynamicEntityModelIds;
    try
    {
        const std::uint16_t dynamicModelCount =
            DynEnt_GetEntityCount(DYNENT_COLL_CLIENT_MODEL);
        dynamicEntityModels.reserve(dynamicModelCount);
        dynamicEntityModelIds.reserve(dynamicModelCount);
        for (std::uint32_t dynEntId = 0u;
             dynEntId < dynamicModelCount; ++dynEntId)
        {
            const DynEntityClient *client = DynEnt_GetClientEntity(
                static_cast<std::uint16_t>(dynEntId), DYNENT_DRAW_MODEL);
            const DynEntityDef *definition = DynEnt_GetEntityDef(
                static_cast<std::uint16_t>(dynEntId), DYNENT_DRAW_MODEL);
            DynEntityPose *pose = DynEnt_GetClientPose(
                static_cast<std::uint16_t>(dynEntId), DYNENT_DRAW_MODEL);
            if (!client || !definition || !pose || !definition->xModel ||
                (client->flags & DYNENT_CL_VISIBLE) == 0u)
            {
                continue;
            }
            WebRendererFxModelSubmission submission{};
            submission.model = definition->xModel;
            submission.placement.base = pose->pose;
            submission.placement.scale = 1.0f;
            submission.sourceKind =
                WebRendererSceneBatchKind::DynamicXModel;
            const int lod = WebRenderer_SelectFxModelLod(
                submission.model, submission.placement, refdef->vieworg);
            if (lod < 0) continue;
            submission.lod = static_cast<std::uint16_t>(lod);
            dynamicEntityModels.push_back(submission);
            dynamicEntityModelIds.push_back(
                static_cast<std::uint16_t>(dynEntId));
        }
    }
    catch (const std::bad_alloc &)
    {
        Com_Error(ERR_DROP,
            "R_RenderScene canonical DynEntity model allocation failed");
        return;
    }

    if (!dynamicEntityModels.empty())
    {
        const std::uint32_t previousLightingEntries =
            dynamicCommand.modelLightingAtlas.entryCount;
        const std::uint32_t totalLightingEntries = previousLightingEntries +
            static_cast<std::uint32_t>(dynamicEntityModels.size());
        WebRendererModelLightingAtlas combinedLighting;
        bool combinedLightingReady =
            totalLightingEntries >= previousLightingEntries &&
            WebRenderer_InitializeModelLightingAtlas(
                totalLightingEntries, combinedLighting);
        if (combinedLightingReady && previousLightingEntries != 0u)
        {
            combinedLightingReady =
                WebRenderer_CopyModelLightingAtlasEntries(
                    dynamicCommand.modelLightingAtlas,
                    combinedLighting, 0u);
        }
        if (combinedLightingReady)
        {
            if (previousLightingEntries != 0u)
            {
                const WebRendererModelLightingAtlas &previous =
                    dynamicCommand.modelLightingAtlas;
                for (WebRendererWorldBatchDesc &batch :
                     dynamicCommand.batches)
                {
                    if (batch.lightingMode !=
                            WebRendererWorldLightingMode::ModelLightGrid)
                        continue;
                    const int centerX = static_cast<int>(std::lround(
                        batch.modelLightingCoordinates[0] * previous.width));
                    const int centerY = static_cast<int>(std::lround(
                        batch.modelLightingCoordinates[1] * previous.height));
                    if (centerX < 2 || centerY < 2) continue;
                    const std::uint32_t entry =
                        static_cast<std::uint32_t>((centerY - 2) / 4) * 64u +
                        static_cast<std::uint32_t>((centerX - 2) / 4);
                    WebRenderer_GetModelLightingCoordinates(
                        combinedLighting, entry,
                        batch.modelLightingCoordinates);
                }
            }
            for (std::uint32_t index = 0u;
                 index < dynamicEntityModels.size(); ++index)
            {
                const std::uint16_t dynEntId =
                    dynamicEntityModelIds[index];
                const std::uint32_t nonSunPrimaryLight =
                    s_world.nonSunPrimaryLightForModelDynEnt &&
                        dynEntId < DynEnt_GetEntityCount(
                            DYNENT_COLL_CLIENT_MODEL)
                    ? s_world.nonSunPrimaryLightForModelDynEnt[dynEntId]
                    : 0u;
                WebRendererModelLightingSample sample{};
                const std::uint32_t lightingEntry =
                    previousLightingEntries + index;
                WebRendererFxModelSubmission &submission =
                    dynamicEntityModels[index];
                submission.modelLightingEnabled =
                    WebRenderer_EvaluateModelLighting(
                        s_world.lightGrid,
                        submission.placement.base.origin,
                        nonSunPrimaryLight,
                        &MODEL_LIGHTING_CALLBACKS, sample) &&
                    WebRenderer_SetModelLightingAtlasEntry(
                        combinedLighting, lightingEntry, sample.colors,
                        sample.primaryLightWeight);
                if (submission.modelLightingEnabled)
                {
                    WebRenderer_GetModelLightingCoordinates(
                        combinedLighting, lightingEntry,
                        submission.modelLightingCoordinates);
                }
            }
            dynamicCommand.modelLightingAtlas =
                std::move(combinedLighting);
        }
    }

    WebRendererFxModelSceneCommand dynamicEntityModelCommand;
    std::uint32_t droppedDynamicEntityModels = 0u;
    const WebRendererFxModelSceneResult dynamicEntityModelBuild =
        WebRenderer_BuildFxModelSceneCommand(
            dynamicEntityModels.data(),
            static_cast<std::uint32_t>(dynamicEntityModels.size()),
            dynamicEntityModelCommand, &droppedDynamicEntityModels,
            ResolveRendererMaterial);
    const bool hasDynamicEntityModels = dynamicEntityModelBuild ==
        WebRendererFxModelSceneResult::Success;
    if (dynamicEntityModelBuild != WebRendererFxModelSceneResult::Success &&
        dynamicEntityModelBuild != WebRendererFxModelSceneResult::NoFxModel)
    {
        Com_Error(ERR_DROP, "R_RenderScene DynEntity model: %s",
            WebRenderer_FxModelSceneResultString(dynamicEntityModelBuild));
        return;
    }
    if (hasDynamicEntityModels)
    {
        const WebRendererFxModelAppendResult append =
            WebRenderer_AppendFxModelSceneCommand(
                dynamicEntityModelCommand,
                dynamicCommand.vertices,
                dynamicCommand.indices,
                dynamicCommand.batches,
                dynamicCommand.surfaceCount);
        if (append != WebRendererFxModelAppendResult::Success)
        {
            Com_Error(ERR_DROP,
                "R_RenderScene DynEntity model append failed");
            return;
        }
    }

    // The exact native R_SkinXModel LOD ramp depends on renderer-global FOV
    // dvars that are not compiled into this frontend. Select the same model
    // thresholds from active refdef distance, scaled by placement, as the
    // deterministic rigid FX compatibility subset.
    std::uint32_t selectedFxModels = 0u;
    for (std::uint32_t index = 0u; index < g_fxModelSubmissionCount; ++index)
    {
        WebRendererFxModelSubmission submission = g_fxModelSubmissions[index];
        const int lod = WebRenderer_SelectFxModelLod(
            submission.model, submission.placement, refdef->vieworg);
        if (lod < 0)
        {
            // XModelGetLodForDist returns -1 when a model has passed its
            // farthest configured LOD. This is ordinary distance culling
            // (not an unknown shader), notably for ejected shell effects.
            continue;
        }
        submission.lod = static_cast<std::uint16_t>(lod);
        g_fxModelSubmissions[selectedFxModels++] = submission;
    }
    g_fxModelSubmissionCount = selectedFxModels;

    WebRendererFxModelSceneCommand fxModelCommand;
    std::uint32_t droppedFxModels = 0u;
    WebRendererFxModelSceneResult fxModelBuild =
        WebRenderer_BuildFxModelSceneCommand(
            g_fxModelSubmissions.data(), g_fxModelSubmissionCount,
            fxModelCommand, &droppedFxModels, ResolveRendererMaterial);
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
    // already-converted canonical mark/code-mesh spans to the existing
    // dynamic command so the backend retains one ordered, copied submission.
    const bool hasMarkMesh = !g_markMeshRenderBatches.empty();
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
                g_markMeshRenderVertices.size() +
                    g_codeMeshRenderVertices.size(),
                g_markMeshRenderIndices.size() +
                    g_codeMeshRenderIndices.size(),
                g_markMeshRenderBatches.size() +
                    g_codeMeshRenderBatches.size(),
                static_cast<std::uint32_t>(
                    g_markMeshRenderBatches.size() +
                    g_codeMeshRenderBatches.size()));
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
    if (hasMarkMesh)
    {
        try
        {
            const std::uint32_t vertexBase = static_cast<std::uint32_t>(
                dynamicCommand.vertices.size());
            const std::uint32_t indexBase = static_cast<std::uint32_t>(
                dynamicCommand.indices.size());
            if (vertexBase > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                    g_markMeshRenderVertices.size() ||
                indexBase > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
                    g_markMeshRenderIndices.size())
            {
                Com_Error(ERR_DROP,
                    "R_RenderScene canonical mark mesh exceeds dynamic limits");
                return;
            }
            dynamicCommand.vertices.insert(dynamicCommand.vertices.end(),
                g_markMeshRenderVertices.begin(),
                g_markMeshRenderVertices.end());
            for (const std::uint32_t index : g_markMeshRenderIndices)
                dynamicCommand.indices.push_back(vertexBase + index);
            for (WebRendererWorldBatchDesc batch : g_markMeshRenderBatches)
            {
                batch.firstIndex += indexBase;
                dynamicCommand.batches.push_back(batch);
            }
            dynamicCommand.surfaceCount += static_cast<std::uint32_t>(
                g_markMeshRenderBatches.size());
        }
        catch (const std::bad_alloc &)
        {
            Com_Error(ERR_DROP,
                "R_RenderScene canonical mark mesh allocation failed");
            return;
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
        hasBrushModels ||
        hasDynamicEntityModels ||
        hasFxModel || hasMarkMesh || hasCodeMesh || hasParticleCloud)
    {
        for (WebRendererWorldBatchDesc &batch : dynamicCommand.batches)
            ResolveRendererBatchImages(batch);
        const WebRendererModelLightingAtlasDesc lightingAtlas{
            dynamicCommand.modelLightingAtlas.pixels.data(),
            dynamicCommand.modelLightingAtlas.width,
            dynamicCommand.modelLightingAtlas.height,
            dynamicCommand.modelLightingAtlas.depth,
            dynamicCommand.modelLightingAtlas.entryCount,
            dynamicCommand.modelLightingAtlas.pixels.size(),
        };
        const WebRendererWorldSurfaceDesc scene{
            dynamicCommand.vertices.data(),
            static_cast<std::uint32_t>(dynamicCommand.vertices.size()),
            dynamicCommand.indices.data(),
            static_cast<std::uint32_t>(dynamicCommand.indices.size()),
            dynamicCommand.batches.data(),
            static_cast<std::uint32_t>(dynamicCommand.batches.size()),
            dynamicCommand.modelLightingAtlas.pixels.empty()
                ? nullptr : &lightingAtlas,
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
    if (hasBrushModels && !g_dynamicBrushSceneReported)
    {
        g_dynamicBrushSceneReported = true;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical dynamic brush scene: %u models, "
            "%u surfaces, %zu batches.\n",
            brushCommand.modelCount, brushCommand.surfaceCount,
            brushCommand.batches.size());
    }
    if (hasDynamicEntityModels && !g_dynamicEntityModelSceneReported)
    {
        g_dynamicEntityModelSceneReported = true;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical DynEntity model scene: %u models, "
            "%u surfaces, %zu batches, dropped=%u, first='%s'.\n",
            dynamicEntityModelCommand.modelCount,
            dynamicEntityModelCommand.surfaceCount,
            dynamicEntityModelCommand.batches.size(),
            droppedDynamicEntityModels,
            dynamicEntityModelCommand.batches.empty()
                ? "<none>"
                : dynamicEntityModelCommand.batches[0].modelName);
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
    const GfxBrushModel *model, const float *origin, const float *angles,
    std::uint16_t entityNumber)
{
    if (!model || !origin || !angles || model->surfaceCount == 0u ||
        g_brushModelSubmissionCount >= g_brushModelSubmissions.size())
    {
        if (model && model->surfaceCount != 0u &&
            g_brushModelSubmissionCount >= g_brushModelSubmissions.size())
        {
            R_WarnOncePerFrame(R_WARN_MAX_SCENE_BRUSH_REFS,
                static_cast<unsigned int>(g_brushModelSubmissions.size()));
        }
        return;
    }
    WebRendererBrushModelSubmission &submission =
        g_brushModelSubmissions[g_brushModelSubmissionCount++];
    submission.model = model;
    submission.entityNumber = entityNumber;
    std::copy_n(origin, 3u, submission.origin);
    AnglesToAxis(angles, submission.axis);
}
void __cdecl R_AddDObjToScene(const DObj_s *obj, const cpose_t *pose,
    std::uint32_t entityNumber, std::uint32_t renderFlags,
    float *lightingOrigin, float)
{
    // Keep the callback identity and pose intact until R_RenderScene consumes
    // it synchronously. Native GfxScene admits ordinary and first-person
    // DObjs through the same fixed 512-entry sceneDObj array.
    WebRendererDObjSubmission submission{};
    submission.obj = obj;
    submission.pose = pose;
    submission.entityNumber = entityNumber;
    submission.renderFlags = renderFlags;
    const float *sourceLightingOrigin = lightingOrigin
        ? lightingOrigin : (pose ? pose->origin : vec3_origin);
    std::copy_n(sourceLightingOrigin, 3u, submission.lightingOrigin);
    const WebRendererDObjAdmissionResult admission =
        WebRenderer_ValidateDObjSubmission(
            submission, g_dobjSubmissionCount,
            static_cast<std::uint32_t>(g_dobjSubmissions.size()));
    if (admission == WebRendererDObjAdmissionResult::InvalidSubmission)
        return;
    if (admission == WebRendererDObjAdmissionResult::LimitReached)
    {
        R_WarnOncePerFrame(R_WARN_MAX_SCENE_DOBJ_REFS,
            static_cast<unsigned int>(g_dobjSubmissions.size()));
        return;
    }
    g_dobjSubmissions[g_dobjSubmissionCount++] = submission;
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
    FX_GenerateMarkVertsForWorld(cmd->localClientNum);
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
