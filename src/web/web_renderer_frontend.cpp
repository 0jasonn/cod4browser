// Browser renderer frontend closure for the canonical SP runtime. This owns
// presentation and scene APIs at the platform boundary; the DB-owned world
// remains s_world and the existing bounded WebGL2 adapter remains the only
// world conversion/submission path.

#include <client/client.h>
#include <cgame/cg_local.h>
#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
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
#include <gfx_d3d/r_dpvs_core.h>
#include <gfx_d3d/r_dynamiclights_core.h>
#include <gfx_d3d/r_effects_api.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_gamma.h>
#include <gfx_d3d/r_image_quality.h>
#include <gfx_d3d/r_material_override_core.h>
#include <gfx_d3d/r_text.h>
#include <gfx_d3d/r_rendercmds.h>
#include <gfx_d3d/r_primarylights_core.h>
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
#include <ui/ui.h>
#include <web/web_renderer.h>
#include <web/web_display.h>
#include <web/web_frame_profile.h>
#include <limits>
#include <web/web_renderer_image_reference.h>
#include <web/web_renderer_code_mesh.h>
#include <web/web_renderer_dobj_scene.h>
#include <web/web_renderer_fx_model_scene.h>
#include <web/web_renderer_lighting.h>
#include <web/web_renderer_mark_mesh.h>
#include <web/web_renderer_particle_cloud_scene.h>
#include <web/web_renderer_static_model_scene.h>
#include <web/web_renderer_world_scene.h>
#include <web/web_client_server_lifecycle.h>
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
#include <utility>
#include <vector>

enum CubemapShot : int;

extern GfxWorld s_world;

namespace
{
cmd_function_s g_applyPicmipCommand{};
bool g_applyPicmipCommandRegistered = false;

void ApplyPicmipForBrowser()
{
    // Native refreshes every registered material image in place. The browser
    // backend retains bounded decoded recovery sources instead, so reuse the
    // established renderer restart path to rebuild them at the newly selected
    // authored mip levels. SP vid_restart preserves the running game through
    // its canonical temporary-save path.
    Cbuf_AddText(0, "vid_restart\n");
}

#if KISAK_WEB_DIAGNOSTICS
double g_frontendProfileStarted = 0.0;
std::array<std::uint32_t, 256> g_testUiTextHashes{};
std::size_t g_testUiTextHashCursor = 0;

std::uint32_t HashDiagnosticText(const char *text)
{
    std::uint32_t hash = 2166136261u;
    while (*text)
    {
        char character = *text++;
        if (character >= 'A' && character <= 'Z')
            character += 'a' - 'A';
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619u;
    }
    return hash;
}
#endif
bool DynamicShadowVisibleToPrimaryLight(
    WebRendererShadowEntityKind kind, std::uint32_t entityId,
    std::uint32_t localClientNum,
    std::uint32_t primaryLightIndex) noexcept
{
    using namespace kisak::primary_lights;
    switch (kind)
    {
    case WebRendererShadowEntityKind::SceneEntity:
    {
        if (!EntityVisibilityAvailable(s_world, primaryLightIndex))
            return true;
        return IsEntityVisible(s_world, Web_RendererEntityCount(),
            localClientNum, entityId, primaryLightIndex);
    }
    case WebRendererShadowEntityKind::DynEntModel:
    case WebRendererShadowEntityKind::DynEntBrush:
    {
        const std::uint32_t drawType =
            kind == WebRendererShadowEntityKind::DynEntModel
            ? DYNENT_DRAW_MODEL : DYNENT_DRAW_BRUSH;
        if (!DynEntVisibilityAvailable(
                s_world, drawType, primaryLightIndex))
            return true;
        const auto collType = kind ==
                WebRendererShadowEntityKind::DynEntModel
            ? DYNENT_COLL_CLIENT_MODEL : DYNENT_COLL_CLIENT_BRUSH;
        return IsDynEntVisible(s_world,
            DynEnt_GetEntityCount(collType), entityId, drawType,
            primaryLightIndex);
    }
    default:
        return true;
    }
}

std::uint32_t WebRenderer_CalcReflectionProbeIndex(
    const GfxWorld &world, const float origin[3]) noexcept
{
    if (!world.reflectionProbes || world.reflectionProbeCount <= 1u)
        return 0u;

    const auto nearestFromList = [&](const std::uint8_t *indices,
                                     std::uint32_t count) noexcept
    {
        std::uint32_t bestIndex = 0u;
        float bestDistance = std::numeric_limits<float>::max();
        for (std::uint32_t entry = 0u; entry < count; ++entry)
        {
            const std::uint32_t index = indices ? indices[entry] : entry + 1u;
            if (index >= world.reflectionProbeCount) continue;
            float distance = 0.0f;
            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                const float delta = origin[axis] -
                    world.reflectionProbes[index].origin[axis];
                distance += delta * delta;
            }
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        return bestIndex;
    };

    // Mirror native R_CellForPoint and then restrict the probe search to the
    // canonical cell list. Bound the traversal count so invalid cycles fall
    // back to the global nearest-probe search.
    if (world.dpvsPlanes.nodes && world.dpvsPlanes.planes && world.cells &&
        world.dpvsPlanes.cellCount > 0)
    {
        const mnode_t *node = reinterpret_cast<const mnode_t *>(
            world.dpvsPlanes.nodes);
        const int cellLimit = world.dpvsPlanes.cellCount + 1;
        for (std::uint32_t step = 0u; step < 65536u; ++step)
        {
            const int nodeIndex = node->cellIndex;
            if (nodeIndex < cellLimit)
            {
                const int cellIndex = nodeIndex - 1;
                if (cellIndex >= 0 &&
                    cellIndex < world.dpvsPlanes.cellCount)
                {
                    const GfxCell &cell = world.cells[cellIndex];
                    if (cell.reflectionProbes &&
                        cell.reflectionProbeCount > 0u)
                        return nearestFromList(cell.reflectionProbes,
                            cell.reflectionProbeCount);
                }
                break;
            }
            const cplane_s &plane =
                world.dpvsPlanes.planes[nodeIndex - cellLimit];
            const float distance = origin[0] * plane.normal[0] +
                origin[1] * plane.normal[1] +
                origin[2] * plane.normal[2] - plane.dist;
            if (distance <= 0.0f && node->rightChildOffset < 2u)
                break;
            const std::uint16_t offset = distance <= 0.0f
                ? static_cast<std::uint16_t>(node->rightChildOffset - 2u)
                : 0u;
            node = reinterpret_cast<const mnode_t *>(
                reinterpret_cast<const std::uint8_t *>(node) +
                offset * 2u + 4u);
        }
    }
    return nearestFromList(nullptr, world.reflectionProbeCount - 1u);
}

bool RebuildWorldSurfaceRuntimeData() noexcept
{
    if (!s_world.models) return false;
    const std::uint32_t surfaceCount = s_world.models[0].surfaceCount;
    if (surfaceCount == 0u) return true;
    const std::uint32_t casterWordCount = (surfaceCount + 31u) >> 5u;
    if (!s_world.dpvs.surfaces || !s_world.dpvs.surfaceMaterials ||
        !s_world.dpvs.surfaceCastsSunShadow ||
        surfaceCount > s_world.dpvs.staticSurfaceCount ||
        casterWordCount > s_world.dpvs.surfaceVisDataCount)
    {
        return false;
    }

    std::fill_n(s_world.dpvs.surfaceCastsSunShadow,
        s_world.dpvs.surfaceVisDataCount, 0u);
    std::uint32_t casterCount = 0u;
    for (std::uint32_t surfaceIndex = 0u;
         surfaceIndex < surfaceCount; ++surfaceIndex)
    {
        const GfxSurface &surface = s_world.dpvs.surfaces[surfaceIndex];
        if (!surface.material) return false;
        GfxDrawSurf drawSurf = surface.material->info.drawSurf;
        if (drawSurf.fields.primaryLightIndex != 0u) return false;
        drawSurf.fields.primaryLightIndex = surface.primaryLightIndex;
        s_world.dpvs.surfaceMaterials[surfaceIndex] = drawSurf;
        if (drawSurf.fields.customIndex != 0u &&
            (surface.flags & 1u) != 0u)
        {
            s_world.dpvs.surfaceCastsSunShadow[surfaceIndex >> 5u] |=
                1u << (surfaceIndex & 31u);
            ++casterCount;
        }
    }
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Rebuilt canonical world draw-surface runtime data "
        "(%u surfaces, %u sun-shadow casters).\n",
        surfaceCount, casterCount);
    return true;
}

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
    batch.detailImage = ResolveRendererImage(batch.detailImage);
    batch.normalImage = ResolveRendererImage(batch.normalImage);
    batch.specularImage = ResolveRendererImage(batch.specularImage);
    batch.lightmapImage = ResolveRendererImage(batch.lightmapImage);
    batch.secondaryLightmapImage =
        ResolveRendererImage(batch.secondaryLightmapImage);
    batch.reflectionProbeImage =
        ResolveRendererImage(batch.reflectionProbeImage);
}

bool BuildRendererPrimaryLights(
    const GfxLight *frameLights,
    std::vector<WebRendererPrimaryLightDesc> &primaryLights,
    std::uint32_t &sunPrimaryLightIndex) noexcept
{
    if (!comWorld.isInUse || !comWorld.primaryLights ||
        comWorld.primaryLightCount > WEB_RENDERER_MAX_PRIMARY_LIGHTS)
    {
        return false;
    }
    try
    {
        primaryLights.clear();
        primaryLights.resize(comWorld.primaryLightCount);
        sunPrimaryLightIndex = s_world.sunPrimaryLightIndex;
        for (std::uint32_t index = 0u;
             index < comWorld.primaryLightCount; ++index)
        {
            const ComPrimaryLight &worldLight = comWorld.primaryLights[index];
            const GfxLight *frameLight = frameLights
                ? &frameLights[index] : nullptr;
            WebRendererPrimaryLightDesc &destination =
                primaryLights[index];
            destination.canUseShadowMap = frameLight
                ? frameLight->canUseShadowMap : worldLight.canUseShadowMap;
            destination.type = frameLight
                ? frameLight->type : worldLight.type;
            destination.exponent = static_cast<std::uint8_t>(std::clamp(
                frameLight ? frameLight->exponent
                           : static_cast<int>(worldLight.exponent),
                0, 255));
            const float diffuseScale = r_diffuseColorScale
                ? r_diffuseColorScale->current.value : 1.0f;
            for (std::size_t component = 0u; component < 3u; ++component)
                destination.color[component] =
                    (frameLight ? frameLight->color[component]
                                : worldLight.color[component]) * diffuseScale;
            std::copy_n(frameLight ? frameLight->dir : worldLight.dir,
                3u, destination.direction);
            std::copy_n(frameLight ? frameLight->origin : worldLight.origin,
                3u, destination.origin);
            destination.radius = frameLight
                ? frameLight->radius : worldLight.radius;
            destination.cosHalfFovOuter = frameLight
                ? frameLight->cosHalfFovOuter : worldLight.cosHalfFovOuter;
            destination.cosHalfFovInner = frameLight
                ? frameLight->cosHalfFovInner : worldLight.cosHalfFovInner;
            const GfxLightDef *definition = frameLight
                ? frameLight->def : nullptr;
            if (!definition && worldLight.defName)
            {
                definition = DB_FindXAssetHeader(
                    ASSET_TYPE_LIGHT_DEF, worldLight.defName).lightDef;
            }
            if (definition)
            {
                const GfxImage *attenuation = ResolveRendererImage(
                    definition->attenuation.image);
                destination.falloffScale = attenuation
                    ? static_cast<float>(attenuation->width) / 512.0f
                    : 0.0f;
                destination.falloffShift =
                    static_cast<float>(definition->lmapLookupStart) / 512.0f;
            }
        }
        if (!frameLights &&
            s_world.sunPrimaryLightIndex < primaryLights.size() &&
            s_world.sunLight)
        {
            WebRendererPrimaryLightDesc &sun =
                primaryLights[s_world.sunPrimaryLightIndex];
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

struct MaterialFeature
{
    const char *name;
    std::uint32_t mask;
    std::uint32_t value;
};

constexpr std::array<MaterialFeature, 20> MATERIAL_FEATURES{{
    {"s0", 4u, 0u}, {"s1", 4u, 0u}, {"s2", 4u, 0u},
    {"s3", 4u, 0u}, {"s4", 4u, 0u}, {"d0", 8u, 0u},
    {"d1", 8u, 0u}, {"d2", 8u, 0u}, {"d3", 8u, 0u},
    {"d4", 8u, 0u}, {"n0", 16u, 0u}, {"n1", 16u, 0u},
    {"n2", 16u, 0u}, {"n3", 16u, 0u}, {"n4", 16u, 0u},
    {"zfeather", 1u, 0u}, {"outdoor", 2u, 0u},
    {"sm", 384u, 128u}, {"hsm", 384u, 256u}, {"twk", 32u, 0u},
}};

bool g_techniqueSetRemapsDirty = true;
std::uint32_t g_techniqueSetRemapMask = UINT32_MAX;
std::uint32_t g_techniqueSetRemapValue = UINT32_MAX;

struct TechniqueSetRemapStats
{
    std::uint32_t shaderModel3 = 0u;
    std::uint32_t references = 0u;
    std::uint32_t featureRemaps = 0u;
    bool changed = false;
};

TechniqueSetRemapStats ResolveTechniqueSetRemaps(bool force = false)
{
    TechniqueSetRemapStats stats{};
    if (!DB_AreAssetPoolsInitialized()) return stats;
    std::uint32_t remapMask = 0x180u;
    constexpr std::uint32_t remapValue = 0x100u;
    if (r_detail && !r_detail->current.enabled) remapMask |= 8u;
    if (r_specular && !r_specular->current.enabled) remapMask |= 4u;
    if (r_normal && !r_normal->current.enabled) remapMask |= 16u;
    if (r_zFeather && !r_zFeather->current.enabled) remapMask |= 1u;
    if (r_outdoor && !r_outdoor->current.enabled) remapMask |= 2u;
    if (r_envMapOverride && r_envMapOverride->current.enabled) remapMask |= 32u;
    if (!force && !g_techniqueSetRemapsDirty &&
        remapMask == g_techniqueSetRemapMask &&
        remapValue == g_techniqueSetRemapValue)
        return stats;

    std::vector<MaterialTechniqueSet *> techniqueSets;
    DB_EnumXAssets(ASSET_TYPE_TECHNIQUE_SET,
        CollectTechniqueSet, &techniqueSets, false);

    const MaterialTechniqueSetRemapStats resolved =
        Material_ResolveTechniqueSetRemapsCore(
            techniqueSets.data(), techniqueSets.size(), remapMask, remapValue,
            MATERIAL_FEATURES.data(), MATERIAL_FEATURES.size(),
            [](const char *name) {
                XAssetEntryPoolEntry *entry = DB_FindXAssetEntryCanonical(
                    ASSET_TYPE_TECHNIQUE_SET, name);
                return entry ? entry->entry.asset.header.techniqueSet : nullptr;
            });
    stats.shaderModel3 = resolved.shaderModel3;
    stats.references = resolved.references;
    stats.featureRemaps = resolved.featureRemaps;
    g_techniqueSetRemapMask = remapMask;
    g_techniqueSetRemapValue = remapValue;
    g_techniqueSetRemapsDirty = false;
    stats.changed = true;
    return stats;
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
DpvsGlobals g_cameraDpvs{};
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
std::array<std::uint8_t, MAX_GENTITIES> g_dobjCellLinkValid{};
std::array<std::uint8_t, MAX_GENTITIES> g_brushCellLinkValid{};
// Renderer link metadata corresponding to native scene.dpvs.entInfo.radius.
// Cgame owns the supplied radius (including its movement tolerance); the
// per-frame pose radius must not replace it for inner portal-plane tests.
std::array<float, MAX_GENTITIES> g_dobjCellLinkRadius{};

struct PortalSceneAdmissionState
{
    GfxWorld *world = nullptr;
    std::uint32_t entityCount = 0u;
    std::uint32_t localClientNum = 0u;
    const WebRendererLodParms *lodParms = nullptr;
    const float *viewOffset = nullptr;
#if KISAK_WEB_DIAGNOSTICS
    unsigned posedTests = 0u, posedPlaneRejects = 0u, posedCellRejects = 0u;
#endif
    std::array<std::uint8_t,
        WEB_RENDERER_MAX_DYNAMIC_DOBJ_SUBMISSIONS> dobjVisible{};
    std::array<std::uint8_t,
        WEB_RENDERER_MAX_DYNAMIC_BMODEL_SUBMISSIONS> brushVisible{};
    bool valid = true;
};

void ObservePortalSceneCell(void *user, const GfxCell *cell,
    const DpvsPlane *planes, unsigned char planeCount,
    unsigned char frustumPlaneCount)
{
    auto *state = static_cast<PortalSceneAdmissionState *>(user);
    if (!state || !state->valid || !state->world || !cell ||
        frustumPlaneCount > planeCount ||
        planeCount > DPVS_PORTAL_MAX_PLANES ||
        (planeCount != 0u && !planes))
    {
        if (state) state->valid = false;
        return;
    }
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(
        state->world->cells);
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(cell);
    if (!begin || target < begin ||
        (target - begin) % sizeof(GfxCell) != 0u)
    {
        state->valid = false;
        return;
    }
    const std::uint32_t cellIndex = static_cast<std::uint32_t>(
        (target - begin) / sizeof(GfxCell));
    if (cellIndex >= static_cast<std::uint32_t>(
            state->world->dpvsPlanes.cellCount))
    {
        state->valid = false;
        return;
    }
    // DynEntities use every cell plane, including the camera prefix. Native
    // scene DObjs/brushes below have already consumed that prefix separately.
    if (!r_drawDynEnts || r_drawDynEnts->current.enabled)
        for (unsigned kind = 0; kind < 2u; ++kind)
        {
            const auto drawType = static_cast<DynEntityDrawType>(kind);
            const unsigned count = state->world->dpvsDyn.dynEntClientCount[kind];
            if (!R_CullDynEntityCell(*state->world, kind, cellIndex,
                    kind == 0u ? DynEnt_GetClientModelPoseList() : nullptr,
                    kind == 1u && count ? DynEnt_GetEntityDef(0u, drawType) : nullptr,
                    planes, planeCount, state->world->dpvsDyn.dynEntVisData[kind][0]))
                Com_Error(ERR_DROP, "R_RenderScene: invalid DynEntity portal cell data");
        }
    const DpvsPlane *const innerPlanes =
        planes ? planes + frustumPlaneCount : nullptr;
    const int innerPlaneCount = planeCount - frustumPlaneCount;

    for (std::uint32_t index = 0u;
         index < g_dobjSubmissionCount; ++index)
    {
        const WebRendererDObjSubmission &submission =
            g_dobjSubmissions[index];
        if (state->dobjVisible[index]) continue;
        if (submission.entityNumber >= g_dobjCellLinkValid.size() ||
            !g_dobjCellLinkValid[submission.entityNumber])
            continue;
        const DpvsSceneEntityCellLink link =
            R_QuerySceneEntityCellLink(*state->world,
                state->localClientNum, state->entityCount,
                submission.entityNumber, cellIndex,
                DpvsSceneEntityKind::DObj);
        if (link == DpvsSceneEntityCellLink::Unavailable)
        {
            state->valid = false;
            return;
        }
        if (link != DpvsSceneEntityCellLink::Linked)
            continue;
        const float radius =
            g_dobjCellLinkRadius[submission.entityNumber];
        if (!submission.obj || !submission.pose || !std::isfinite(radius) || radius < 0.0f)
        {
            state->dobjVisible[index] = 1u; // Preserve the builder's validation path.
            continue;
        }
        if (innerPlaneCount != 0 && kisak::dpvs::CullSphere(
                submission.pose->origin, radius, innerPlanes, innerPlaneCount))
            continue;
        const bool sceneEntity = (submission.renderFlags & 4u) != 0u ||
            submission.obj->tree || submission.obj->numModels != 1u;
        if (sceneEntity)
        {
            const DpvsView &camera = g_cameraDpvs.views[state->localClientNum][0];
            const float cameraRadius = static_cast<float>(DObjGetRadius(submission.obj));
            if (std::isfinite(cameraRadius) && cameraRadius >= 0.0f &&
                kisak::dpvs::CullSphere(submission.pose->origin, cameraRadius,
                    camera.frustumPlanes, camera.frustumPlaneCount))
                continue;
            float mins[3]{}, maxs[3]{};
            const auto result = WebRenderer_ComputeDObjVisibilityBounds(
                submission, state->lodParms, state->viewOffset, mins, maxs);
            if (result == WebRendererDObjSceneResult::NoDObj) continue;
            if (result != WebRendererDObjSceneResult::Success)
                Com_Error(ERR_DROP, "R_RenderScene: invalid canonical DObj cell bounds");
#if KISAK_WEB_DIAGNOSTICS
            ++state->posedTests;
#endif
            // Native's post-pose worker consumes the full planes, then verifies
            // the updated box still occupies this precise BSP cell. Repeated
            // paths retry rejected poses through CG_DObjCalcPose's own skeleton.
            if (planeCount != 0 && kisak::dpvs::CullBox(mins, maxs, planes, planeCount))
            {
#if KISAK_WEB_DIAGNOSTICS
                ++state->posedPlaneRejects;
#endif
                continue;
            }
            bool inside = false;
            if (!R_QueryBoundsInCell(*state->world, cellIndex, mins, maxs, inside))
                Com_Error(ERR_DROP, "R_RenderScene: invalid canonical DObj BSP cell query");
            if (!inside)
            {
#if KISAK_WEB_DIAGNOSTICS
                ++state->posedCellRejects;
#endif
                continue;
            }
            CG_CullIn(const_cast<cpose_t *>(submission.pose));
        }
        state->dobjVisible[index] = 1u;
    }

    for (std::uint32_t index = 0u;
         index < g_brushModelSubmissionCount; ++index)
    {
        const WebRendererBrushModelSubmission &submission =
            g_brushModelSubmissions[index];
        if (submission.entityNumber >= g_brushCellLinkValid.size() ||
            !g_brushCellLinkValid[submission.entityNumber])
            continue;
        const DpvsSceneEntityCellLink link =
            R_QuerySceneEntityCellLink(*state->world,
                state->localClientNum, state->entityCount,
                submission.entityNumber, cellIndex,
                DpvsSceneEntityKind::Brush);
        if (link == DpvsSceneEntityCellLink::Unavailable)
        {
            state->valid = false;
            return;
        }
        if (link != DpvsSceneEntityCellLink::Linked)
            continue;
        if (!submission.model || innerPlaneCount == 0 ||
            !kisak::dpvs::CullBox(submission.model->writable.mins,
                submission.model->writable.maxs,
                innerPlanes, innerPlaneCount))
            state->brushVisible[index] = 1u;
    }
}
struct BrushGeometryReference
{
    std::uint32_t handle = UINT32_MAX;
    std::uint32_t surfaces = 0u;
    std::uint32_t batches = 0u;
    std::uint32_t vertices = 0u;
    std::uint32_t indices = 0u;
};
std::vector<BrushGeometryReference> g_brushGeometryByModel;
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
int g_uiSceneTime = 0;
std::array<GfxLight, 32> g_addedLights{};
std::uint32_t g_addedLightCount = 0;
float g_dynamicSpotLightNearPlaneOffset = 0.0f;
#if KISAK_WEB_DIAGNOSTICS
int g_testTransientLightMode = 0;
int g_testDynEntityCamera = -1;
bool g_testDynEntityCameraRendered = false;
// A render-only fixture: select an existing linked model, without changing
// the player, canonical poses, scripts, or the authored camera owner.
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestDynEntityCamera(int enabled)
{
    if (enabled < 0) return g_testDynEntityCameraRendered ? 1 : 0;
    g_testDynEntityCameraRendered = false;
    g_testDynEntityCamera = -1;
    if (!enabled || !g_rendererWorldReady ||
        !R_DynEntityCellLayoutAvailable(s_world, 0u)) return 0;
    const unsigned count = DynEnt_GetEntityCount(DYNENT_COLL_CLIENT_MODEL);
    if (count != s_world.dpvsDyn.dynEntClientCount[0]) return 0;
    for (unsigned id = 0; id < count; ++id)
    {
        const auto *client = DynEnt_GetClientEntity(id, DYNENT_DRAW_MODEL);
        const auto *pose = DynEnt_GetClientPose(id, DYNENT_DRAW_MODEL);
        if (!(client->flags & DYNENT_CL_VISIBLE) ||
            !std::isfinite(pose->radius) || pose->radius < 8.0f) continue;
        for (int cell = 0; cell < s_world.dpvsPlanes.cellCount; ++cell)
            if (s_world.dpvsDyn.dynEntCellBits[0][
                    cell * s_world.dpvsDyn.dynEntClientWordCount[0] + (id >> 5u)] &
                (0x80000000u >> (id & 31u)))
            {
                g_testDynEntityCamera = static_cast<int>(id);
                return static_cast<int>(id + 1u);
            }
    }
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestTransientLights(int mode)
{
    if (mode >= 0 && mode <= 3) g_testTransientLightMode = mode;
    return static_cast<int>(g_addedLightCount);
}
#endif

EM_JS(void, Web_GetCanvasSize, (std::uint32_t *width, std::uint32_t *height), {
    const canvas = Module.canvas;
    HEAPU32[width >> 2] = canvas ? (canvas.width >>> 0) : 1280;
    HEAPU32[height >> 2] = canvas ? (canvas.height >>> 0) : 720;
});

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

bool AppendSunSprite(WebRendererDObjSceneCommand &command,
    const WebRendererSceneViewDesc &view,
    std::uint32_t brushVertices, std::uint32_t brushIndices) noexcept
{
    static bool reported = false;
    if (!s_world.sun.hasValidData || !s_world.sun.spriteMaterial ||
        s_world.sun.spriteSize <= 0.0f)
        return false;
    Material *material = ResolveRendererMaterial(s_world.sun.spriteMaterial);
    if (!material) return false;
    std::uint8_t samplerState = 0u;
    const GfxImage *image = FindFxImage(material, samplerState);
    if (!image) return false;

    float perpendicular[3]{};
    const float *sun = s_world.sun.sunFxPosition;
    if (sun[2] * sun[2] <= 0.99000001f)
    {
        perpendicular[0] = sun[1];
        perpendicular[1] = -sun[0];
    }
    else
        perpendicular[0] = 1.0f;
    float right[3]{};
    Vec3Cross(sun, perpendicular, right);
    if (Vec3Normalize(right) <= 0.0f) return false;
    Vec3Scale(right,
        s_world.sun.spriteSize * 0.001311092986725271f, right);
    float up[3]{};
    Vec3Cross(right, sun, up);
    float rightUp[3]{};
    float rightDown[3]{};
    Vec3Add(right, up, rightUp);
    Vec3Sub(right, up, rightDown);
    float directions[4][3]{};
    Vec3Add(sun, rightUp, directions[0]);
    Vec3Add(sun, rightDown, directions[1]);
    Vec3Sub(sun, rightUp, directions[2]);
    Vec3Sub(sun, rightDown, directions[3]);

    constexpr float uvs[4][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    try
    {
        if (command.vertices.size() + brushVertices + 4u >
                WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES ||
            command.indices.size() + brushIndices + 6u >
                WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES)
            return false;
        const std::uint32_t vertexBase = static_cast<std::uint32_t>(
            command.vertices.size());
        const std::uint32_t firstIndex = static_cast<std::uint32_t>(
            command.indices.size());
        for (std::size_t vertexIndex = 0u; vertexIndex < 4u; ++vertexIndex)
        {
            float clip[4]{};
            for (std::size_t column = 0u; column < 4u; ++column)
            {
                clip[column] =
                    directions[vertexIndex][0] *
                        view.viewProjectionMatrix[0][column] +
                    directions[vertexIndex][1] *
                        view.viewProjectionMatrix[1][column] +
                    directions[vertexIndex][2] *
                        view.viewProjectionMatrix[2][column];
            }
            if (!std::isfinite(clip[3]) || clip[3] <= 0.0f)
                return false;
            WebRendererSurfaceVertex vertex{};
            vertex.position[0] = clip[0] / clip[3];
            vertex.position[1] = clip[1] / clip[3];
            vertex.position[2] = clip[2] / clip[3];
            vertex.textureCoordinate[0] = uvs[vertexIndex][0];
            vertex.textureCoordinate[1] = uvs[vertexIndex][1];
            std::fill_n(vertex.color, 4u, 1.0f);
            command.vertices.push_back(vertex);
        }
        // Match RB_SetTessQuad's canonical winding exactly.  The authored sun
        // material retains its native cull state, so reversing these triangles
        // makes the otherwise-valid billboard disappear.
        constexpr std::uint32_t localIndices[6] = {3u, 0u, 2u, 2u, 0u, 1u};
        for (const std::uint32_t index : localIndices)
            command.indices.push_back(vertexBase + index);

        WebRendererWorldBatchDesc batch{};
        batch.firstIndex = firstIndex;
        batch.indexCount = 6u;
        batch.surfaceCount = 1u;
        batch.materialIdentity = material;
        batch.materialName = material->info.name
            ? material->info.name : "<sun-sprite>";
        batch.modelName = "<sun-sprite>";
        batch.firstInstanceIndex = UINT32_MAX;
        batch.lastInstanceIndex = UINT32_MAX;
        batch.baseImage = image;
        batch.samplerState = samplerState;
        batch.sourceKind = WebRendererSceneBatchKind::SunSprite;
        batch.technique = WebRendererWorldTechnique::BaseTexture;
        batch.techniqueType = 4u;
        const MaterialTechnique *technique = material->techniqueSet
            ? material->techniqueSet->techniques[4u] : nullptr;
        batch.techniqueName = technique && technique->name
            ? technique->name : "<sun-unlit>";
        if (material->stateBitsTable)
        {
            const std::uint8_t entry = material->stateBitsEntry[4u];
            if (entry != 0xffu && entry < material->stateBitsCount)
            {
                batch.stateBits[0] = material->stateBitsTable[entry].loadBits[0];
                batch.stateBits[1] = material->stateBitsTable[entry].loadBits[1];
            }
        }
        command.batches.push_back(batch);
        ++command.surfaceCount;
        if (!reported)
        {
            reported = true;
            Material *flareMaterial = s_world.sun.flareMaterial
                ? ResolveRendererMaterial(s_world.sun.flareMaterial) : nullptr;
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical sun sprite: material='%s' "
                "image='%s' size=%.3f direction=(%.5f %.5f %.5f) "
                "sampler=0x%02x state=0x%08x/0x%08x.\n",
                batch.materialName,
                image->name ? image->name : "<unnamed>",
                s_world.sun.spriteSize,
                sun[0], sun[1], sun[2], samplerState,
                batch.stateBits[0], batch.stateBits[1]);
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical sun post-effects: flare='%s' "
                "size=(%.3f %.3f) dot=(%.5f %.5f) alpha=%.5f "
                "viewDot=%.5f "
                "fade=(%d %d); blind=(%.5f %.5f %.5f %d %d) "
                "glare=(%.5f %.5f %.5f %d %d).\n",
                flareMaterial && flareMaterial->info.name
                    ? flareMaterial->info.name : "<none>",
                s_world.sun.flareMinSize, s_world.sun.flareMaxSize,
                s_world.sun.flareMinDot, s_world.sun.flareMaxDot,
                s_world.sun.flareMaxAlpha,
                Vec3Dot(sun, view.viewAxis[0]),
                s_world.sun.flareFadeInTime,
                s_world.sun.flareFadeOutTime,
                s_world.sun.blindMinDot, s_world.sun.blindMaxDot,
                s_world.sun.blindMaxDarken,
                s_world.sun.blindFadeInTime,
                s_world.sun.blindFadeOutTime,
                s_world.sun.glareMinDot, s_world.sun.glareMaxDot,
                s_world.sun.glareMaxLighten,
                s_world.sun.glareFadeInTime,
                s_world.sun.glareFadeOutTime);
        }
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}

bool AppendSunFlare(WebRendererDObjSceneCommand &command,
    const WebRendererSceneViewDesc &view,
    std::uint32_t brushVertices, std::uint32_t brushIndices) noexcept
{
    if (!s_world.sun.hasValidData || !s_world.sun.flareMaterial ||
        s_world.sun.flareMaxAlpha <= 0.0f)
        return false;
    const float sunDot = Vec3Dot(s_world.sun.sunFxPosition, view.viewAxis[0]);
    if (sunDot <= s_world.sun.flareMinDot)
        return false;
    float lerp = 1.0f;
    if (sunDot < s_world.sun.flareMaxDot &&
        s_world.sun.flareMaxDot > s_world.sun.flareMinDot)
    {
        lerp = (sunDot - s_world.sun.flareMinDot) /
            (s_world.sun.flareMaxDot - s_world.sun.flareMinDot);
    }
    lerp = std::clamp(lerp, 0.0f, 1.0f);
    const float alpha = lerp * s_world.sun.flareMaxAlpha;
    const float size = s_world.sun.flareMinSize +
        lerp * s_world.sun.flareMaxSize;
    if (alpha <= 0.0f || size <= 0.0f)
        return false;

    Material *material = ResolveRendererMaterial(s_world.sun.flareMaterial);
    if (!material) return false;
    std::uint8_t samplerState = 0u;
    const GfxImage *image = FindFxImage(material, samplerState);
    if (!image) return false;

    float clip[4]{};
    for (std::size_t column = 0u; column < 4u; ++column)
    {
        clip[column] =
            s_world.sun.sunFxPosition[0] *
                view.viewProjectionMatrix[0][column] +
            s_world.sun.sunFxPosition[1] *
                view.viewProjectionMatrix[1][column] +
            s_world.sun.sunFxPosition[2] *
                view.viewProjectionMatrix[2][column];
    }
    if (!std::isfinite(clip[3]) || clip[3] <= 0.0f)
        return false;
    const float centerX = clip[0] / clip[3];
    const float centerY = clip[1] / clip[3];
    const float centerZ = clip[2] / clip[3];
    const float centerU = centerX * 0.5f + 0.5f;
    const float centerV = centerY * 0.5f + 0.5f;
    if (centerU < 0.0f || centerU > 1.0f ||
        centerV < 0.0f || centerV > 1.0f)
        return false;

    try
    {
        if (command.vertices.size() + brushVertices + 4u >
                WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES ||
            command.indices.size() + brushIndices + 6u >
                WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES)
            return false;
        const std::uint32_t vertexBase = static_cast<std::uint32_t>(
            command.vertices.size());
        const std::uint32_t firstIndex = static_cast<std::uint32_t>(
            command.indices.size());
        const float halfWidth = size / 640.0f;
        const float halfHeight = size / 480.0f;
        constexpr float offsets[4][2] = {
            {1.0f, 1.0f}, {1.0f, -1.0f},
            {-1.0f, -1.0f}, {-1.0f, 1.0f},
        };
        constexpr float uvs[4][2] = {
            {0.0f, 0.0f}, {1.0f, 0.0f},
            {1.0f, 1.0f}, {0.0f, 1.0f},
        };
        for (std::size_t vertexIndex = 0u; vertexIndex < 4u; ++vertexIndex)
        {
            WebRendererSurfaceVertex vertex{};
            vertex.position[0] = centerX + offsets[vertexIndex][0] * halfWidth;
            vertex.position[1] = centerY + offsets[vertexIndex][1] * halfHeight;
            // RB_TessSunBillboard offsets D3D depth by -0.0005. The view
            // matrix supplied here has already converted D3D [0,1] NDC to
            // WebGL [-1,1], so preserve the authored offset at double scale.
            vertex.position[2] = centerZ - 0.0010000000474974513f;
            vertex.textureCoordinate[0] = uvs[vertexIndex][0];
            vertex.textureCoordinate[1] = uvs[vertexIndex][1];
            vertex.color[0] = alpha;
            vertex.color[1] = alpha;
            vertex.color[2] = alpha;
            vertex.color[3] = 1.0f;
            command.vertices.push_back(vertex);
        }
        constexpr std::uint32_t localIndices[6] = {
            3u, 0u, 2u, 2u, 0u, 1u,
        };
        for (const std::uint32_t index : localIndices)
            command.indices.push_back(vertexBase + index);

        WebRendererWorldBatchDesc batch{};
        batch.firstIndex = firstIndex;
        batch.indexCount = 6u;
        batch.surfaceCount = 1u;
        batch.materialIdentity = material;
        batch.materialName = material->info.name
            ? material->info.name : "<sun-flare>";
        batch.modelName = "<sun-flare>";
        batch.firstInstanceIndex = UINT32_MAX;
        batch.lastInstanceIndex = UINT32_MAX;
        batch.baseImage = image;
        batch.samplerState = samplerState;
        batch.sourceKind = WebRendererSceneBatchKind::SunFlare;
        batch.technique = WebRendererWorldTechnique::BaseTexture;
        batch.techniqueType = 4u;
        const MaterialTechnique *technique = material->techniqueSet
            ? material->techniqueSet->techniques[4u] : nullptr;
        batch.techniqueName = technique && technique->name
            ? technique->name : "<sun-flare-unlit>";
        if (material->stateBitsTable)
        {
            const std::uint8_t entry = material->stateBitsEntry[4u];
            if (entry != 0xffu && entry < material->stateBitsCount)
            {
                batch.stateBits[0] = material->stateBitsTable[entry].loadBits[0];
                batch.stateBits[1] = material->stateBitsTable[entry].loadBits[1];
            }
        }
        // The post-effect pass samples the resolved scene depth at the native
        // 16-pixel query center.  Keep that platform-owned visibility input in
        // an existing portable constant payload rather than exposing a GL
        // query handle through the frontend seam.
        batch.falloffParms[0] = centerU;
        batch.falloffParms[1] = centerV;
        batch.falloffParms[2] =
            (centerZ - 0.0010000000474974513f) * 0.5f + 0.5f;
        float sunTraceEnd[3]{};
        Vec3Mad(view.viewOrigin, 262144.0f,
            s_world.sun.sunFxPosition, sunTraceEnd);
        batch.falloffParms[3] = CM_BoxSightTrace(
            0, view.viewOrigin, sunTraceEnd,
            vec3_origin, vec3_origin, 0u, 8195) == 0
            ? 1.0f : 0.0f;
        batch.falloffBeginColor[0] = static_cast<float>(
            s_world.sun.flareFadeInTime);
        batch.falloffBeginColor[1] = static_cast<float>(
            s_world.sun.flareFadeOutTime);
        float blindLerp = 0.0f;
        if (s_world.sun.blindMaxDarken > 0.0f &&
            sunDot > s_world.sun.blindMinDot)
        {
            blindLerp = 1.0f;
            if (sunDot < s_world.sun.blindMaxDot &&
                s_world.sun.blindMaxDot > s_world.sun.blindMinDot)
            {
                blindLerp = (sunDot - s_world.sun.blindMinDot) /
                    (s_world.sun.blindMaxDot - s_world.sun.blindMinDot);
            }
            blindLerp = std::clamp(blindLerp, 0.0f, 1.0f);
        }
        batch.envMapParms[0] = blindLerp;
        batch.envMapParms[1] = std::max(0.0f,
            s_world.sun.blindMaxDarken);
        batch.envMapParms[2] = static_cast<float>(
            s_world.sun.blindFadeInTime);
        batch.envMapParms[3] = static_cast<float>(
            s_world.sun.blindFadeOutTime);
        float glareLerp = 0.0f;
        if (s_world.sun.glareMaxLighten > 0.0f &&
            sunDot > s_world.sun.glareMinDot)
        {
            glareLerp = 1.0f;
            if (sunDot < s_world.sun.glareMaxDot &&
                s_world.sun.glareMaxDot > s_world.sun.glareMinDot)
            {
                glareLerp = (sunDot - s_world.sun.glareMinDot) /
                    (s_world.sun.glareMaxDot - s_world.sun.glareMinDot);
            }
            glareLerp = std::clamp(glareLerp, 0.0f, 1.0f);
        }
        batch.waterColor[0] = glareLerp;
        batch.waterColor[1] = std::max(0.0f,
            s_world.sun.glareMaxLighten);
        batch.waterColor[2] = static_cast<float>(
            s_world.sun.glareFadeInTime);
        batch.waterColor[3] = static_cast<float>(
            s_world.sun.glareFadeOutTime);
        command.batches.push_back(batch);
        ++command.surfaceCount;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
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

GfxCmdDrawText2D MakeTextCommand(int maxChars, Font_s *font,
    float x, float y, float xScale, float yScale, float rotation,
    const float *color, int style)
{
    GfxCmdDrawText2D cmd{};
    cmd.x = x;
    cmd.y = y;
    cmd.font = font;
    cmd.xScale = xScale;
    cmd.yScale = yScale;
    cmd.rotation = rotation;
    cmd.maxChars = maxChars > 0 ? maxChars : INT_MAX;
    cmd.renderFlags = R_TextStyleFlags(style);
    if (color) Byte4PackVertexColor(color, cmd.color.array);
    else cmd.color.packed = UINT32_MAX;
    return cmd;
}

void AppendUiText(const char *text, const GfxCmdDrawText2D &cmd)
{
    if (!text || !cmd.font || !cmd.font->glyphs || !cmd.font->material) return;
#if KISAK_WEB_DIAGNOSTICS
    g_testUiTextHashes[g_testUiTextHashCursor++ % g_testUiTextHashes.size()] =
        HashDiagnosticText(text);
#endif
    const float radians = cmd.rotation * 0.017453292519943295f;
    DrawText2D(text, cmd.x, cmd.y, cmd.font, cmd.xScale, cmd.yScale,
        std::sin(radians), std::cos(radians), cmd.color, cmd.maxChars,
        static_cast<short>(cmd.renderFlags), cmd.cursorPos, cmd.cursorLetter,
        cmd.padding, cmd.glowForceColor, cmd.fxBirthTime, cmd.fxLetterTime,
        cmd.fxDecayStartTime, cmd.fxDecayDuration, cmd.fxMaterial, cmd.fxMaterialGlow);
}

void AppendConsoleText(char *pool, int poolSize, int first, int count,
    const GfxCmdDrawText2D &parameters)
{
    // The canonical console is a byte ring. Bound both copy spans before
    // using its shared copy routine (including embedded HUD-icon payloads).
    if (!pool || poolSize <= 0 || first < 0 || first >= poolSize ||
        count <= 0 || count > poolSize) return;
    std::vector<unsigned char> storage(sizeof(GfxCmdDrawText2D) + count);
    auto *cmd = new (storage.data()) GfxCmdDrawText2D(parameters);
    CopyPoolTextToCmd(pool, poolSize, first, count, cmd);
    AppendUiText(cmd->text, *cmd);
}

#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestUiTextSeen(
    std::uint32_t textHash)
{
    return std::find(g_testUiTextHashes.begin(), g_testUiTextHashes.end(),
        textHash) != g_testUiTextHashes.end();
}
#endif

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

    if (!WebRenderer_Initialize())
        Com_Error(ERR_FATAL, "Could not initialize the browser renderer");
    // CL_Init has seeded the process random stream by this registration point.
    // Match native R_CreateParticleCloudBuffer's one lattice per renderer
    // creation rather than deriving a new center in each scene command.
    WebRenderer_InitializeParticleCloudLayout();
    // WebGL2 exceeds IW3's shader-model-3 feature floor. Select the same
    // canonical best path as a max-graphics D3D9 client before zone assets
    // are loaded, so shader programs and technique sets remain coherent.
    Dvar_SetInt(r_rendererInUse, 1);
    // WebGL exposes no VRAM capacity. Auto quality uses the existing 800 MiB
    // decoded-image admission ceiling and a 1 GiB platform planning budget,
    // not a claim about the user's hardware. Manual policy is native.
    R_SetPicmipForMemory(800, 1024);
    Cmd_AddCommandInternal("r_applyPicmip", ApplyPicmipForBrowser,
        &g_applyPicmipCommand);
    g_applyPicmipCommandRegistered = true;
    std::memset(configuration, 0, sizeof(*configuration));
    WebDisplay_Configure(configuration);
    configuration->maxTextureSize = 2048;
    configuration->maxTextureMaps = 16;
    configuration->deviceSupportsGamma = false;
}

void __cdecl R_Shutdown(int destroyWindow)
{
    if (g_applyPicmipCommandRegistered)
    {
        Cmd_RemoveCommand("r_applyPicmip");
        g_applyPicmipCommandRegistered = false;
    }
    if (destroyWindow) WebRenderer_Shutdown();
}

void __cdecl R_UnloadWorld()
{
    UI_InvalidateSaveGameShot();
    g_addedLightCount = 0;
    g_dynamicSpotLightNearPlaneOffset = 0.0f;
    WebRenderer_ReleaseDObjSceneScratch();
    WebRenderer_UnloadWorldResources();
    decltype(g_brushGeometryByModel){}.swap(g_brushGeometryByModel);
}
void R_ShutdownDirect3D() { WebRenderer_Shutdown(); }
void __cdecl R_SyncRenderThread() {}
void __cdecl R_BeginFrame()
{
    const TechniqueSetRemapStats remaps = ResolveTechniqueSetRemaps();
    if (remaps.changed)
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Remapped %u TechniqueSets for renderer feature "
            "changes (%u canonical references).\n",
            remaps.featureRemaps, remaps.references);
#if KISAK_WEB_DIAGNOSTICS
    g_frontendProfileStarted = WebFrameProfile_Current()
        ? WebFrameProfile_Now() : 0.0;
#endif
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
    const float gamma = r_gamma &&
        (!r_ignoreHwGamma || !r_ignoreHwGamma->current.enabled)
        ? r_gamma->current.value : 1.0f;
    if (!WebRenderer_SetDisplayGamma(gamma))
        Com_Error(ERR_DROP, "R_EndFrame: invalid display gamma");
#if KISAK_WEB_DIAGNOSTICS
    if (WebFrameProfileSample *const profile = WebFrameProfile_Current();
        profile && g_frontendProfileStarted != 0.0)
    {
        profile->rendererFrontendMs +=
            WebFrameProfile_Now() - g_frontendProfileStarted;
    }
    g_frontendProfileStarted = 0.0;
#endif
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
    if (!RebuildWorldSurfaceRuntimeData())
        Com_Error(ERR_DROP,
            "R_LoadWorld: canonical GfxWorld '%s' has invalid world "
            "draw-surface runtime storage", name);
    const TechniqueSetRemapStats remaps = ResolveTechniqueSetRemaps(true);
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Selected %u canonical shader-model-3 technique sets "
        "for the WebGL2 renderer after zone publication; %u feature remaps "
        "and %u leading-comma references resolved.\n",
        remaps.shaderModel3, remaps.featureRemaps, remaps.references);
    for (unsigned kind = 0; kind < 2u; ++kind)
        if (!R_ClearDynEntityCellLinks(s_world, kind))
            Com_Error(ERR_DROP, "R_LoadWorld: invalid DynEntity cell storage");
#if KISAK_WEB_DIAGNOSTICS
    g_testDynEntityCamera = -1;
#endif
    g_rendererWorldReady = true;
    g_dobjCellLinkValid.fill(0u);
    g_brushCellLinkValid.fill(0u);
    g_dobjCellLinkRadius.fill(0.0f);
    g_gameDrivenFrameReported = false;
    g_visionLightingReported = false;
    g_sceneBlurReported = false;
    g_worldSceneSubmitted = false;
    g_staticModelSceneSubmitted = false;
    g_brushGeometryByModel.clear();
    g_dynamicModelSceneReported = false;
    g_dynamicBrushSceneReported = false;
    g_dynamicEntityModelSceneReported = false;
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
            lineWidth += R_GetCharacterGlyph(font, letter)->dx;
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
            if (font) pixelsUsed += R_GetCharacterGlyph(font, letter)->dx;
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
            width += R_GetCharacterGlyph(font, letter)->dx;
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
    const float *color, int style)
{
    AppendUiText(text, MakeTextCommand(maxChars, font, x, y, xScale, yScale,
        rotation, color, style));
}
void __cdecl R_AddCmdDrawTextSubtitle(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int style, const float *glow, bool cinematic)
{
    auto cmd = MakeTextCommand(maxChars, font, x, y, xScale, yScale, rotation, color, style);
    cmd.renderFlags |= 0x100 | (cinematic ? 0x200 : 0);
    SetDrawText2DGlowParms(&cmd, color, glow);
    AppendUiText(text, cmd);
}
void __cdecl R_AddCmdDrawTextWithCursor(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int style, int cursorPos, char cursor)
{
    auto cmd = MakeTextCommand(maxChars, font, x, y, xScale, yScale, rotation, color, style);
    if (cursorPos >= 0) {
        cmd.renderFlags |= 2;
        cmd.cursorPos = cursorPos;
        cmd.cursorLetter = cursor;
    }
    AppendUiText(text, cmd);
}
void __cdecl R_AddCmdDrawTextWithEffects(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int style, const float *glow, Material *fx,
    Material *fxGlow, int birth, int letterTime, int decayStart, int decayDuration)
{
    auto cmd = MakeTextCommand(maxChars, font, x, y, xScale, yScale, rotation, color, style);
    SetDrawText2DGlowParms(&cmd, color, glow);
    SetDrawText2DPulseFXParms(&cmd, fx, fxGlow, birth, letterTime, decayStart, decayDuration);
    AppendUiText(text, cmd);
}
void __cdecl R_AddCmdDrawConsoleText(char *pool, int poolSize, int first, int count,
    Font_s *font, float x, float y, float xScale, float yScale, const float *color, int style)
{
    AppendConsoleText(pool, poolSize, first, count,
        MakeTextCommand(INT_MAX, font, x, y, xScale, yScale, 0, color, style));
}
void __cdecl R_AddCmdDrawConsoleTextSubtitle(char *pool, int poolSize, int first, int count,
    Font_s *font, float x, float y, float xScale, float yScale, const float *color,
    int style, const float *glow)
{
    auto cmd = MakeTextCommand(INT_MAX, font, x, y, xScale, yScale, 0, color, style);
    cmd.renderFlags |= 0x100;
    SetDrawText2DGlowParms(&cmd, color, glow);
    AppendConsoleText(pool, poolSize, first, count, cmd);
}
void __cdecl R_AddCmdDrawConsoleTextPulseFX(char *pool, int poolSize, int first, int count,
    Font_s *font, float x, float y, float xScale, float yScale, const float *color,
    int style, const float *glow, int birth, int letterTime, int decayStart, int decayDuration,
    Material *fx, Material *fxGlow)
{
    auto cmd = MakeTextCommand(INT_MAX, font, x, y, xScale, yScale, 0, color, style);
    SetDrawText2DGlowParms(&cmd, color, glow);
    SetDrawText2DPulseFXParms(&cmd, fx, fxGlow, birth, letterTime, decayStart, decayDuration);
    AppendConsoleText(pool, poolSize, first, count, cmd);
}
void __cdecl R_AddCmdDrawProfile() {}
void __cdecl R_AddCmdClearScreen(int, const float *, float, std::uint8_t) {}
void __cdecl R_AddCmdSaveScreenSection(float x, float y, float width, float height,
    std::uint32_t screenTimerId)
{
    iassert(screenTimerId < 4u);
    WebRendererUiBatchDesc batch{};
    batch.firstIndex = static_cast<std::uint32_t>(g_uiIndices.size());
    batch.materialName = "<save-screen>";
    batch.savedScreen = {WebRendererUiCommand::SaveScreen, screenTimerId,
        g_uiSceneTime, 0, {x, y, width, height}};
    g_uiBatches.push_back(batch);
}
void __cdecl R_AddCmdSaveScreen(std::uint32_t screenTimerId)
{
    R_AddCmdSaveScreenSection(0.0f, 0.0f, 1.0f, 1.0f, screenTimerId);
}

static void AppendShellShock(WebRendererUiCommand command, int fadeMsec,
    float x, float y, float width, float height, std::uint32_t timerId,
    const float color[4])
{
    const char *name = command == WebRendererUiCommand::ShellShockBlurred
        ? "shellshock" : "shellshock_flashed";
    Material *material = g_rendererWorldReady
        ? DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, name).material : nullptr;
    const auto previousCount = g_uiBatches.size();
    // Native RB_BlendSavedScreen* draws at (0,0), sampling the normalized view
    // region. Framebuffer textures have the opposite vertical origin to IW3.
    AppendUiRect(0.0f, 0.0f, cls.vidConfig.displayWidth * width,
        cls.vidConfig.displayHeight * height, x, 1.0f - y,
        x + width, 1.0f - y - height, color, material);
    if (g_uiBatches.size() == previousCount) return;
    auto &batch = g_uiBatches.back();
    batch.savedScreen = {command, timerId, g_uiSceneTime, fadeMsec};
}

void __cdecl R_AddCmdBlendSavedScreenShockBlurred(int fadeMsec, float x,
    float y, float width, float height, std::uint32_t screenTimerId)
{
    iassert(screenTimerId < 4u);
    if (fadeMsec <= 0) return;
    const float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    AppendShellShock(WebRendererUiCommand::ShellShockBlurred, fadeMsec,
        x, y, width, height, screenTimerId, color);
}
void __cdecl R_AddCmdBlendSavedScreenShockFlashed(float whiteout, float screengrab,
    float x, float y, float width, float height)
{
    const auto quantize = [](float value) {
        return static_cast<std::uint8_t>(SnapFloatToInt(
            std::clamp(value, 0.0f, 1.0f) * 255.0f)) / 255.0f;
    };
    const float white = quantize(whiteout);
    const float color[4]{white, white, white, quantize(screengrab)};
    AppendShellShock(WebRendererUiCommand::ShellShockFlashed, 0,
        x, y, width, height, 0u, color);
}

#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestDisplayGamma(
    int level, float gamma, int bypass)
{
    if (level < 0 || level > 255 || !std::isfinite(gamma) || gamma < 0.5f || gamma > 3.0f)
        return -1;
    if (!r_gamma) R_RegisterDvars();
    const float previousGamma = r_gamma->current.value;
    const bool previousBypass = r_ignoreHwGamma->current.enabled;
    Dvar_SetFloat(r_gamma, gamma);
    Dvar_SetBool(r_ignoreHwGamma, bypass != 0);
    GfxGammaRamp ramp{};
    R_CalcGammaRamp(&ramp);
    const int expected = bypass ? level : 255 * ramp.entries[level] / 65535;
    Web_GetCanvasSize(&cls.vidConfig.displayWidth, &cls.vidConfig.displayHeight);
    const float color[4]{level / 255.0f, level / 255.0f, level / 255.0f, 1};
    R_BeginFrame();
    R_AddCmdDrawStretchPic(0, 0, cls.vidConfig.displayWidth, cls.vidConfig.displayHeight,
        0, 0, 1, 1, color, nullptr);
    R_EndFrame();
    const auto pixel = WebRenderer_TestDrawPixel(16, 16);
    WebRenderer_SetUiScene({});
    Dvar_SetFloat(r_gamma, previousGamma);
    Dvar_SetBool(r_ignoreHwGamma, previousBypass);
    return static_cast<double>(expected) * 4294967296.0 + pixel;
}

extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestTextDraw(
    int scenario, int time, int field, int index)
{
    static std::array<Glyph, 96> glyphs{};
    static Material base{}, glow{}, fx{};
    static MaterialTechniqueSet technique{};
    base.info.name = "synthetic-font";
    glow.info.name = "synthetic-font-glow";
    fx.info.name = "synthetic-text-fx";
    fx.techniqueSet = &technique;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        glyphs[i].letter = static_cast<std::uint16_t>(i + 32);
        glyphs[i].y0 = -10;
        glyphs[i].dx = 8;
        glyphs[i].pixelWidth = 6;
        glyphs[i].pixelHeight = 10;
        glyphs[i].s1 = glyphs[i].t1 = 1;
    }
    glyphs['o' - 32].dx = 12;
    Font_s font{"synthetic", 12, 96, &base, &glow, glyphs.data()};
    const float white[4]{1, 1, 1, 1}, redGlow[4]{1, 0, 0, 1};
    Web_GetCanvasSize(&cls.vidConfig.displayWidth, &cls.vidConfig.displayHeight);
    R_BeginFrame();
    g_uiSceneTime = time;
    if (field == 100) {
        const float black[4]{0, 0, 0, 1};
        R_AddCmdDrawStretchPic(0, 0, cls.vidConfig.displayWidth, cls.vidConfig.displayHeight,
            0, 0, 1, 1, black, nullptr);
    }
    char pool[8]{'A', 'B', 'C', 'x', 'x', 'x', '^', '2'};
    if (scenario == 4)
        R_AddCmdDrawTextSubtitle("^1A^2B^7C", 99, &font, 20, 30, 1, 1, 0, white, 0, redGlow, false);
    else if (scenario == 5)
        R_AddCmdDrawTextWithEffects("ABCD", 99, &font, 20, 30, 1, 1, 0, white, 0,
            nullptr, &fx, &fx, 100, 100, 1000, 1000);
    else if (scenario == 6)
        R_AddCmdDrawConsoleText(pool, 8, 6, 5, &font, 20, 30, 1, 1, white, 0);
    else if (scenario == 7)
        R_AddCmdDrawConsoleTextSubtitle(pool, 8, 6, 5, &font, 20, 30, 1, 1, white, 0, redGlow);
    else if (scenario == 8)
        R_AddCmdDrawConsoleTextPulseFX(pool, 8, 6, 5, &font, 20, 30, 1, 1, white, 0,
            nullptr, 100, 100, 1000, 1000, &fx, &fx);
    else
        R_AddCmdDrawText("AB", 99, &font, 20, 30, 1, 1, scenario == 9 ? 90 : 0,
            white, scenario == 1 ? 3 : scenario == 2 ? 6 : scenario == 3 ? 128 : 0);
    double result = -1;
    if (field == 0) result = g_uiBatches.size();
    else if (index >= 0 && static_cast<std::size_t>(index) < g_uiBatches.size()) {
        const auto &batch = g_uiBatches[index];
        const auto &vertex = g_uiVertices[index * 4];
        if (field == 1) result = (vertex.position[0] + 1) * cls.vidConfig.displayWidth / 2;
        if (field == 2) result = (1 - vertex.position[1]) * cls.vidConfig.displayHeight / 2;
        if (field >= 3 && field <= 6) result = std::round(batch.color[field - 3] * 255);
    }
    R_EndFrame();
    if (field == 100)
        result = WebRenderer_TestDrawPixel(21, cls.vidConfig.displayHeight - 22);
    WebRenderer_SetUiScene({});
    return result;
}

extern "C" EMSCRIPTEN_KEEPALIVE std::uint32_t KisakWeb_TestSavedScreen(
    int action, int time, int fadeMsec, float whiteout, float screengrab, int bottom)
{
    Web_GetCanvasSize(&cls.vidConfig.displayWidth, &cls.vidConfig.displayHeight);
    const float width = cls.vidConfig.displayWidth;
    const float height = cls.vidConfig.displayHeight;
    const float red[4]{1, 0, 0, 1}, green[4]{0, 1, 0, 1};
    const float blue[4]{0, 0, 1, 1}, black[4]{0, 0, 0, 1};
    const float yellow[4]{1, 1, 0, 1};
    R_BeginFrame();
    g_uiSceneTime = time;
    if (action == 0)
    {
        R_AddCmdDrawStretchPic(0, 0, width, height / 2, 0, 0, 1, 1, red, nullptr);
        R_AddCmdDrawStretchPic(0, height / 2, width, height / 2, 0, 0, 1, 1, green, nullptr);
        R_AddCmdSaveScreen(0u);
        R_AddCmdDrawStretchPic(0, 0, width, height, 0, 0, 1, 1, blue, nullptr);
    }
    else if (action == 3)
    {
        R_AddCmdDrawStretchPic(0, 0, width, height, 0, 0, 1, 1, yellow, nullptr);
        R_AddCmdSaveScreenSection(0, 0, 1, 0.5f, 1u);
        R_AddCmdDrawStretchPic(0, 0, width, height, 0, 0, 1, 1, blue, nullptr);
    }
    else if (action == 4)
        R_AddCmdSaveScreen(0u); // No geometry: capture is itself an ordered command.
    else
    {
        R_AddCmdDrawStretchPic(0, 0, width, height, 0, 0, 1, 1,
            action == 1 ? black : blue, nullptr);
        if (action == 1 || action == 6)
            R_AddCmdBlendSavedScreenShockFlashed(whiteout, screengrab, 0, 0, 1, 1);
        else
        {
            R_AddCmdBlendSavedScreenShockBlurred(fadeMsec, 0, 0, 1, 1, 0u);
            if (action == 5) R_AddCmdSaveScreen(0u);
        }
    }
    R_EndFrame();
    const auto pixel = WebRenderer_TestDrawPixel(static_cast<int>(width / 4),
        static_cast<int>(height * (bottom ? 0.25f : 0.75f)));
    WebRenderer_SetUiScene({});
    return pixel;
}
#endif

std::uint32_t __cdecl R_GetLocalClientNum() { return 0; }
void __cdecl R_ClearScene(std::uint32_t)
{
    g_addedLightCount = 0;
    g_dynamicSpotLightNearPlaneOffset = 0.0f;
    g_dobjSubmissionCount = 0u;
    g_brushModelSubmissionCount = 0u;
    WebRenderer_ClearFxModelSubmissions(&g_fxModelSubmissionCount);
    WebRenderer_ClearParticleCloudSubmissions(&g_particleCloudSubmissionCount);
}

void R_ComErrorCleanup()
{
    iassert(Sys_IsMainThread());
    // Discard the interrupted frontend lists. WebGL submission is synchronous
    // in this Worker; there is no D3D BeginScene or queued render thread.
    R_BeginFrame();
    R_ClearScene(0);
}

void __cdecl R_InitSceneData(int localClientNum)
{
    iassert(localClientNum == 0 && g_rendererWorldReady);
    // Native clears both per-cell scene-entity banks for this client before
    // cgame relinks the current snapshot. Keep those canonical world-owned
    // bits even though the WebGL2 frontend does not allocate GfxScene buffers.
    R_ClearSceneEntityCellLinks(s_world,
        static_cast<unsigned>(localClientNum), Web_RendererEntityCount());
    g_dobjCellLinkValid.fill(0u);
    g_brushCellLinkValid.fill(0u);
    g_dobjCellLinkRadius.fill(0.0f);
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

static GfxLight *AllocateSceneLight(const float *origin, float radius,
    const float color[3])
{
    if (!g_rendererWorldReady || !origin || !std::isfinite(radius) || radius <= 0) return nullptr;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(origin[i]) || !std::isfinite(color[i])) return nullptr;
    if (g_addedLightCount == g_addedLights.size())
    {
        R_WarnOncePerFrame(R_WARN_MAX_DLIGHTS);
        return nullptr;
    }
    return &g_addedLights[g_addedLightCount++];
}

void __cdecl R_AddOmniLightToScene(const float *origin, float radius, float r, float g, float b)
{
    const float color[3]{r, g, b};
    if (auto *light = AllocateSceneLight(origin, radius, color))
        kisak::dynamic_lights::SetOmni(*light,
            DB_FindXAssetHeader(ASSET_TYPE_LIGHT_DEF, "light_dynamic").lightDef,
            origin, radius, color);
}

void __cdecl R_AddSpotLightToScene(const float *origin, const float *direction,
    float radius, float r, float g, float b)
{
    if (!direction || !std::isfinite(direction[0]) ||
        !std::isfinite(direction[1]) || !std::isfinite(direction[2])) return;
    const float color[3]{r, g, b};
    auto *light = AllocateSceneLight(origin, radius, color);
    if (!light) return;
    const float start = r_spotLightStartRadius->current.value;
    if (start >= r_spotLightEndRadius->current.value)
        Dvar_SetFloat(r_spotLightEndRadius, start + 0.1f);
    if (r_spotLightEndRadius->current.value >= start + radius)
        Dvar_SetFloat(r_spotLightEndRadius, start + radius - 0.1f);
    const float nearPlaneOffset = kisak::dynamic_lights::SetSpot(*light,
        DB_FindXAssetHeader(ASSET_TYPE_LIGHT_DEF, "light_dynamic").lightDef,
        origin, direction, radius, color, start, r_spotLightEndRadius->current.value,
        r_spotLightFovInnerFraction->current.value, r_spotLightBrightness->current.value,
        r_spotLightShadows->current.enabled);
    if (light == g_addedLights.data())
        g_dynamicSpotLightNearPlaneOffset = nearPlaneOffset;
    iassert(light->cosHalfFovInner > light->cosHalfFovOuter);
}
void __cdecl R_SetLodOrigin(const refdef_s *) {}
void __cdecl R_RenderScene(const refdef_s *refdef)
{
#if KISAK_WEB_DIAGNOSTICS
    refdef_s testView;
    if (g_testDynEntityCamera >= 0 && g_testDynEntityCamera <
        DynEnt_GetEntityCount(DYNENT_COLL_CLIENT_MODEL))
    {
        testView = *refdef;
        const auto *pose = DynEnt_GetClientPose(g_testDynEntityCamera, DYNENT_DRAW_MODEL);
        std::copy_n(pose->pose.origin, 3u, testView.vieworg);
        AxisClear(testView.viewaxis);
        // Stay inside the linked bounds and face the selected model's centre.
        testView.vieworg[0] -= std::min(16.0f, pose->radius * 0.25f);
        refdef = &testView;
    }
#endif
    g_uiSceneTime = refdef->time;
#if KISAK_WEB_DIAGNOSTICS
    WebFrameProfileSample *const sceneProfile = WebFrameProfile_Current();
    const double sceneProfileStarted = sceneProfile
        ? WebFrameProfile_Now() : 0.0;
#endif
    iassert(refdef->tanHalfFovX > 0.0f);
    iassert(refdef->tanHalfFovY > 0.0f);
    iassert(refdef->height > 0u);
    iassert(refdef->width > 0u);
    iassert(refdef->localClientNum == 0);
    if (!g_rendererWorldReady || !s_world.name)
        Com_Error(ERR_DROP, "R_RenderScene: NULL worldmodel");

    // Native R_SetTestLods runs at the render-command boundary before every
    // scene. Preserve developer overrides as well as authored XModel ranges.
    if (r_forceLod->current.integer == r_forceLod->reset.integer)
    {
        XModelSetTestLods(0u, r_highLodDist->current.value);
        XModelSetTestLods(1u, r_mediumLodDist->current.value);
        XModelSetTestLods(2u, r_lowLodDist->current.value);
        XModelSetTestLods(3u, r_lowestLodDist->current.value);
    }
    else
    {
        for (std::uint32_t lod = 0u; lod < MAX_LODS; ++lod)
            XModelSetTestLods(lod,
                lod == static_cast<std::uint32_t>(
                    r_forceLod->current.integer) ? 0.0f : 0.001f);
    }
    WebRendererLodParms lodParms{};
    if (!WebRenderer_BuildLodParms(
            refdef->vieworg,
            refdef->tanHalfFovY,
            r_lodScaleRigid->current.value,
            r_lodBiasRigid->current.value,
            r_lodScaleSkinned->current.value,
            r_lodBiasSkinned->current.value,
            lodParms))
    {
        Com_Error(ERR_DROP, "R_RenderScene: invalid canonical LOD parameters");
        return;
    }
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
    GfxGlow glow = refdef->glow;
    if (r_glowUseTweaks && r_glowUseTweaks->current.enabled)
    {
        glow.enabled = r_glowTweakEnable->current.enabled;
        glow.radius = r_glowTweakRadius->current.value;
        glow.bloomIntensity = r_glowTweakBloomIntensity->current.value;
        glow.bloomCutoff = r_glowTweakBloomCutoff->current.value;
        glow.bloomDesaturation =
            r_glowTweakBloomDesaturation->current.value;
    }
    const bool glowAllowed =
        (!r_glow_allowed || r_glow_allowed->current.enabled) ||
        (r_glow_allowed_script_forced &&
            r_glow_allowed_script_forced->current.enabled);
    WebRendererGlowSettings glowSettings{};
    glowSettings.enabled = glowAllowed && glow.enabled &&
        (!r_fullbright || !r_fullbright->current.enabled) &&
        (!r_glow || r_glow->current.enabled) &&
        glow.bloomIntensity > 0.0f && glow.radius > 0.0f;
    glowSettings.bloomCutoff = glow.bloomCutoff;
    glowSettings.bloomDesaturation = glow.bloomDesaturation;
    glowSettings.bloomIntensity = glow.bloomIntensity;
    glowSettings.radius = glow.radius;
    WebRendererGlowConstants glowConstants{};
    if (!WebRenderer_CalculateGlowConstants(glowSettings, glowConstants))
    {
        Com_Error(ERR_DROP,
            "R_RenderScene: invalid canonical glow constants");
        return;
    }
    view.glowEnabled = glowConstants.enabled;
    view.glowBloomCutoff = glowConstants.bloomCutoff;
    view.glowBloomCutoffRescale =
        glowConstants.bloomCutoffRescale;
    view.glowBloomDesaturation = glowConstants.bloomDesaturation;
    view.glowBloomIntensity = glowConstants.bloomIntensity;
    view.glowRadius = glowConstants.radius;
    GfxDepthOfField depthOfField{};
    if (r_dof_tweak && r_dof_tweak->current.enabled)
    {
        depthOfField.viewModelStart = r_dof_viewModelStart->current.value;
        depthOfField.viewModelEnd = r_dof_viewModelEnd->current.value;
        depthOfField.nearStart = r_dof_nearStart->current.value;
        depthOfField.nearEnd = r_dof_nearEnd->current.value;
        depthOfField.farStart = r_dof_farStart->current.value;
        depthOfField.farEnd = r_dof_farEnd->current.value;
        depthOfField.nearBlur = r_dof_nearBlur->current.value;
        depthOfField.farBlur = r_dof_farBlur->current.value;
    }
    else if (!r_dof_enable || r_dof_enable->current.enabled)
    {
        depthOfField = refdef->dof;
    }
    view.depthOfField.viewModelStart = depthOfField.viewModelStart;
    view.depthOfField.viewModelEnd = depthOfField.viewModelEnd;
    view.depthOfField.nearStart = depthOfField.nearStart;
    view.depthOfField.nearEnd = depthOfField.nearEnd;
    view.depthOfField.farStart = depthOfField.farStart;
    view.depthOfField.farEnd = depthOfField.farEnd;
    view.depthOfField.nearBlur = depthOfField.nearBlur;
    view.depthOfField.farBlur = depthOfField.farBlur;
    view.depthOfField.enabled =
        depthOfField.viewModelEnd > depthOfField.viewModelStart + 1.0f ||
        depthOfField.nearEnd > depthOfField.nearStart + 1.0f ||
        (depthOfField.farEnd > depthOfField.farStart + 1.0f &&
            depthOfField.farBlur > 0.0f);
    if (!WebRenderer_ValidateDepthOfFieldSettings(view.depthOfField))
    {
        Com_Error(ERR_DROP,
            "R_RenderScene: invalid canonical depth-of-field settings");
        return;
    }
    view.depthHackZNear = std::max(0.01f,
        r_znear_depthhack ? r_znear_depthhack->current.value : 0.1f);
    view.blurRadius = refdef->blurRadius;
    if (!g_sceneBlurReported && refdef->blurRadius > 0.0f)
    {
        g_sceneBlurReported = true;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical resolved-scene blur active: "
            "radius=%.6f before 2D composition.\n",
            refdef->blurRadius);
    }
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
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical glow filter: active=%d "
            "cutoff=%.6f rescale=%.6f desaturation=%.6f "
            "intensity=%.6f radius=%.6f, quarter-resolution Gaussian "
            "before HUD.\n",
            view.glowEnabled, view.glowBloomCutoff,
            view.glowBloomCutoffRescale,
            view.glowBloomDesaturation,
            view.glowBloomIntensity, view.glowRadius);
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical depth of field: active=%d "
            "viewmodel=(%.6f %.6f) near=(%.6f %.6f %.6f) "
            "far=(%.6f %.6f %.6f), znear=(%.6f %.6f).\n",
            view.depthOfField.enabled,
            view.depthOfField.viewModelStart,
            view.depthOfField.viewModelEnd,
            view.depthOfField.nearStart,
            view.depthOfField.nearEnd,
            view.depthOfField.nearBlur,
            view.depthOfField.farStart,
            view.depthOfField.farEnd,
            view.depthOfField.farBlur,
            view.zNear, view.depthHackZNear);
    }
    view.localClientNum = refdef->localClientNum;
    view.worldName = s_world.name;
    std::vector<WebRendererPrimaryLightDesc> framePrimaryLights;
    std::uint32_t sunPrimaryLightIndex = 0u;
    if (!BuildRendererPrimaryLights(
            refdef->primaryLights, framePrimaryLights,
            sunPrimaryLightIndex))
    {
        Com_Error(ERR_DROP,
            "R_RenderScene: invalid canonical frame primary lights");
        return;
    }
    view.primaryLights = framePrimaryLights.data();
    view.primaryLightCount = static_cast<std::uint32_t>(
        framePrimaryLights.size());
    view.dynamicShadowVisibility =
        DynamicShadowVisibleToPrimaryLight;
    view.sunShadowEnabled = sm_enable && sm_enable->current.enabled &&
        sunPrimaryLightIndex < framePrimaryLights.size() &&
        framePrimaryLights[sunPrimaryLightIndex].type == 1u;
    if (view.sunShadowEnabled)
    {
        const WebRendererPrimaryLightDesc &sun =
            framePrimaryLights[sunPrimaryLightIndex];
        std::copy_n(sun.direction, 3u, view.sunDirection);
        std::copy_n(sun.color, 3u, view.sunColor);
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
        view.depthHackZNear);
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

#if KISAK_WEB_DIAGNOSTICS
    const double cameraVisibilityStarted =
        sceneProfile ? WebFrameProfile_Now() : 0.0;
#endif
    // Native establishes the camera DPVS before filtering any dynamic scene
    // family. Do the same here so canonical sphere/box tests can reject work
    // before pose skinning, material expansion and GPU upload.
    GfxViewParms cameraParms{};
    std::memcpy(cameraParms.viewMatrix.m, viewMatrix, sizeof(viewMatrix));
    std::memcpy(cameraParms.projectionMatrix.m, projectionMatrix,
        sizeof(projectionMatrix));
    std::memcpy(cameraParms.viewProjectionMatrix.m,
        d3dViewProjectionMatrix, sizeof(d3dViewProjectionMatrix));
    MatrixInverse44(cameraParms.viewProjectionMatrix.m,
        cameraParms.inverseViewProjectionMatrix.m);
    std::copy_n(view.viewOrigin, 3u, cameraParms.origin);
    cameraParms.origin[3] = 1.0f;
    std::memcpy(cameraParms.axis, view.viewAxis, sizeof(cameraParms.axis));
    for (unsigned kind = 0; kind < 2u; ++kind)
    {
        const unsigned count = DynEnt_GetEntityCount(static_cast<DynEntityCollType>(kind));
        const unsigned words = s_world.dpvsDyn.dynEntClientWordCount[kind];
        auto *visibility = s_world.dpvsDyn.dynEntVisData[kind][0];
        if (!R_DynEntityCellLayoutAvailable(s_world, kind) ||
            count != s_world.dpvsDyn.dynEntClientCount[kind] ||
            (words != 0u && !visibility))
            Com_Error(ERR_DROP, "R_RenderScene: invalid DynEntity camera storage");
        if (words != 0u) std::memset(visibility, 0, 32u * words);
    }
    PortalSceneAdmissionState portalAdmission{};
    portalAdmission.world = &s_world;
    portalAdmission.entityCount = Web_RendererEntityCount();
    portalAdmission.localClientNum = view.localClientNum;
    portalAdmission.lodParms = &lodParms;
    portalAdmission.viewOffset = refdef->viewOffset;
    view.staticModelVisibilityComputed = R_ComputeStaticCameraVisibility(
        s_world, g_cameraDpvs, cameraParms, view.localClientNum,
        static_cast<float>(R_GetFarPlaneDist()), true,
        ObservePortalSceneCell, &portalAdmission);
    view.staticModelVisibility = s_world.dpvs.smodelVisData[0];
    view.staticModelVisibilityCount = s_world.dpvs.smodelCount;
    view.worldSurfaceVisibilityComputed =
        view.staticModelVisibilityComputed;
    view.worldSurfaceVisibility = s_world.dpvs.surfaceVisData[0];
    view.worldSurfaceVisibilityCount = s_world.dpvs.staticSurfaceCount;
    if (!view.staticModelVisibilityComputed &&
        (s_world.dpvs.smodelCount || s_world.dpvs.staticSurfaceCount))
    {
        Com_Error(ERR_DROP,
            "R_RenderScene: canonical static camera DPVS unavailable");
        return;
    }
    if ((!view.staticModelVisibilityComputed || !portalAdmission.valid) &&
        (s_world.dpvsDyn.dynEntClientCount[0] || s_world.dpvsDyn.dynEntClientCount[1]))
        Com_Error(ERR_DROP, "R_RenderScene: DynEntity portal walk unavailable");
    const DpvsView &camera =
        g_cameraDpvs.views[view.localClientNum][0];
#if KISAK_WEB_DIAGNOSTICS
    if (!g_gameDrivenFrameReported)
    {
        std::uint32_t linkedDObjs = 0u, admittedDObjs = 0u;
        std::uint32_t linkedBrushes = 0u, admittedBrushes = 0u;
        for (std::uint32_t i = 0; i < g_dobjSubmissionCount; ++i)
        {
            const auto entity = g_dobjSubmissions[i].entityNumber;
            linkedDObjs += entity < g_dobjCellLinkValid.size() &&
                g_dobjCellLinkValid[entity];
            admittedDObjs += portalAdmission.dobjVisible[i] != 0u;
        }
        for (std::uint32_t i = 0; i < g_brushModelSubmissionCount; ++i)
        {
            const auto entity = g_brushModelSubmissions[i].entityNumber;
            linkedBrushes += entity < g_brushCellLinkValid.size() &&
                g_brushCellLinkValid[entity];
            admittedBrushes += portalAdmission.brushVisible[i] != 0u;
        }
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical portal scene admission: valid=%u "
            "DObj linked=%u admitted=%u brush linked=%u admitted=%u.\n",
            portalAdmission.valid ? 1u : 0u, linkedDObjs, admittedDObjs,
            linkedBrushes, admittedBrushes);
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical DObj post-pose admission: tested=%u "
            "plane-rejected=%u cell-rejected=%u.\n",
            portalAdmission.posedTests, portalAdmission.posedPlaneRejects,
            portalAdmission.posedCellRejects);
        unsigned admittedDynEntities[2]{};
        for (unsigned kind = 0; kind < 2u; ++kind)
            for (unsigned id = 0; id < s_world.dpvsDyn.dynEntClientCount[kind]; ++id)
                admittedDynEntities[kind] += s_world.dpvsDyn.dynEntVisData[kind][0][id] != 0u;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical DynEntity portal admission: models=%u admitted=%u "
            "brushes=%u admitted=%u.\n",
            s_world.dpvsDyn.dynEntClientCount[0], admittedDynEntities[0],
            s_world.dpvsDyn.dynEntClientCount[1], admittedDynEntities[1]);
    }
    if (sceneProfile)
        sceneProfile->sceneCameraVisibilityMs +=
            WebFrameProfile_Now() - cameraVisibilityStarted;
#endif

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
            "[kisakcod-web] Canonical world sun: valid=%u "
            "fxDirection=(%.6f %.6f %.6f) sprite='%s' flare='%s'.\n",
            s_world.sun.hasValidData ? 1u : 0u,
            s_world.sun.sunFxPosition[0],
            s_world.sun.sunFxPosition[1],
            s_world.sun.sunFxPosition[2],
            s_world.sun.spriteMaterial && s_world.sun.spriteMaterial->info.name
                ? s_world.sun.spriteMaterial->info.name : "<none>",
            s_world.sun.flareMaterial && s_world.sun.flareMaterial->info.name
                ? s_world.sun.flareMaterial->info.name : "<none>");
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
        const WebRendererWorldLightTechniqueContext lightContext{
            framePrimaryLights.data(),
            static_cast<std::uint32_t>(framePrimaryLights.size()),
            sunPrimaryLightIndex,
            view.sunShadowEnabled,
        };
        WebRendererWorldSceneCommand command;
        const WebRendererWorldSceneResult build =
            WebRenderer_BuildWorldSceneCommand(
                s_world, view, command, &lightContext);
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
            command.sunPrimaryLightIndex = sunPrimaryLightIndex;
            for (WebRendererWorldBatchDesc &batch : command.batches)
                ResolveRendererBatchImages(batch);
            std::array<std::uint32_t, 37u> techniqueBatchCounts{};
            std::array<std::uint32_t, 37u> techniqueSurfaceCounts{};
            std::uint32_t localLightBatchCount = 0u;
            std::uint32_t localLightSurfaceCount = 0u;
            std::uint32_t localLightFallbackBatchCount = 0u;
            std::uint32_t localLightFallbackSurfaceCount = 0u;
            std::uint32_t localLightSkippedBatchCount = 0u;
            std::uint32_t localLightSkippedSurfaceCount = 0u;
            std::vector<const Material *> loggedLocalMaterials;
            for (const WebRendererWorldBatchDesc &batch : command.batches)
            {
                if (batch.techniqueType < techniqueBatchCounts.size())
                {
                    ++techniqueBatchCounts[batch.techniqueType];
                    techniqueSurfaceCounts[batch.techniqueType] +=
                        batch.surfaceCount;
                }
                const bool localLight = batch.primaryLightIndex <
                        framePrimaryLights.size() &&
                    (framePrimaryLights[batch.primaryLightIndex].type == 2u ||
                        framePrimaryLights[batch.primaryLightIndex].type == 3u);
                if (!localLight) continue;
                ++localLightBatchCount;
                localLightSurfaceCount += batch.surfaceCount;
                if (batch.technique ==
                    WebRendererWorldTechnique::BackendFallback)
                {
                    ++localLightFallbackBatchCount;
                    localLightFallbackSurfaceCount += batch.surfaceCount;
                }
                if (WebRenderer_SkipsNativeDraw(batch.technique))
                {
                    ++localLightSkippedBatchCount;
                    localLightSkippedSurfaceCount += batch.surfaceCount;
                }
                if (std::find(loggedLocalMaterials.begin(),
                        loggedLocalMaterials.end(), batch.materialIdentity) ==
                    loggedLocalMaterials.end())
                {
                    loggedLocalMaterials.push_back(batch.materialIdentity);
                    const MaterialTechniqueSet *techniqueSet =
                        batch.materialIdentity
                        ? batch.materialIdentity->techniqueSet : nullptr;
                    if (techniqueSet && techniqueSet->remappedTechniqueSet)
                        techniqueSet = techniqueSet->remappedTechniqueSet;
                    const bool hasSpotTechnique = techniqueSet &&
                        techniqueSet->techniques[10u] != nullptr;
                    const std::uint8_t spotStateEntry = batch.materialIdentity
                        ? batch.materialIdentity->stateBitsEntry[10u] : 0xffu;
                    Web_Log(WebLogLevel::Info,
                        "[kisakcod-web] Canonical local-light material: "
                        "material='%s' light=%u technique='%s' type=%u "
                        "shader='%s' samplerFlags=0x%02x portable=%u "
                        "spotTechnique=%u stateEntry=%u.\n",
                        batch.materialName,
                        static_cast<unsigned int>(batch.primaryLightIndex),
                        batch.techniqueName,
                        static_cast<unsigned int>(batch.techniqueType),
                        batch.pixelShaderName,
                        static_cast<unsigned int>(batch.customSamplerFlags),
                        static_cast<unsigned int>(batch.technique),
                        hasSpotTechnique ? 1u : 0u,
                        static_cast<unsigned int>(spotStateEntry));
                }
            }
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical local-light technique coverage: "
                "%u batches/%u surfaces; exact spot=%u/%u, exact omni=%u/%u, "
                "native-skip=%u/%u, fallback=%u/%u, unique materials=%zu.\n",
                localLightBatchCount, localLightSurfaceCount,
                techniqueBatchCounts[10u], techniqueSurfaceCounts[10u],
                techniqueBatchCounts[12u], techniqueSurfaceCounts[12u],
                localLightSkippedBatchCount, localLightSkippedSurfaceCount,
                localLightFallbackBatchCount, localLightFallbackSurfaceCount,
                loggedLocalMaterials.size());
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
                 index < framePrimaryLights.size(); ++index)
            {
                const WebRendererPrimaryLightDesc &light =
                    framePrimaryLights[index];
                if (light.type == 2u) ++spotLightCount;
                if (light.type == 3u) ++omniLightCount;
                if (light.falloffScale > 0.0f)
                    ++resolvedAttenuationCount;
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
                        "falloff=(%.6f %.6f).\n",
                        index, static_cast<unsigned int>(light.type),
                        primaryLightSurfaceCounts[index],
                        light.origin[0], light.origin[1], light.origin[2],
                        light.radius, light.color[0], light.color[1],
                        light.color[2], light.falloffScale,
                        light.falloffShift);
                }
            }
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Canonical primary-light inventory: "
                "%zu total, sun=%u, spot=%u, omni=%u, "
                "%u attenuation images, %u local lights assigned to "
                "%u world surfaces.\n",
                framePrimaryLights.size(),
                command.sunPrimaryLightIndex, spotLightCount,
                omniLightCount, resolvedAttenuationCount,
                assignedLocalLightCount, assignedLocalSurfaceCount);
            WebRendererWorldSurfaceDesc surface{
                command.vertices.data(),
                static_cast<std::uint32_t>(command.vertices.size()),
                command.indices.data(),
                static_cast<std::uint32_t>(command.indices.size()),
                command.batches.data(),
                static_cast<std::uint32_t>(command.batches.size()),
                nullptr,
                framePrimaryLights.data(),
                static_cast<std::uint32_t>(framePrimaryLights.size()),
                command.sunPrimaryLightIndex,
                command.spotShadowCasters.empty()
                    ? nullptr : command.spotShadowCasters.data(),
                static_cast<std::uint32_t>(
                    command.spotShadowCasters.size()),
                command.spotShadowStaticModels.empty()
                    ? nullptr : command.spotShadowStaticModels.data(),
                static_cast<std::uint32_t>(
                    command.spotShadowStaticModels.size()),
                command.surfaceRanges.data(),
                static_cast<std::uint32_t>(command.surfaceRanges.size()),
                s_world.dpvs.staticSurfaceCount,
                command.outdoorImage,
            };
            std::memcpy(surface.outdoorLookupMatrix,
                command.outdoorLookupMatrix,
                sizeof(surface.outdoorLookupMatrix));
            const WebRendererSurfaceResult submission =
                WebRenderer_SetWorldSurface(surface);
            if (submission != WebRendererSurfaceResult::Success)
            {
                Web_Log(WebLogLevel::Error,
                    "[kisakcod-web] Canonical world submission failed: %s.\n",
                    WebRenderer_SurfaceResultString(submission));
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
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Building the canonical static XModel command "
            "with every authored LOD.\n");
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
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Built canonical static XModel LOD geometry "
                "(%zu batches, %zu vertices, %zu indices, %zu instances); "
                "resolving material images.\n",
                command.batches.size(), command.vertices.size(),
                command.indices.size(), command.instances.size());
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
                command.shadowBounds.data(),
                static_cast<std::uint32_t>(command.shadowBounds.size()),
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

    // Native scene-entity DPVS rejects the linked sphere against the camera
    // planes before updating bounds and skinning. Preserve malformed records
    // for the existing validation path, but compact ordinary culled records
    // out of the per-frame command.
    std::uint32_t visibleDObjCount = 0u;
    for (std::uint32_t index = 0u; index < g_dobjSubmissionCount; ++index)
    {
        const WebRendererDObjSubmission &submission =
            g_dobjSubmissions[index];
        bool culled = false;
        if (submission.obj && submission.pose)
        {
            const float radius = static_cast<float>(
                DObjGetRadius(submission.obj));
            if (std::isfinite(radius) && radius >= 0.0f)
            {
                culled = kisak::dpvs::CullSphere(
                    submission.pose->origin, radius,
                    camera.frustumPlanes, camera.frustumPlaneCount);
                const bool linkedPortalResult =
                    view.staticModelVisibilityComputed &&
                    portalAdmission.valid &&
                    submission.entityNumber < g_dobjCellLinkValid.size() &&
                    g_dobjCellLinkValid[submission.entityNumber];
                culled = culled || (linkedPortalResult
                    ? portalAdmission.dobjVisible[index] == 0u
                    : !R_SphereTouchesVisibleCell(
                        s_world, g_cameraDpvs.cellVisibleBits,
                        submission.pose->origin, radius));
            }
        }
        if (!culled)
            g_dobjSubmissions[visibleDObjCount++] = submission;
    }
    g_dobjSubmissionCount = visibleDObjCount;

    // Native scene brushes use the second sceneEntCellBits bank and the same
    // portal-path inner planes. Compact only records whose canonical link was
    // rebuilt successfully; malformed or unlinked records retain the existing
    // conservative BSP-overlap fallback.
    std::uint32_t visibleBrushCount = 0u;
    for (std::uint32_t index = 0u;
         index < g_brushModelSubmissionCount; ++index)
    {
        const WebRendererBrushModelSubmission &submission =
            g_brushModelSubmissions[index];
        bool culled = false;
        if (submission.model)
        {
            culled = kisak::dpvs::CullBox(
                submission.model->writable.mins,
                submission.model->writable.maxs,
                camera.frustumPlanes, camera.frustumPlaneCount);
            const bool linkedPortalResult =
                view.staticModelVisibilityComputed &&
                portalAdmission.valid &&
                submission.entityNumber < g_brushCellLinkValid.size() &&
                g_brushCellLinkValid[submission.entityNumber];
            culled = culled || (linkedPortalResult
                ? portalAdmission.brushVisible[index] == 0u
                : !R_BoundsTouchVisibleCell(
                    s_world, g_cameraDpvs.cellVisibleBits,
                    submission.model->writable.mins,
                    submission.model->writable.maxs));
        }
        if (!culled)
            g_brushModelSubmissions[visibleBrushCount++] = submission;
    }
    g_brushModelSubmissionCount = visibleBrushCount;

    WebRendererDObjSceneCommand dynamicCommand;
#if KISAK_WEB_DIAGNOSTICS
    const double dobjBuildStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
    if (sceneProfile)
        sceneProfile->sceneSetupMs += dobjBuildStarted - sceneProfileStarted;
#endif
    const WebRendererDObjSceneResult dynamicBuild =
        WebRenderer_BuildDObjSceneCommand(
            g_dobjSubmissions.data(), g_dobjSubmissionCount,
            dynamicCommand, &lodParms, &s_world.lightGrid,
            &MODEL_LIGHTING_CALLBACKS, ResolveRendererMaterial,
            refdef->viewOffset, camera.frustumPlanes,
            camera.frustumPlaneCount);
#if KISAK_WEB_DIAGNOSTICS
    const double assemblyStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
    if (sceneProfile)
        sceneProfile->dobjBuildMs += assemblyStarted - dobjBuildStarted;
#endif
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
            if (!s_world.dpvsDyn.dynEntVisData[1][0][dynEntId]) continue;
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
            submission.shadowEntityKind =
                WebRendererShadowEntityKind::DynEntBrush;
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

#if KISAK_WEB_DIAGNOSTICS
    const double modelBuildStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
    if (sceneProfile)
        sceneProfile->sceneEffectsPrepareMs += modelBuildStarted - assemblyStarted;
#endif
    const WebRendererWorldLightTechniqueContext brushLightContext{
        framePrimaryLights.data(),
        static_cast<std::uint32_t>(framePrimaryLights.size()),
        sunPrimaryLightIndex,
        false,
    };
    const std::uint32_t brushInsertBatch = static_cast<std::uint32_t>(dynamicCommand.batches.size());
    std::vector<WebRendererBrushModelInstanceDesc> brushInstances;
    std::uint32_t brushSurfaceCount = 0u;
    std::uint32_t brushBatchCount = 0u;
    std::uint32_t brushVertexCount = 0u;
    std::uint32_t brushIndexCount = 0u;
    try
    {
        if (g_brushGeometryByModel.size() != s_world.modelCount)
            g_brushGeometryByModel.assign(s_world.modelCount, {});
        brushInstances.reserve(activeBrushModels.size());
        for (const auto &submission : activeBrushModels)
        {
            bool finitePlacement = std::all_of(std::begin(submission.origin), std::end(submission.origin),
                [](float value) { return std::isfinite(value); });
            for (const auto &axis : submission.axis)
                finitePlacement = finitePlacement && std::all_of(std::begin(axis), std::end(axis),
                    [](float value) { return std::isfinite(value); });
            if (!finitePlacement)
            {
                Com_Error(ERR_DROP, "R_RenderScene brush placement contains non-finite values");
                return;
            }
            const auto address = reinterpret_cast<std::uintptr_t>(submission.model);
            const auto base = reinterpret_cast<std::uintptr_t>(s_world.models);
            if (!submission.model || !s_world.models || address < base ||
                (address - base) % sizeof(GfxBrushModel) != 0u ||
                (address - base) / sizeof(GfxBrushModel) >= s_world.modelCount)
            {
                Com_Error(ERR_DROP, "R_RenderScene brush model is outside the canonical world");
                return;
            }
            auto &resource = g_brushGeometryByModel[(address - base) / sizeof(GfxBrushModel)];
            if (resource.handle == UINT32_MAX)
            {
                // Native brush geometry is immutable between world publications.
                // Technique remaps run in R_LoadWorld, which clears this table;
                // primary-light types are stable (enforced by SetSceneView).
                // Retain it at identity placement; current entity transforms stay
                // in the per-frame command and never become cached engine state.
                WebRendererBrushModelSubmission identity{};
                identity.model = submission.model;
                for (std::size_t axis = 0u; axis < 3u; ++axis) identity.axis[axis][axis] = 1.0f;
                WebRendererBrushModelSceneCommand geometry;
                const auto build = WebRenderer_BuildBrushModelSceneCommand(
                    s_world, &identity, 1u, geometry, &brushLightContext);
                if (build == WebRendererWorldSceneResult::NoVisibleSurface)
                {
                    resource.handle = UINT32_MAX - 1u;
                    continue;
                }
                if (build != WebRendererWorldSceneResult::Success)
                {
                    Com_Error(ERR_DROP, "R_RenderScene brush geometry: %s",
                        WebRenderer_WorldSceneResultString(build));
                    return;
                }
                for (auto &batch : geometry.batches) ResolveRendererBatchImages(batch);
                const WebRendererWorldSurfaceDesc descriptor{
                    geometry.vertices.data(), static_cast<std::uint32_t>(geometry.vertices.size()),
                    geometry.indices.data(), static_cast<std::uint32_t>(geometry.indices.size()),
                    geometry.batches.data(), static_cast<std::uint32_t>(geometry.batches.size()),
                };
                const auto retain = WebRenderer_RetainBrushModelGeometry(descriptor, resource.handle);
                if (retain != WebRendererSurfaceResult::Success)
                {
                    Com_Error(ERR_DROP, "R_RenderScene retain brush geometry: %s",
                        WebRenderer_SurfaceResultString(retain));
                    return;
                }
                resource.surfaces = geometry.surfaceCount;
                resource.batches = static_cast<std::uint32_t>(geometry.batches.size());
                resource.vertices = descriptor.vertexCount;
                resource.indices = descriptor.indexCount;
            }
            if (resource.handle == UINT32_MAX - 1u) continue;
            // Retention removes physical copies, not logical scene occupancy.
            // Later optional effects must see the same remaining budget.
            if (WebRenderer_ValidateFxModelAppendCounts(
                    resource.vertices, resource.indices, resource.batches, resource.surfaces,
                    dynamicCommand.vertices.size() + brushVertexCount,
                    dynamicCommand.indices.size() + brushIndexCount,
                    dynamicCommand.batches.size() + brushBatchCount,
                    dynamicCommand.surfaceCount + brushSurfaceCount) !=
                WebRendererFxModelAppendResult::Success)
            {
                Com_Error(ERR_DROP, "R_RenderScene brush geometry exceeds dynamic limits");
                return;
            }
            WebRendererBrushModelInstanceDesc instance{};
            instance.geometryIndex = resource.handle;
            std::memcpy(instance.axis, submission.axis, sizeof(instance.axis));
            std::memcpy(instance.origin, submission.origin, sizeof(instance.origin));
            instance.shadowEntityKind = submission.shadowEntityKind;
            instance.shadowEntityId = submission.entityNumber;
            if (!WebRenderer_CopyBrushReceiverBounds(*submission.model, instance))
            {
                Com_Error(ERR_DROP, "R_RenderScene brush receiver bounds are invalid");
                return;
            }
            if (submission.shadowEntityKind != WebRendererShadowEntityKind::DynEntBrush &&
                (kisak::dpvs::CullBox(
                    instance.receiverMins, instance.receiverMaxs,
                    camera.frustumPlanes, camera.frustumPlaneCount) ||
                !R_BoundsTouchVisibleCell(s_world,
                    g_cameraDpvs.cellVisibleBits,
                    instance.receiverMins, instance.receiverMaxs)))
                continue;
            brushInstances.push_back(instance);
            brushSurfaceCount += resource.surfaces;
            brushBatchCount += resource.batches;
            brushVertexCount += resource.vertices;
            brushIndexCount += resource.indices;
        }
    }
    catch (const std::bad_alloc &)
    {
        Com_Error(ERR_DROP, "R_RenderScene canonical brush placement allocation failed");
        return;
    }
    const bool hasBrushModels = !brushInstances.empty();

#if KISAK_WEB_DIAGNOSTICS
    if (sceneProfile)
        sceneProfile->sceneBrushBuildMs += WebFrameProfile_Now() - modelBuildStarted;
#endif
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
            if (!s_world.dpvsDyn.dynEntVisData[0][0][dynEntId]) continue;
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
            submission.shadowEntityKind =
                WebRendererShadowEntityKind::DynEntModel;
            submission.shadowEntityId = dynEntId;
            submission.dynamicEntityRadius = pose->radius;
            const int lod = WebRenderer_SelectFxModelLod(
                submission.model, submission.placement, &lodParms);
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
        const auto admission = WebRenderer_ValidateFxModelAppendCounts(
            dynamicEntityModelCommand.vertices.size(), dynamicEntityModelCommand.indices.size(),
            dynamicEntityModelCommand.batches.size(), dynamicEntityModelCommand.surfaceCount,
            dynamicCommand.vertices.size() + brushVertexCount,
            dynamicCommand.indices.size() + brushIndexCount,
            dynamicCommand.batches.size() + brushBatchCount,
            dynamicCommand.surfaceCount + brushSurfaceCount);
        const WebRendererFxModelAppendResult append =
            admission == WebRendererFxModelAppendResult::Success
            ? WebRenderer_AppendFxModelSceneCommand(
                dynamicEntityModelCommand,
                dynamicCommand.vertices,
                dynamicCommand.indices,
                dynamicCommand.batches,
                dynamicCommand.surfaceCount) : admission;
        if (append != WebRendererFxModelAppendResult::Success)
        {
            Com_Error(ERR_DROP,
                "R_RenderScene DynEntity model append failed");
            return;
        }
#if KISAK_WEB_DIAGNOSTICS
        if (g_testDynEntityCamera >= 0)
            g_testDynEntityCameraRendered = std::any_of(
                dynamicEntityModelCommand.batches.begin(), dynamicEntityModelCommand.batches.end(),
                [](const auto &batch) {
                    return batch.shadowEntityId == static_cast<unsigned>(g_testDynEntityCamera);
                });
#endif
    }

#if KISAK_WEB_DIAGNOSTICS
    const double effectsStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
#endif
    // Native R_RenderScene builds DynEntity draws from the same poses used
    // for camera visibility, then advances physics before generating marks.
    // All draw placements and receiver bounds above are already copied.
    FX_RunPhysics(refdef->localClientNum);
    DynEntCl_ProcessEntities(refdef->localClientNum);

    // Native camera-scene ordering advances physics, waits for world marks,
    // then expands persistent marks for visible static and dynamic
    // receivers before the decal draw list is consumed. The browser already
    // receives world marks from R_UpdateRemainingEffects; complete the other
    // canonical receiver families here using the camera mask produced above.
    if (fx_marks && fx_marks->current.enabled && fx_marks_smodels &&
        fx_marks_smodels->current.enabled && s_world.dpvs.smodelCount != 0u)
    {
        FX_GenerateMarkVertsForStaticModels(
            refdef->localClientNum,
            static_cast<int>(s_world.dpvs.smodelCount),
            s_world.dpvs.smodelVisData[0]);
    }

    if (fx_marks && fx_marks->current.enabled && fx_marks_ents &&
        fx_marks_ents->current.enabled)
    {
        std::uint32_t markIndexCount = 0u;
        FX_BeginGeneratingMarkVertsForEntModels(
            refdef->localClientNum, &markIndexCount);
        for (std::uint32_t index = 0u;
             index < g_dobjSubmissionCount; ++index)
        {
            const WebRendererDObjSubmission &submission =
                g_dobjSubmissions[index];
            if (!submission.obj || !submission.pose ||
                submission.entityNumber >= MAX_GENTITIES ||
                WebRenderer_DObjUsesDepthHack(submission.renderFlags))
            {
                continue;
            }
            // The DObj build has assigned the canonical pose-owned lighting
            // handle by this point. Keep the historical nonzero fallback when
            // lighting was unavailable.
            const std::uint16_t fallbackLightingHandle =
                static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(index + 1u, UINT16_MAX));
            const std::uint16_t lightingHandle =
                submission.cachedLightingHandle &&
                    *submission.cachedLightingHandle != 0u
                ? *submission.cachedLightingHandle
                : fallbackLightingHandle;
            FX_GenerateMarkVertsForEntDObj(
                refdef->localClientNum,
                static_cast<int>(submission.entityNumber),
                &markIndexCount,
                lightingHandle,
                submission.reflectionProbeIndex,
                submission.obj,
                submission.pose);
        }
        for (std::uint32_t index = 0u;
             index < g_brushModelSubmissionCount; ++index)
        {
            const WebRendererBrushModelSubmission &submission =
                g_brushModelSubmissions[index];
            if (!submission.model ||
                submission.entityNumber >= MAX_GENTITIES)
            {
                continue;
            }
            if (kisak::dpvs::CullBox(
                    submission.model->writable.mins,
                    submission.model->writable.maxs,
                    camera.frustumPlanes, camera.frustumPlaneCount) ||
                !R_BoundsTouchVisibleCell(s_world,
                    g_cameraDpvs.cellVisibleBits,
                    submission.model->writable.mins,
                    submission.model->writable.maxs))
            {
                continue;
            }
            GfxPlacement placement{};
            AxisToQuat(submission.axis, placement.quat);
            std::copy_n(submission.origin, 3u, placement.origin);
            const std::uint8_t reflectionProbeIndex =
                static_cast<std::uint8_t>(std::min<std::uint32_t>(
                    WebRenderer_CalcReflectionProbeIndex(
                        s_world, submission.origin),
                    UINT8_MAX));
            FX_GenerateMarkVertsForEntBrush(
                refdef->localClientNum,
                static_cast<int>(submission.entityNumber),
                &markIndexCount,
                reflectionProbeIndex,
                &placement);
        }
        FX_EndGeneratingMarkVertsForEntModels(refdef->localClientNum);
    }

#if KISAK_WEB_DIAGNOSTICS
    const double effectsElapsed = sceneProfile
        ? WebFrameProfile_Now() - effectsStarted : 0.0;
    if (sceneProfile) sceneProfile->sceneEffectsPrepareMs += effectsElapsed;
#endif

    // Use the same scene-wide FOV-adjusted rigid/skinned LOD parameters as
    // DObjs, DynEntities, and static models.
    std::uint32_t selectedFxModels = 0u;
    for (std::uint32_t index = 0u; index < g_fxModelSubmissionCount; ++index)
    {
        WebRendererFxModelSubmission submission = g_fxModelSubmissions[index];
        if (submission.model &&
            WebRenderer_FxModelPlacementIsValid(submission.placement))
        {
            const float radius = submission.model->radius *
                submission.placement.scale;
            if (std::isfinite(radius) && radius >= 0.0f &&
                kisak::dpvs::CullSphere(
                    submission.placement.base.origin, radius,
                    camera.frustumPlanes, camera.frustumPlaneCount))
            {
                continue;
            }
        }
        const int lod = WebRenderer_SelectFxModelLod(
            submission.model, submission.placement, &lodParms);
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

#if KISAK_WEB_DIAGNOSTICS
    const double commandAppendStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
    if (sceneProfile)
        sceneProfile->sceneModelBuildMs +=
            commandAppendStarted - modelBuildStarted - effectsElapsed;
#endif
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
                dynamicCommand.vertices.size() + brushVertexCount,
                dynamicCommand.indices.size() + brushIndexCount,
                dynamicCommand.batches.size() + brushBatchCount,
                dynamicCommand.surfaceCount + brushSurfaceCount,
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
            if (vertexBase + brushVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                    g_markMeshRenderVertices.size() ||
                indexBase + brushIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
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
            if (vertexBase + brushVertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES -
                    g_codeMeshRenderVertices.size() ||
                indexBase + brushIndexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES -
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
#if KISAK_WEB_DIAGNOSTICS
        const double cloudAppendStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
#endif
        const auto admission = WebRenderer_ValidateParticleCloudAppendCounts(
            dynamicCommand.vertices.size() + brushVertexCount,
            dynamicCommand.indices.size() + brushIndexCount,
            dynamicCommand.batches.size() + brushBatchCount,
            dynamicCommand.surfaceCount + brushSurfaceCount);
        const WebRendererParticleCloudAppendResult append =
            admission == WebRendererParticleCloudAppendResult::Success
            ? WebRenderer_AppendParticleCloudCommand(
                cloudCommand,
                dynamicCommand.vertices,
                dynamicCommand.indices,
                dynamicCommand.batches,
                dynamicCommand.surfaceCount) : admission;
#if KISAK_WEB_DIAGNOSTICS
        if (sceneProfile)
            sceneProfile->sceneCloudAppendMs += WebFrameProfile_Now() - cloudAppendStarted;
#endif
        if (append == WebRendererParticleCloudAppendResult::Success)
            hasParticleCloud = true;
        else if (droppedParticleClouds != UINT32_MAX)
            ++droppedParticleClouds;
    }
    if (droppedParticleClouds != 0u)
        R_WarnOncePerFrame(R_WARN_MAX_CLOUDS);

    const bool hasSunSprite = AppendSunSprite(dynamicCommand, view, brushVertexCount, brushIndexCount);
    const bool hasSunFlare = AppendSunFlare(dynamicCommand, view, brushVertexCount, brushIndexCount);

#if KISAK_WEB_DIAGNOSTICS
    const double imageResolveStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
    double dynamicSubmitStarted = imageResolveStarted;
    if (sceneProfile)
    {
        sceneProfile->sceneCommandAppendMs += imageResolveStarted - commandAppendStarted;
        sceneProfile->sceneAssemblyMs += imageResolveStarted - assemblyStarted;
    }
#endif
    if (dynamicBuild == WebRendererDObjSceneResult::Success ||
        hasBrushModels ||
        hasDynamicEntityModels ||
        hasFxModel || hasMarkMesh || hasCodeMesh || hasParticleCloud ||
        hasSunSprite || hasSunFlare)
    {
        for (WebRendererWorldBatchDesc &batch : dynamicCommand.batches)
            ResolveRendererBatchImages(batch);
#if KISAK_WEB_DIAGNOSTICS
        if (sceneProfile)
        {
            dynamicSubmitStarted = WebFrameProfile_Now();
            sceneProfile->sceneImageResolveMs +=
                dynamicSubmitStarted - imageResolveStarted;
        }
#endif
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
        // Keep the first command intact for the one-time diagnostic below.
        // Later frames transfer final geometry storage to the backend and
        // receive its reusable staging capacity in return.
        const bool retainFirstDiagnosticGeometry =
            dynamicBuild == WebRendererDObjSceneResult::Success &&
            !g_dynamicModelSceneReported;
        const WebRendererSurfaceResult submission =
            retainFirstDiagnosticGeometry
            ? WebRenderer_SetDynamicModelScene(scene, brushInstances.data(),
                static_cast<std::uint32_t>(brushInstances.size()),
                brushInsertBatch)
            : WebRenderer_SetDynamicModelSceneOwned(scene,
                dynamicCommand.vertices, dynamicCommand.indices,
                brushInstances.data(),
                static_cast<std::uint32_t>(brushInstances.size()),
                brushInsertBatch);
        if (submission != WebRendererSurfaceResult::Success)
        {
            Com_Error(ERR_DROP, "R_RenderScene dynamic/canonical FX command %s (vertices=%u indices=%u batches=%u brushes=%zu insert=%u)",
                WebRenderer_SurfaceResultString(submission), scene.vertexCount,
                scene.indexCount, scene.batchCount, brushInstances.size(), brushInsertBatch);
            return;
        }
    }
    else
    {
        WebRenderer_SetDynamicModelScene({});
    }
#if KISAK_WEB_DIAGNOSTICS
    if (sceneProfile)
        sceneProfile->sceneDynamicSubmitMs +=
            WebFrameProfile_Now() - dynamicSubmitStarted;
#endif

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
            static_cast<std::uint32_t>(brushInstances.size()), brushSurfaceCount,
            static_cast<std::size_t>(brushBatchCount));
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
    WebRenderer_RecycleDObjSceneGeometry(dynamicCommand);
    view.worldSurfaceCount = g_worldSceneSurfaceCount;
    view.worldVertexCount = g_worldSceneVertexCount;
    view.worldIndexCount = g_worldSceneIndexCount;
    view.geometrySubmitted = g_worldSceneSubmitted;
    std::array<const GfxLight *, 32> visibleLights{};
#if KISAK_WEB_DIAGNOSTICS
    if (g_testTransientLightMode)
    {
        g_addedLightCount = 0;
        g_dynamicSpotLightNearPlaneOffset = 0.0f;
        float origin[3];
        Vec3Mad(view.viewOrigin, 64.0f, view.viewAxis[0], origin);
        if (g_testTransientLightMode == 2)
            R_AddSpotLightToScene(view.viewOrigin, view.viewAxis[0], 512, 0.2f, 0.1f, 0.05f);
        else
            for (int i = 0; i < (g_testTransientLightMode == 3 ? 33 : 1); ++i)
                R_AddOmniLightToScene(origin, 256, 0.8f, 0.2f, 0.05f);
    }
#endif
    int visibleCount = 0;
    const int lightLimit = r_fullbright->current.enabled ? 0 : r_dlightLimit->current.integer;
    if (lightLimit > 0)
    {
        for (std::uint32_t index = 0; index < g_addedLightCount; ++index)
        {
            const auto &light = g_addedLights[index];
            bool culled = false;
            for (int plane = 0; plane < camera.frustumPlaneCount; ++plane)
            {
                const float *coeffs = camera.frustumPlanes[plane].coeffs;
                if (Vec3Dot(light.origin, coeffs) + coeffs[3] < -light.radius)
                {
                    culled = true;
                    break;
                }
            }
            if (!culled) visibleLights[visibleCount++] = &light;
        }
        if (visibleCount > lightLimit)
        {
            kisak::dynamic_lights::MostImportant(visibleLights.data(), visibleCount,
                lightLimit, view.viewOrigin);
            visibleCount = lightLimit;
        }
    }
    view.dynamicLights = visibleLights.data();
    view.dynamicLightCount = visibleCount;
    view.dynamicSpotLightIndex = UINT32_MAX;
    if (g_addedLightCount && g_addedLights[0].type == 2u &&
        g_addedLights[0].canUseShadowMap)
    {
        for (int index = 0; index < visibleCount; ++index)
            if (visibleLights[index] == &g_addedLights[0])
            {
                view.dynamicSpotLightIndex =
                    static_cast<std::uint32_t>(index);
                break;
            }
    }
    view.dynamicSpotLightNearPlaneOffset =
        g_dynamicSpotLightNearPlaneOffset;
    if (visibleCount && visibleLights[0]->def)
        view.dynamicLightAttenuation = ResolveRendererImage(visibleLights[0]->def->attenuation.image);

#if KISAK_WEB_DIAGNOSTICS
    const double viewSubmitStarted = sceneProfile ? WebFrameProfile_Now() : 0.0;
#endif
    if (!WebRenderer_SubmitSceneView(view))
        Com_Error(ERR_DROP, "R_RenderScene: invalid cgame view command");
#if KISAK_WEB_DIAGNOSTICS
    if (sceneProfile)
        sceneProfile->sceneViewSubmitMs += WebFrameProfile_Now() - viewSubmitStarted;
#endif

    if (!g_gameDrivenFrameReported)
    {
        g_gameDrivenFrameReported = true;
        EmitEngineLifecycleTrace(
            EngineLifecycleStage::GameDrivenFrame, s_world.name);
    }
#if KISAK_WEB_DIAGNOSTICS
    if (sceneProfile)
    {
        sceneProfile->sceneBuildMs += WebFrameProfile_Now() - sceneProfileStarted;
    }
#endif
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
    // Native R_AddDObjToScene also treats the first cpose_t field as the
    // mutable cached model-lighting handle despite receiving a const pose.
    submission.cachedLightingHandle = pose
        ? &const_cast<cpose_t *>(pose)->lightingHandle : nullptr;
    const float *sourceLightingOrigin = lightingOrigin
        ? lightingOrigin : (pose ? pose->origin : vec3_origin);
    std::copy_n(sourceLightingOrigin, 3u, submission.lightingOrigin);
    if (s_world.reflectionProbes && s_world.reflectionProbeCount != 0u)
    {
        const std::uint32_t probeIndex =
            WebRenderer_CalcReflectionProbeIndex(
                s_world, sourceLightingOrigin);
        if (probeIndex < s_world.reflectionProbeCount)
        {
            submission.reflectionProbeIndex =
                static_cast<std::uint8_t>(probeIndex);
            submission.reflectionProbeImage =
                s_world.reflectionProbes[probeIndex].reflectionImage;
        }
    }
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
void __cdecl R_LinkDObjEntity(std::uint32_t localClientNum,
    std::uint32_t entityNumber, float *origin, float radius)
{
    if (entityNumber < g_dobjCellLinkValid.size())
    {
        g_dobjCellLinkValid[entityNumber] = 0u;
        g_brushCellLinkValid[entityNumber] = 0u;
        if (origin && std::isfinite(radius) && radius >= 0.0f)
        {
            const float mins[3]{
                origin[0] - radius, origin[1] - radius,
                origin[2] - radius};
            const float maxs[3]{
                origin[0] + radius, origin[1] + radius,
                origin[2] + radius};
            if (R_LinkSceneEntityBoundsToCells(s_world, localClientNum,
                    Web_RendererEntityCount(), entityNumber,
                    DpvsSceneEntityKind::DObj, mins, maxs) ==
                DpvsSceneEntityCellLink::Linked)
            {
                g_dobjCellLinkValid[entityNumber] = 1u;
                g_dobjCellLinkRadius[entityNumber] = radius;
            }
        }
    }
    kisak::primary_lights::LinkSphereEntity(s_world, comWorld,
        Web_RendererEntityCount(), localClientNum, entityNumber,
        origin, radius);
}
void __cdecl R_LinkBModelEntity(std::uint32_t localClientNum,
    std::uint32_t entityNumber, GfxBrushModel *model)
{
    if (model)
    {
        if (entityNumber < g_brushCellLinkValid.size())
        {
            g_dobjCellLinkValid[entityNumber] = 0u;
            g_brushCellLinkValid[entityNumber] =
                R_LinkSceneEntityBoundsToCells(s_world, localClientNum,
                    Web_RendererEntityCount(), entityNumber,
                    DpvsSceneEntityKind::Brush,
                    model->writable.mins, model->writable.maxs) ==
                    DpvsSceneEntityCellLink::Linked
                ? 1u : 0u;
        }
        kisak::primary_lights::LinkBoxEntity(s_world, comWorld,
            Web_RendererEntityCount(), localClientNum, entityNumber,
            model->writable.mins, model->writable.maxs);
    }
}
void __cdecl R_UnlinkEntity(std::uint32_t localClientNum,
    std::uint32_t entityNumber)
{
    R_UnlinkSceneEntityFromCells(s_world, localClientNum,
        Web_RendererEntityCount(), entityNumber);
    if (entityNumber < g_dobjCellLinkValid.size())
    {
        g_dobjCellLinkValid[entityNumber] = 0u;
        g_brushCellLinkValid[entityNumber] = 0u;
        g_dobjCellLinkRadius[entityNumber] = 0.0f;
    }
    kisak::primary_lights::UnlinkEntity(s_world,
        Web_RendererEntityCount(), localClientNum, entityNumber);
}
void __cdecl R_UnlinkDynEnt(std::uint32_t dynEntId,
    DynEntityDrawType drawType)
{
    if (!R_UnlinkDynEntityFromCells(s_world, drawType, dynEntId))
        Com_Error(ERR_DROP, "R_UnlinkDynEnt: invalid canonical DynEntity cell data");
    const auto collType = drawType == DYNENT_DRAW_MODEL
        ? DYNENT_COLL_CLIENT_MODEL : DYNENT_COLL_CLIENT_BRUSH;
    kisak::primary_lights::UnlinkDynEnt(s_world,
        DynEnt_GetEntityCount(collType), dynEntId,
        static_cast<std::uint32_t>(drawType));
}
void __cdecl R_LinkDynEnt(std::uint32_t dynEntId,
    DynEntityDrawType drawType, float *mins, float *maxs)
{
    if (!R_LinkDynEntityBoundsToCells(s_world, drawType, dynEntId, mins, maxs))
        Com_Error(ERR_DROP, "R_LinkDynEnt: invalid canonical DynEntity cell data");
    const auto collType = drawType == DYNENT_DRAW_MODEL
        ? DYNENT_COLL_CLIENT_MODEL : DYNENT_COLL_CLIENT_BRUSH;
    kisak::primary_lights::LinkDynEnt(s_world, comWorld,
        DynEnt_GetEntityCount(collType), dynEntId,
        static_cast<std::uint32_t>(drawType), mins, maxs);
}

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

double __cdecl R_GetFarPlaneDist()
{
    return r_zfar && r_zfar->current.value != 0.0f
        ? r_zfar->current.value : g_cullDistance;
}
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

void __cdecl R_GetAverageLightingAtPoint(const float *position, std::uint8_t *color)
{
    if (g_rendererWorldReady && s_world.sunLight &&
        WebRenderer_EvaluateAverageLighting(s_world.lightGrid, position,
            s_world.sunLight->color, &MODEL_LIGHTING_CALLBACKS, color)) return;
    // Preserve the startup fallback only when a usable world sample is absent.
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

void __cdecl Material_DirtyTechniqueSetOverrides()
{
    g_techniqueSetRemapsDirty = true;
}

Material *__cdecl Material_Duplicate(Material *source, char *name)
{
    iassert(source && name);
    if (Material *duplicate = DB_DuplicateMaterialAsset(source, name))
        return duplicate;
    return source;
}

int R_TextSceneTime() { return g_uiSceneTime; }
int R_TextCursorTime() { return CL_ScaledMilliseconds(); }

const Material *__cdecl Material_FromHandle(Material *handle)
{
    iassert(handle && handle->info.name && handle->info.name[0]);
    return handle;
}
bool __cdecl IsValidMaterialHandle(Material *const handle)
{
    iassert((reinterpret_cast<std::uintptr_t>(handle) & 3u) == 0u);
    return handle && handle->info.name && handle->info.name[0];
}
bool __cdecl Material_HasAnyFogableTechnique(const Material *material)
{
    if (!material || !material->techniqueSet) return false;
    const auto *tech = material->techniqueSet->remappedTechniqueSet;
    if (!tech) tech = material->techniqueSet;
    return tech->techniques[TECHNIQUE_LIT_BEGIN] || tech->techniques[TECHNIQUE_EMISSIVE];
}
void __cdecl RB_LookupColor(std::uint8_t c, GfxColor *color)
{
    const std::uint32_t index = ColorIndex(c);
    // SP never calls the MP R_UpdateTeamColors owner; native rg starts zeroed.
    *color = index < 8u ? color_table[index] : GfxColor(c == '8' || c == '9' ? 0 : -1);
}
void __cdecl R_TextDrawQuad(const Material *material, float x, float y, float w,
    float h, float s0, float t0, float s1, float t1, float sinAngle, float cosAngle,
    std::uint32_t packed)
{
    const float dx = w * cosAngle, dy = w * sinAngle;
    const float hx = -h * sinAngle, hy = h * cosAngle;
    const float verts[4][2]{{x, y}, {x + dx, y + dy},
        {x + dx + hx, y + dy + hy}, {x + hx, y + hy}};
    const GfxColor nativeColor(packed);
    const float color[4]{nativeColor.array[2] / 255.0f, nativeColor.array[1] / 255.0f,
        nativeColor.array[0] / 255.0f, nativeColor.array[3] / 255.0f};
    AppendUiQuad(verts, s0, t0, s1, t1, color, const_cast<Material *>(material));
}
