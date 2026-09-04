#if KISAK_TEST_DPVS_CORE
#include <universal/q_shared.h>
#include <universal/com_math.h>
#include <universal/com_memory.h>
#include <qcommon/qcommon.h>
#include <gfx_d3d/r_dpvs_core.h>
#include <gfx_d3d/r_dvars.h>
#include <DynEntity/DynEntity_client.h>
#endif
#include <gfx_d3d/gfx_world_types.h>
#include <web/web_renderer_static_model_scene.h>
#include <web/web_renderer_world_scene.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out)
{
    out[0] = static_cast<float>(in.packed & 0xffu) / 255.0f;
    out[1] = static_cast<float>((in.packed >> 8u) & 0xffu) / 255.0f;
}

void __cdecl Vec3UnpackUnitVec(PackedUnitVec, float *out)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 1.0f;
}


#if KISAK_TEST_DPVS_CORE
// Host services only; visibility, portal geometry and packing execute production code.
clipMap_t cm{}; // Unused debug-collision owner in native BoxOnPlaneSide's TU.
void MyAssertHandler(const char *file, int line, int, const char *format, ...)
{
    std::fprintf(stderr, "%s:%d: ", file, line);
    va_list args; va_start(args, format); std::vfprintf(stderr, format, args); va_end(args);
    std::abort();
}
void Com_Error(errorParm_t, const char *format, ...)
{
    va_list args; va_start(args, format); std::vfprintf(stderr, format, args); va_end(args);
    std::abort();
}
bool Sys_IsMainThread() { return true; }
void track_static_alloc_internal(void *, int, const char *, int) {}
char *va(const char *format, ...)
{
    static char buffers[8][4096];
    static unsigned index = 0;
    char *out = buffers[index++ % 8];
    va_list args; va_start(args, format); std::vsnprintf(out, 4096, format, args); va_end(args);
    return out;
}
namespace {
std::array<std::uint8_t, 0x20000> portalScratch{};
bool portalScratchInUse = false;
dvar_t disabled{}, bevels{}, minClipArea{}, minRecurse{}, decals{};
}
LargeLocal::LargeLocal(int bytes) : startPos(0), size(bytes)
{
    assert(!portalScratchInUse && bytes <= portalScratch.size());
    portalScratchInUse = true;
}
LargeLocal::~LargeLocal() { portalScratchInUse = false; }
std::uint8_t *LargeLocal::GetBuf() { return portalScratch.data(); }
const dvar_t *r_skipPvs = &disabled;
const dvar_t *r_singleCell = &disabled;
const dvar_t *r_portalWalkLimit = &disabled;
const dvar_t *r_portalBevelsOnly = &disabled;
const dvar_t *r_showPortals = &disabled;
const dvar_t *r_showAabbTrees = &disabled;
const dvar_t *r_drawDecals = &decals;
const dvar_t *r_portalBevels = &bevels;
const dvar_t *r_portalMinClipArea = &minClipArea;
const dvar_t *r_portalMinRecurseDepth = &minRecurse;

#endif

namespace
{
#if KISAK_TEST_DPVS_CORE
struct ObservedDpvsCell
{
    std::uint32_t cellIndex = UINT32_MAX;
    std::uint8_t planeCount = 0u;
    std::uint8_t frustumPlaneCount = 0u;
    std::array<DpvsPlane, DPVS_PORTAL_MAX_PLANES> planes{};
};

struct DpvsObserverCapture
{
    const GfxCell *cells = nullptr;
    std::array<ObservedDpvsCell, 8> records{};
    std::uint32_t count = 0u;
    bool overflowed = false;
};

void CaptureDpvsCell(void *user, const GfxCell *cell,
    const DpvsPlane *planes, unsigned char planeCount,
    unsigned char frustumPlaneCount)
{
    auto &capture = *static_cast<DpvsObserverCapture *>(user);
    if (!capture.cells || !cell || planeCount > DPVS_PORTAL_MAX_PLANES ||
        (planeCount && !planes) || capture.count >= capture.records.size())
    {
        capture.overflowed = true;
        return;
    }
    ObservedDpvsCell &record = capture.records[capture.count++];
    record.cellIndex = static_cast<std::uint32_t>(cell - capture.cells);
    record.planeCount = planeCount;
    record.frustumPlaneCount = frustumPlaneCount;
    std::copy_n(planes, planeCount, record.planes.begin());
}
#endif

void TestTransientLightMaskUsesPackedBoundsAndCanonicalCameraIds()
{
    std::array<WebRendererStaticModelInstanceDesc, 4> instances{};
    const unsigned ids[]{4, 0, 3, 1};
    for (unsigned i = 0; i < 4; ++i) instances[i].canonicalInstanceIndex = ids[i];
    const std::array<WebRendererStaticModelShadowBounds, 4> bounds{{
        {{-1,-1,2},{1,1,3}}, {{99,-1,2},{101,1,3}},
        {{-1,-1,6},{1,1,7}}, {{-1,-1,4},{1,1,5}}}};
    std::array<std::uint8_t, 5> camera{1,1,0,0,1};
    std::array<std::uint8_t, 4> mask{};
    GfxLight light{};
    light.type = 3;
    light.radius = 10;
    const auto build = [&](float nearOffset = 0.0f) {
        return WebRenderer_BuildStaticModelLightVisibility(
            instances, bounds, camera, true, light, nearOffset, mask);
    };
    assert(build() && (mask == std::array<std::uint8_t,4>{1,0,0,1}));
    light.type = 2;
    light.dir[2] = -1;
    light.cosHalfFovOuter = std::sqrt(0.5f);
    assert(build(3) && (mask == std::array<std::uint8_t,4>{0,0,0,1}));
    // A light-space box alone admits both a camera-hidden model and one
    // rejected by the native shifted near plane. Transient spot casters must
    // use this receiver mask; sun cascades retain their independent selection.
    const std::array<float, 16> broadPartition{
        0.1f,0,0,0, 0,0.1f,0,0, 0,0,0.1f,0, 0,0,0,1};
    assert(WebRenderer_StaticModelIntersectsShadowPartition(bounds[0], broadPartition));
    assert(WebRenderer_StaticModelIntersectsShadowPartition(bounds[2], broadPartition));
    assert(mask[0] == 0u && mask[2] == 0u && mask[3] == 1u);
    light.origin[0] = 100;
    assert(build() && (mask == std::array<std::uint8_t,4>{0,1,0,0}));
    light.origin[0] = 0;
    camera[3] = 1;
    assert(build() && (mask == std::array<std::uint8_t,4>{1,0,1,1}));
    // Failed rebuilds cannot leave the previous light's eligible slots behind.
    instances[1].canonicalInstanceIndex = 99;
    assert(!build() && (mask == std::array<std::uint8_t,4>{}));
    instances[1].canonicalInstanceIndex = 0;
    light.cosHalfFovOuter = std::numeric_limits<float>::quiet_NaN();
    assert(!build() && (mask == std::array<std::uint8_t,4>{}));
    light.cosHalfFovOuter = std::sqrt(0.5f);
    assert(!WebRenderer_BuildStaticModelLightVisibility(
        instances, std::span(bounds).first(3), camera, true, light, 0, mask));
}

constexpr std::uint32_t TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX = 2u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_INDEX = 14u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_SUN_INDEX = 15u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX = 16u;
Material *g_resolvedMaterial = nullptr;

Material *ResolveMaterial(Material *) noexcept
{
    return g_resolvedMaterial;
}

struct Fixture
{
    std::array<GfxPackedVertex, 3> vertices{};
    std::array<std::uint16_t, 3> indices{0u, 1u, 2u};
    XSurface surface{};
    GfxImage image{};
    MaterialTextureDef texture{};
    MaterialTechnique technique{};
    MaterialTechniqueSet techniqueSet{};
    GfxStateBits stateBits[1]{};
    Material material{};
    Material *materials[1]{&material};
    XModel model{};
    std::array<GfxStaticModelDrawInst, 3> instances{};
    std::array<GfxStaticModelInst, 3> lightingInstances{};
    GfxWorld world{};

    Fixture()
    {
        vertices[0].xyz[0] = 1.0f;
        vertices[1].xyz[1] = 1.0f;
        vertices[2].xyz[2] = 1.0f;
        vertices[0].color.packed = 0xff804020u;
        vertices[1].color.packed = 0xffffffffu;
        vertices[2].color.packed = 0xffffffffu;
        vertices[0].texCoord.packed = 0x00008040u;
        surface.vertCount = static_cast<std::uint16_t>(vertices.size());
        surface.triCount = 1u;
        surface.verts0 = vertices.data();
        surface.triIndices = indices.data();

        image.name = "model-color";
        texture.semantic = 2u;
        texture.samplerState = 0x42u;
        texture.u.image = &image;
        technique.passCount = 1u;
        techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &technique;
        stateBits[0].loadBits[0] = 0x18008800u;
        stateBits[0].loadBits[1] = 0x0000000du;
        material.info.name = "model/material";
        material.info.gameFlags = 0x40u;
        material.textureCount = 1u;
        material.textureTable = &texture;
        material.techniqueSet = &techniqueSet;
        material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
        material.stateBitsEntry[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX] = 0u;
        material.stateBitsCount = 1u;
        material.stateBitsTable = stateBits;

        model.name = "model/test";
        model.numsurfs = 1u;
        model.numLods = 1;
        model.surfs = &surface;
        model.materialHandles = materials;
        model.lodInfo[0].numsurfs = 1u;
        model.lodInfo[0].surfIndex = 0u;

        for (std::size_t instance = 0u; instance < instances.size(); ++instance)
        {
            instances[instance].model = &model;
            instances[instance].cullDist = 1000.0f;
            instances[instance].placement.scale = instance == 0u ? 1.0f : 2.0f;
            instances[instance].placement.origin[0] =
                static_cast<float>(instance) * 10.0f;
            for (std::size_t axis = 0u; axis < 3u; ++axis)
                instances[instance].placement.axis[axis][axis] = 1.0f;
            lightingInstances[instance].groundLighting.packed =
                0xff204080u;
            lightingInstances[instance].mins[0] =
                static_cast<float>(instance) * 10.0f - 1.0f;
            lightingInstances[instance].mins[1] = -2.0f;
            lightingInstances[instance].mins[2] = -3.0f;
            lightingInstances[instance].maxs[0] =
                static_cast<float>(instance) * 10.0f + 1.0f;
            lightingInstances[instance].maxs[1] = 2.0f;
            lightingInstances[instance].maxs[2] = 3.0f;
        }
        world.dpvs.smodelCount = 2u;
        world.dpvs.smodelDrawInsts = instances.data();
        world.dpvs.smodelInsts = lightingInstances.data();
    }
};

void TestCanonicalInstancesShareOneMaterialSurfaceBatch()
{
    Fixture fixture;
    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.modelCount == 1u);
    assert(command.surfaceCount == 1u);
    assert(command.canonicalInstanceCount == 2u);
    assert(command.vertices.size() == 3u);
    assert(command.indices == std::vector<std::uint32_t>({0u, 1u, 2u}));
    assert(command.instances.size() == 2u);
    assert(command.batches.size() == 1u);

    const WebRendererStaticModelBatchDesc &batch = command.batches[0];
    assert(batch.instanceOffset == 0u && batch.instanceCount == 2u);
    assert(batch.lodIndex == 0u);
    assert(batch.draw.sourceKind == WebRendererSceneBatchKind::StaticXModel);
    assert(batch.draw.dynamicLightSurfType == 2u);
    assert(batch.draw.materialIdentity == &fixture.material);
    assert(batch.draw.modelIdentity == &fixture.model);
    assert(std::strcmp(batch.draw.materialName, "model/material") == 0);
    assert(std::strcmp(batch.draw.modelName, "model/test") == 0);
    assert(batch.draw.baseImage == &fixture.image);
    assert(batch.draw.samplerState == 0x42u);
    assert(batch.draw.technique == WebRendererWorldTechnique::BaseTexture);
    assert(batch.draw.castsSunShadow);
    assert(batch.draw.castsSpotShadow);
    assert(batch.draw.lightingMode ==
        WebRendererWorldLightingMode::ModelLightGrid);
    assert(batch.draw.firstInstanceIndex == 0u);
    assert(batch.draw.lastInstanceIndex == 1u);
    assert(command.instances[1].origin[0] == 10.0f);
    assert(command.instances[1].axis[0][0] == 2.0f);
    assert(command.instances[1].modelScale == 2.0f);
    assert(command.instances[1].modelCullDistance == 1000.0f);
    assert(command.instances[1].canonicalInstanceIndex == 1u);
    assert(command.shadowBounds[1].mins[0] == 9.0f);
    assert(command.shadowBounds[1].maxs[0] == 11.0f);
    assert(command.modelLightingAtlas.entryCount == 2u);
    assert(command.instances[0].modelLightingCoordinates[0] !=
        command.instances[1].modelLightingCoordinates[0]);
    assert(command.modelLightingAtlas.pixels[0] == 0x20u);
    assert(command.modelLightingAtlas.pixels[1] == 0x40u);
    assert(command.modelLightingAtlas.pixels[2] == 0x80u);
    assert(std::fabs(command.vertices[0].textureCoordinate[0] -
        64.0f / 255.0f) < 0.0001f);
    assert(command.vertices[0].normal[2] == 1.0f);

    fixture.material.info.gameFlags = 0u;
    WebRendererStaticModelSceneCommand spotOnlyCommand;
    assert(WebRenderer_BuildStaticModelSceneCommand(
        fixture.world, spotOnlyCommand) ==
        WebRendererStaticModelSceneResult::Success);
    assert(!spotOnlyCommand.batches[0].draw.castsSunShadow);
    assert(spotOnlyCommand.batches[0].draw.castsSpotShadow);
}

void TestSunShadowPartitionUsesCanonicalBounds()
{
    WebRendererStaticModelShadowBounds bounds{};
    bounds.mins[0] = -0.25f;
    bounds.mins[1] = -0.25f;
    bounds.mins[2] = -0.25f;
    bounds.maxs[0] = 0.25f;
    bounds.maxs[1] = 0.25f;
    bounds.maxs[2] = 0.25f;
    const std::array<float, 16> identity{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    assert(WebRenderer_StaticModelIntersectsShadowPartition(bounds, identity));
    bounds.mins[0] = 1.25f;
    bounds.maxs[0] = 1.75f;
    assert(!WebRenderer_StaticModelIntersectsShadowPartition(bounds, identity));
    bounds.mins[0] = 1.0f;
    bounds.maxs[0] = 1.5f;
    assert(WebRenderer_StaticModelIntersectsShadowPartition(bounds, identity));
}

void TestDeformedSiblingSelectsNativeSkinnedReceiverType()
{
    Fixture fixture;
    std::array<XSurface, 2> surfaces{fixture.surface, fixture.surface};
    surfaces[1].deformed = true;
    Material *materials[2]{&fixture.material, &fixture.material};
    fixture.model.numsurfs = 2u;
    fixture.model.surfs = surfaces.data();
    fixture.model.materialHandles = materials;
    fixture.model.lodInfo[0].numsurfs = 2u;
    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.dynamicLightSurfType == 5u);
}

void TestSpotShadowVisibilityUsesAuthoredCanonicalMembership()
{
    WebRendererStaticModelInstanceDesc instances[4]{};
    instances[0].canonicalInstanceIndex = 8u;
    instances[1].canonicalInstanceIndex = 2u;
    instances[2].canonicalInstanceIndex = 5u;
    instances[3].canonicalInstanceIndex = 1u;
    const WebRendererSpotShadowStaticModelDesc memberships[]{
        {3u, 1u}, {3u, 5u}, {4u, 2u}, {4u, 8u},
    };
    std::uint8_t visibility[4]{};
    assert(WebRenderer_BuildStaticModelSpotShadowVisibility(
        instances, 4u, memberships, 4u, 3u, visibility, 4u));
    assert(visibility[0] == 0u);
    assert(visibility[1] == 0u);
    assert(visibility[2] == 1u);
    assert(visibility[3] == 1u);
    assert(WebRenderer_BuildStaticModelSpotShadowVisibility(
        instances, 4u, memberships, 4u, 9u, visibility, 4u));
    assert(std::all_of(std::begin(visibility), std::end(visibility),
        [](std::uint8_t visible) { return visible == 0u; }));
    assert(!WebRenderer_BuildStaticModelSpotShadowVisibility(
        instances, 4u, memberships, 4u, 3u, visibility, 3u));
}

void TestEveryAuthoredLodIsRetainedForRuntimeSelection()
{
    Fixture fixture;
    std::array<XSurface, 2> surfaces{fixture.surface, fixture.surface};
    Material *materials[2]{&fixture.material, &fixture.material};
    fixture.model.numsurfs = 2u;
    fixture.model.numLods = 2;
    fixture.model.surfs = surfaces.data();
    fixture.model.materialHandles = materials;
    fixture.model.lodInfo[0].surfIndex = 0u;
    fixture.model.lodInfo[0].numsurfs = 1u;
    fixture.model.lodInfo[1].surfIndex = 1u;
    fixture.model.lodInfo[1].numsurfs = 1u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.modelCount == 1u);
    assert(command.surfaceCount == 2u);
    assert(command.vertices.size() == 6u);
    assert(command.indices.size() == 6u);
    assert(command.instances.size() == 2u);
    assert(command.batches.size() == 2u);
    assert(command.batches[0].lodIndex == 0u);
    assert(command.batches[1].lodIndex == 1u);
    assert(command.batches[0].instanceOffset == 0u);
    assert(command.batches[1].instanceOffset == 0u);
    assert(command.batches[0].instanceCount == 2u);
    assert(command.batches[1].instanceCount == 2u);
}

void TestCanonicalPrimaryLightSplitsStaticInstanceGroups()
{
    Fixture fixture;
    fixture.instances[0].primaryLightIndex = 1u;
    fixture.instances[1].primaryLightIndex = 2u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 2u);
    assert(command.instances.size() == 2u);
    assert(command.batches[0].instanceCount == 1u);
    assert(command.batches[1].instanceCount == 1u);
    assert(command.batches[0].draw.primaryLightIndex == 1u);
    assert(command.batches[1].draw.primaryLightIndex == 2u);
}

void TestCanonicalInstanceIndicesSurviveRegroupingAndLods()
{
    Fixture fixture;
    fixture.world.dpvs.smodelCount = 3u;
    fixture.instances[0].primaryLightIndex = 1u;
    fixture.instances[1].primaryLightIndex = 2u;
    fixture.instances[2].primaryLightIndex = 1u;
    std::array<XSurface, 2> surfaces{fixture.surface, fixture.surface};
    Material *materials[2]{&fixture.material, &fixture.material};
    fixture.model.numsurfs = 2u;
    fixture.model.numLods = 2;
    fixture.model.surfs = surfaces.data();
    fixture.model.materialHandles = materials;
    fixture.model.lodInfo[1].surfIndex = 1u;
    fixture.model.lodInfo[1].numsurfs = 1u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.canonicalInstanceCount == 3u);
    assert(command.instances.size() == 3u);
    assert(command.batches.size() == 4u);
    for (const auto &batch : command.batches)
    {
        assert(batch.lodIndex < 2u);
        for (std::uint32_t i = 0u; i < batch.instanceCount; ++i)
        {
            const auto &instance = command.instances[batch.instanceOffset + i];
            const std::uint32_t canonical = instance.canonicalInstanceIndex;
            assert(canonical < fixture.world.dpvs.smodelCount);
            assert(fixture.instances[canonical].primaryLightIndex ==
                batch.draw.primaryLightIndex);
            assert(instance.origin[0] == fixture.instances[canonical].placement.origin[0]);
        }
    }
    // The noncontiguous light-1 group must still address DPVS slots 0 and 2.
    assert(command.instances[0].canonicalInstanceIndex == 0u);
    assert(command.instances[1].canonicalInstanceIndex == 2u);
    assert(command.instances[2].canonicalInstanceIndex == 1u);
}


#if KISAK_TEST_DPVS_CORE
void TestCanonicalCameraVisibilityAndIndependentPacking()
{
    Fixture fixture;
    fixture.world.dpvs.smodelCount = 3;
    fixture.instances[0].primaryLightIndex = 1;
    fixture.instances[1].primaryLightIndex = 2;
    fixture.instances[2].primaryLightIndex = 1;
    std::array<XSurface, 2> surfaces{fixture.surface, fixture.surface};
    Material *materials[2]{&fixture.material, &fixture.material};
    fixture.model.numsurfs = 2;
    fixture.model.numLods = 2;
    fixture.model.surfs = surfaces.data();
    fixture.model.materialHandles = materials;
    fixture.model.lodInfo[1].surfIndex = 1;
    fixture.model.lodInfo[1].numsurfs = 1;
    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.instances[1].canonicalInstanceIndex == 2);
    assert(command.instances[2].canonicalInstanceIndex == 1);

    std::array<GfxCell, 2> cells{};
    std::array<GfxAabbTree, 2> trees{};
    std::uint16_t cellNode[2]{1, 0}; // canonical BSP leaf: camera cell 0
    cplane_s unusedPlane{};
    std::uint16_t cell0Models[2]{0, 2}, cell1Models[1]{1};
    std::uint8_t cameraMask[3]{0x55, 0x55, 0x55}; // loaded bytes aren't completion
    std::uint8_t shadowMask[3]{7, 8, 9}, surfaceMask[1]{11};
    fixture.world.cells = cells.data();
    fixture.world.dpvsPlanes.cellCount = 2;
    fixture.world.dpvsPlanes.nodes = cellNode;
    fixture.world.dpvsPlanes.planes = &unusedPlane;
    fixture.world.planeCount = 1;
    fixture.world.dpvs.smodelVisData[0] = cameraMask;
    fixture.world.dpvs.smodelVisData[1] = shadowMask;
    fixture.world.dpvs.surfaceVisData[0] = surfaceMask;
    for (int cell = 0; cell < 2; ++cell)
    {
        cells[cell].aabbTree = &trees[cell];
        cells[cell].aabbTreeCount = 1;
        for (int axis = 0; axis < 3; ++axis)
        {
            trees[cell].mins[axis] = -100;
            trees[cell].maxs[axis] = 100;
        }
    }
    trees[0].smodelIndexes = cell0Models;
    trees[0].smodelIndexCount = 2;
    trees[1].smodelIndexes = cell1Models;
    trees[1].smodelIndexCount = 1;
    const float positions[3][3]{{10, 0, 0}, {30, 0, 0}, {10, 40, 0}};
    for (int model = 0; model < 3; ++model)
        for (int axis = 0; axis < 3; ++axis)
        {
            fixture.lightingInstances[model].mins[axis] = positions[model][axis] - 1;
            fixture.lightingInstances[model].maxs[axis] = positions[model][axis] + 1;
        }
    // The second cell is reachable only through this canonical queued portal.
    GfxPortal portal{};
    float winding[4][3]{{20,-5,-5},{20,-5,5},{20,5,5},{20,5,-5}};
    portal.plane.coeffs[0] = 1;
    portal.plane.coeffs[3] = -20;
    portal.hullAxis[0][1] = 1;
    portal.hullAxis[1][2] = 1;
    portal.vertices = winding;
    portal.vertexCount = 4;
    portal.cell = &cells[1];
    cells[0].portals = &portal;
    cells[0].portalCount = 1;
    bevels.current.value = 0.7f;
    minClipArea.current.value = 0.02f;
    minRecurse.current.integer = 2;
    DpvsGlobals dpvs{};
    GfxViewParms view{};
    auto setView = [&](bool backwards) {
        std::memset(view.axis, 0, sizeof(view.axis));
        view.axis[0][0] = backwards ? -1.0f : 1.0f;
        view.axis[1][1] = backwards ? -1.0f : 1.0f;
        view.axis[2][2] = 1.0f;
        view.origin[3] = 1;
        MatrixForViewer(view.viewMatrix.m, view.origin, view.axis);
        InfinitePerspectiveMatrix(view.projectionMatrix.m, 1, 1, 1);
        MatrixMultiply44(view.viewMatrix.m, view.projectionMatrix.m, view.viewProjectionMatrix.m);
        MatrixInverse44(view.viewProjectionMatrix.m, view.inverseViewProjectionMatrix.m);
    };
    setView(false);
    DpvsObserverCapture capture{};
    capture.cells = cells.data();
    assert(R_ComputeStaticCameraVisibility(fixture.world, dpvs, view, 0, 0,
        false, CaptureDpvsCell, &capture));
    assert(cameraMask[0] == 1 && cameraMask[1] == 1 && cameraMask[2] == 0);
    assert((dpvs.cellVisibleBits[0] & 3u) == 3u);
    assert(!portal.writable.isQueued && !portal.writable.isAncestor && !portal.writable.hullPoints);
    assert(!capture.overflowed && capture.count >= 2u);
    const auto observedCell = [&](std::uint32_t cellIndex)
        -> const ObservedDpvsCell * {
        for (std::uint32_t index = 0u; index < capture.count; ++index)
            if (capture.records[index].cellIndex == cellIndex)
                return &capture.records[index];
        return nullptr;
    };
    const ObservedDpvsCell *const cameraCell = observedCell(0u);
    const ObservedDpvsCell *const portalCell = observedCell(1u);
    assert(cameraCell && portalCell);
    assert(cameraCell->planeCount == cameraCell->frustumPlaneCount);
    // R_VisitPortals supplies a fresh clipped set with a zero camera prefix.
    assert(portalCell->frustumPlaneCount == 0u);
    assert(portalCell->planeCount > portalCell->frustumPlaneCount);
    const float throughPortal[3]{30, 0, 0};
    const float besidePortal[3]{30, 20, 0};
    assert(!kisak::dpvs::CullSphere(throughPortal, 1,
        portalCell->planes.data(), portalCell->planeCount));
    assert(!kisak::dpvs::CullSphere(besidePortal, 1,
        cameraCell->planes.data(), cameraCell->planeCount));
    assert(kisak::dpvs::CullSphere(besidePortal, 1,
        portalCell->planes.data(), portalCell->planeCount));
    const float cellBoundsMins[3]{9.0f, -1.0f, -1.0f};
    const float cellBoundsMaxs[3]{11.0f, 1.0f, 1.0f};
    assert(R_BoundsTouchVisibleCell(fixture.world, dpvs.cellVisibleBits,
        cellBoundsMins, cellBoundsMaxs));
    dpvs.cellVisibleBits[0] = 2u;
    assert(!R_BoundsTouchVisibleCell(fixture.world, dpvs.cellVisibleBits,
        cellBoundsMins, cellBoundsMaxs));
    dpvs.cellVisibleBits[0] = 3u;
    // Native R_SortNodes_r packs internal nodes into two words, leaves into one.
    std::array<std::uint16_t, 4> splitNodes{3u, 3u, 1u, 2u};
    cplane_s splitPlane{};
    splitPlane.normal[0] = 1.0f;
    splitPlane.type = 0u;
    splitPlane.dist = 0.0f;
    GfxWorld splitWorld = fixture.world;
    splitWorld.dpvsPlanes.nodes = reinterpret_cast<std::uint16_t *>(
        splitNodes.data());
    splitWorld.nodeCount = 4;
    splitWorld.dpvsPlanes.planes = &splitPlane;
    splitWorld.planeCount = 1;
    const std::uint32_t frontCellOnly[1]{1u};
    const float frontMins[3]{1.0f, -1.0f, -1.0f};
    const float frontMaxs[3]{2.0f, 1.0f, 1.0f};
    const float backMins[3]{-2.0f, -1.0f, -1.0f};
    const float backMaxs[3]{-1.0f, 1.0f, 1.0f};
    const float splitMins[3]{-1.0f, -1.0f, -1.0f};
    const float splitMaxs[3]{1.0f, 1.0f, 1.0f};
    assert(R_BoundsTouchVisibleCell(splitWorld, frontCellOnly,
        frontMins, frontMaxs));
    assert(!R_BoundsTouchVisibleCell(splitWorld, frontCellOnly,
        backMins, backMaxs));
    assert(R_BoundsTouchVisibleCell(splitWorld, frontCellOnly,
        splitMins, splitMaxs));

    // Post-pose native R_BoundsInCell must reject a box that moved out of
    // the linked cell even when it remains inside the camera/portal planes.
    bool inside = false;
    assert(R_QueryBoundsInCell(splitWorld, 0u, frontMins, frontMaxs, inside) && inside);
    assert(R_QueryBoundsInCell(splitWorld, 1u, frontMins, frontMaxs, inside) && !inside);
    assert(R_QueryBoundsInCell(splitWorld, 0u, backMins, backMaxs, inside) && !inside);
    assert(R_QueryBoundsInCell(splitWorld, 1u, backMins, backMaxs, inside) && inside);
    for (unsigned cell = 0; cell < 2u; ++cell)
        assert(R_QueryBoundsInCell(splitWorld, cell, splitMins, splitMaxs, inside) && inside);
    // Invalid traversals leave the result untouched, not an ordinary miss.
    splitNodes[1] = 1000u;
    assert(!R_QueryBoundsInCell(splitWorld, 1u, backMins, backMaxs, inside));
    assert(inside);
    splitNodes[1] = 3u;
    assert(!R_QueryBoundsInCell(splitWorld, 2u, frontMins, frontMaxs, inside));
    assert(inside);

    // The browser link helpers preserve native sceneEntCellBits ownership:
    // DObjs occupy the first cell bank, brushes the second, and relinking one
    // entity clears its old kind/cells only after a valid BSP walk completes.
    std::array<std::uint32_t, 2u * 2u * 128u> sceneEntCellBits{};
    splitWorld.dpvsPlanes.sceneEntCellBits = sceneEntCellBits.data();
    constexpr std::uint32_t entityCount = 64u;
    constexpr std::uint32_t entityNumber = 5u;
    assert(R_ClearSceneEntityCellLinks(splitWorld, 0u, entityCount));
    assert(R_LinkSceneEntityBoundsToCells(splitWorld, 0u, entityCount,
        entityNumber, DpvsSceneEntityKind::DObj, frontMins, frontMaxs) ==
        DpvsSceneEntityCellLink::Linked);
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityNumber, 0u, DpvsSceneEntityKind::DObj) ==
        DpvsSceneEntityCellLink::Linked);
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityNumber, 1u, DpvsSceneEntityKind::DObj) ==
        DpvsSceneEntityCellLink::Unlinked);
    assert(R_LinkSceneEntityBoundsToCells(splitWorld, 0u, entityCount,
        entityNumber, DpvsSceneEntityKind::DObj, splitMins, splitMaxs) ==
        DpvsSceneEntityCellLink::Linked);
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityNumber, 0u, DpvsSceneEntityKind::DObj) ==
        DpvsSceneEntityCellLink::Linked);
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityNumber, 1u, DpvsSceneEntityKind::DObj) ==
        DpvsSceneEntityCellLink::Linked);
    assert(R_LinkSceneEntityBoundsToCells(splitWorld, 0u, entityCount,
        entityNumber, DpvsSceneEntityKind::Brush, frontMins, frontMaxs) ==
        DpvsSceneEntityCellLink::Linked);
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityNumber, 0u, DpvsSceneEntityKind::DObj) ==
        DpvsSceneEntityCellLink::Unlinked);
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityNumber, 0u, DpvsSceneEntityKind::Brush) ==
        DpvsSceneEntityCellLink::Linked);
    const auto linkedSnapshot = sceneEntCellBits;
    splitWorld.nodeCount = 1;
    assert(R_LinkSceneEntityBoundsToCells(splitWorld, 0u, entityCount,
        entityNumber, DpvsSceneEntityKind::DObj, backMins, backMaxs) ==
        DpvsSceneEntityCellLink::Unavailable);
    assert(sceneEntCellBits == linkedSnapshot);
    splitWorld.nodeCount = 4;
    assert(R_UnlinkSceneEntityFromCells(splitWorld, 0u, entityCount,
        entityNumber));
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityNumber, 0u, DpvsSceneEntityKind::Brush) ==
        DpvsSceneEntityCellLink::Unlinked);
    assert(R_QuerySceneEntityCellLink(splitWorld, 0u, entityCount,
        entityCount, 0u, DpvsSceneEntityKind::DObj) ==
        DpvsSceneEntityCellLink::Unavailable);
    // Client offsets share each 128-word cell row without clearing siblings.
    assert(R_LinkSceneEntityBoundsToCells(splitWorld, 1u, entityCount,
        63u, DpvsSceneEntityKind::DObj, backMins, backMaxs) ==
        DpvsSceneEntityCellLink::Linked);
    assert(sceneEntCellBits[128u + 2u + 1u] == 1u);
    assert(R_ClearSceneEntityCellLinks(splitWorld, 0u, entityCount));
    assert(sceneEntCellBits[128u + 2u + 1u] == 1u);
    assert(R_ClearSceneEntityCellLinks(splitWorld, 1u, entityCount));
    assert(sceneEntCellBits[128u + 2u + 1u] == 0u);
    const std::uint16_t solidLeaf = 0u;
    splitWorld.dpvsPlanes.nodes = const_cast<std::uint16_t *>(&solidLeaf);
    splitWorld.nodeCount = 1;
    assert(R_LinkSceneEntityBoundsToCells(splitWorld, 0u, entityCount,
        entityNumber, DpvsSceneEntityKind::DObj, frontMins, frontMaxs) ==
        DpvsSceneEntityCellLink::Unlinked);
    splitWorld.dpvsPlanes.nodes = splitNodes.data();
    splitWorld.nodeCount = 4;
    // Native DynEntity banks have separate word strides and MSB-first IDs.
    // Exercise IDs across a word boundary and exact queued-portal planes.
    std::array<std::uint32_t, 4> dynModelBits{}, dynBrushBits{};
    std::array<std::uint8_t, 64> dynModelVisible{}, dynBrushVisible{};
    std::array<DynEntityPose, 33> dynPoses{};
    std::array<DynEntityDef, 33> dynDefinitions{};
    std::array<GfxBrushModel, 3> dynBrushModels{};
    splitWorld.models = dynBrushModels.data();
    splitWorld.modelCount = dynBrushModels.size();
    splitWorld.dpvsDyn.dynEntCellBits[0] = dynModelBits.data();
    splitWorld.dpvsDyn.dynEntCellBits[1] = dynBrushBits.data();
    for (unsigned kind = 0; kind < 2u; ++kind)
    {
        splitWorld.dpvsDyn.dynEntClientCount[kind] = 33u;
        splitWorld.dpvsDyn.dynEntClientWordCount[kind] = 2u;
        assert(R_ClearDynEntityCellLinks(splitWorld, kind));
        assert(R_LinkDynEntityBoundsToCells(splitWorld, kind, 0u, frontMins, frontMaxs));
        assert(R_LinkDynEntityBoundsToCells(splitWorld, kind, 32u, frontMins, frontMaxs));
    }
    assert(dynModelBits[0] == 0x80000000u && dynModelBits[1] == 0x80000000u);
    assert(dynModelBits[2] == 0u && dynModelBits[3] == 0u);
    assert(R_LinkDynEntityBoundsToCells(splitWorld, 0u, 32u, backMins, backMaxs));
    assert(dynModelBits[1] == 0u && dynModelBits[3] == 0x80000000u);
    assert(R_LinkDynEntityBoundsToCells(splitWorld, 0u, 32u, splitMins, splitMaxs));
    assert(dynModelBits[1] == 0x80000000u && dynModelBits[3] == 0x80000000u);
    const auto savedDynBits = dynModelBits;
    splitNodes[1] = 1000u;
    assert(!R_LinkDynEntityBoundsToCells(splitWorld, 0u, 32u, backMins, backMaxs));
    assert(dynModelBits == savedDynBits);
    splitNodes[1] = 3u;
    assert(R_UnlinkDynEntityFromCells(splitWorld, 0u, 32u));
    assert(dynModelBits[1] == 0u && dynModelBits[3] == 0u);
    assert(dynBrushBits[1] == 0x80000000u);
    assert(R_LinkDynEntityBoundsToCells(splitWorld, 0u, 32u, frontMins, frontMaxs));
    for (unsigned id : {0u, 32u})
    {
        const float *origin = id == 0u ? besidePortal : throughPortal;
        std::copy_n(origin, 3u, dynPoses[id].pose.origin);
        dynPoses[id].radius = 1.0f;
        dynDefinitions[id].brushModel = id == 0u ? 1u : 2u;
        auto &brush = dynBrushModels[dynDefinitions[id].brushModel];
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            brush.writable.mins[axis] = origin[axis] - 1.0f;
            brush.writable.maxs[axis] = origin[axis] + 1.0f;
        }
    }
    const auto cull = [&](unsigned kind, unsigned cell, const DpvsPlane *planes, int count) {
        return R_CullDynEntityCell(splitWorld, kind, cell, dynPoses.data(),
            dynDefinitions.data(), planes, count,
            kind == 0u ? dynModelVisible.data() : dynBrushVisible.data());
    };
    for (unsigned kind = 0; kind < 2u; ++kind)
    {
        auto &visible = kind == 0u ? dynModelVisible : dynBrushVisible;
        assert(cull(kind, 1u, nullptr, 0));
        assert(visible[0] == 0u && visible[32] == 0u); // Wrong cell.
        assert(cull(kind, 0u, portalCell->planes.data(), portalCell->planeCount));
        assert(visible[0] == 0u && visible[32] == 1u);
        assert(cull(kind, 0u, nullptr, 0)); // A second, open portal path.
        assert(visible[0] == 1u && visible[32] == 1u);
        assert(cull(kind, 0u, portalCell->planes.data(), portalCell->planeCount));
        assert(visible[0] == 1u); // Rejection cannot undo another path's admission.
        visible.fill(0u);
        DpvsPlane outside{{1.0f, 0.0f, 0.0f, -31.0f}, {}};
        assert(cull(kind, 0u, &outside, 1));
        assert(visible[0] == 0u && visible[32] == 0u); // Full camera prefix; tangent.
        assert(!cull(kind, 0u, nullptr, 1));
    }
    // Padding bits are not entity IDs and cannot index the canonical arrays.
    dynModelBits[1] |= 1u;
    assert(!cull(0u, 0u, nullptr, 0));
    dynModelBits[1] &= ~1u;
    dynBrushVisible.fill(0u);
    dynDefinitions[0].brushModel = 3u;
    assert(!cull(1u, 0u, nullptr, 0));
    dynDefinitions[0].brushModel = 1u;
    splitWorld.dpvsDyn.dynEntClientWordCount[0] = 1u;
    assert(!R_DynEntityCellLayoutAvailable(splitWorld, 0u));
    splitWorld.dpvsDyn.dynEntClientWordCount[0] = 2u;
    assert(R_ClearDynEntityCellLinks(splitWorld, 0u));
    assert(std::all_of(dynModelBits.begin(), dynModelBits.end(), [](auto word) { return word == 0u; }));
    assert(dynBrushBits[0] == 0x80000000u);

    // Different bank strides and an independent scalar oracle for the
    // original native sphere/box loops, including their arithmetic order.
    splitWorld.dpvsDyn.dynEntClientCount[0] = 1u;
    splitWorld.dpvsDyn.dynEntClientCount[1] = 1u;
    splitWorld.dpvsDyn.dynEntClientWordCount[1] = 1u;
    dynBrushBits.fill(0x12345678u);
    assert(R_ClearDynEntityCellLinks(splitWorld, 1u));
    assert(dynBrushBits[0] == 0u && dynBrushBits[1] == 0u);
    assert(dynBrushBits[2] == 0x12345678u); // Only this bank's two cells.
    assert(R_LinkDynEntityBoundsToCells(splitWorld, 0u, 0u, frontMins, frontMaxs));
    assert(R_LinkDynEntityBoundsToCells(splitWorld, 1u, 0u, frontMins, frontMaxs));
    std::uint32_t randomState = 0x39177u;
    const auto randomFloat = [&]() {
        randomState = randomState * 1664525u + 1013904223u;
        return (static_cast<int>(randomState >> 16u) - 32768) / 1024.0f;
    };
    for (unsigned sample = 0; sample < 1024u; ++sample)
    {
        std::array<DpvsPlane, 3> planes{};
        for (auto &plane : planes)
            for (float &coefficient : plane.coeffs) coefficient = randomFloat();
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            const float origin = randomFloat();
            dynPoses[0].pose.origin[axis] = origin;
            dynBrushModels[1].writable.mins[axis] = origin - 2.0f;
            dynBrushModels[1].writable.maxs[axis] = origin + 2.0f;
        }
        for (unsigned kind = 0; kind < 2u; ++kind)
        {
            auto &visible = kind == 0u ? dynModelVisible : dynBrushVisible;
            visible.fill(0u);
            bool expected = true;
            for (const auto &plane : planes)
            {
                const float *origin = dynPoses[0].pose.origin;
                float distance = (plane.coeffs[0] * origin[0] +
                    plane.coeffs[1] * origin[1]) + plane.coeffs[2] * origin[2];
                distance = (distance + plane.coeffs[3]) + dynPoses[0].radius;
                if (kind == 1u)
                {
                    const auto &bounds = dynBrushModels[1].writable;
                    const auto extreme = [&](unsigned axis) {
                        return plane.coeffs[axis] >= 0.0f ? bounds.maxs[axis] : bounds.mins[axis];
                    };
                    distance = ((extreme(0) * plane.coeffs[0] + plane.coeffs[3]) +
                        extreme(1) * plane.coeffs[1]) + extreme(2) * plane.coeffs[2];
                }
                if (distance <= 0.0f) expected = false;
            }
            assert(cull(kind, 0u, planes.data(), planes.size()));
            assert((visible[0] != 0u) == expected);
        }
    }

    const DpvsPlane cameraPlane{{1.0f, 0.0f, 0.0f, -5.0f}, {}};
    const float visibleSphere[3]{10.0f, 0.0f, 0.0f};
    const float culledSphere[3]{3.0f, 0.0f, 0.0f};
    const float tangentSphere[3]{4.0f, 0.0f, 0.0f};
    assert(!kisak::dpvs::CullSphere(
        visibleSphere, 1.0f, &cameraPlane, 1));
    assert(kisak::dpvs::CullSphere(
        culledSphere, 1.0f, &cameraPlane, 1));
    assert(kisak::dpvs::CullSphere(
        tangentSphere, 1.0f, &cameraPlane, 1));
    const float visibleBoxMins[3]{6.0f, -1.0f, -1.0f};
    const float visibleBoxMaxs[3]{8.0f, 1.0f, 1.0f};
    const float culledBoxMins[3]{2.0f, -1.0f, -1.0f};
    const float culledBoxMaxs[3]{5.0f, 1.0f, 1.0f};
    assert(!kisak::dpvs::CullBox(
        visibleBoxMins, visibleBoxMaxs, &cameraPlane, 1));
    assert(kisak::dpvs::CullBox(
        culledBoxMins, culledBoxMaxs, &cameraPlane, 1));

    const auto shadowSource = command.instances;
    std::array<WebRendererStaticModelInstanceDesc, 6> packed{};
    std::copy(shadowSource.begin(), shadowSource.end(), packed.begin());
    const std::int8_t selectedLods[3]{0, 1, 1}; // never changed during visibility changes
    std::array<std::uint32_t, 4> offsets{}, counts{};
    auto pack = [&](bool computed) {
        assert(WebRenderer_PackStaticModelCameraInstances(command.instances.data(), 3,
            selectedLods, cameraMask, 3, computed, packed.data() + 3, offsets, counts));
        assert(std::memcmp(packed.data(), shadowSource.data(),
            shadowSource.size() * sizeof(shadowSource[0])) == 0);
    };
    pack(true);
    assert(counts[0] == 1 && counts[1] == 1);
    assert(packed[3].canonicalInstanceIndex == 0 && packed[4].canonicalInstanceIndex == 1);
    setView(true);
    assert(R_ComputeStaticCameraVisibility(fixture.world, dpvs, view, 0, 0));
    assert(cameraMask[0] == 0 && cameraMask[1] == 0 && cameraMask[2] == 0);
    pack(true);
    assert(counts[0] == 0 && counts[1] == 0); // completed empty stays empty
    pack(false);
    assert(counts[0] == 1 && counts[1] == 2); // only unavailable is conservative
    setView(false);
    assert(R_ComputeStaticCameraVisibility(fixture.world, dpvs, view, 0, 15));
    assert(cameraMask[0] == 1 && cameraMask[1] == 0); // canonical far/cull plane
    pack(true);
    assert(counts[0] == 1 && counts[1] == 0);
    assert(shadowMask[0] == 7 && shadowMask[1] == 8 && shadowMask[2] == 9);
    assert(surfaceMask[0] == 11); // static-model-only callers leave world bytes alone

    // Original synthetic geometry (repository GPL-3.0); no retail data. Exercise
    // the same canonical producer and production batch/range helpers together.
    std::array<GfxSurface, 5> worldSurfaces{};
    std::array<GfxWorldVertex, 15> worldVertices{};
    std::array<std::uint16_t, 15> worldIndices{};
    Material sky{};
    sky.info.gameFlags = 8u;
    const float worldPositions[5][3]{{10,0,0},{10,40,0},{10,0,0},{30,0,0},{12,0,0}};
    for (unsigned i = 0; i < worldSurfaces.size(); ++i)
    {
        auto &surface = worldSurfaces[i];
        surface.material = i == 2 ? &sky : &fixture.material;
        surface.primaryLightIndex = i == 4 ? 1u : 0u;
        surface.tris.firstVertex = i * 3;
        surface.tris.baseIndex = i * 3;
        surface.tris.vertexCount = 3;
        surface.tris.triCount = 1;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            surface.bounds[0][axis] = worldPositions[i][axis] - 1;
            surface.bounds[1][axis] = worldPositions[i][axis] + 1;
            for (unsigned vertex = 0; vertex < 3; ++vertex)
                worldVertices[i * 3 + vertex].xyz[axis] = worldPositions[i][axis];
        }
        for (unsigned vertex = 0; vertex < 3; ++vertex)
            worldIndices[i * 3 + vertex] = vertex;
    }
    GfxBrushModel worldModel{};
    worldModel.surfaceCount = 5;
    auto &world = fixture.world;
    world.models = &worldModel;
    world.modelCount = 1;
    world.surfaceCount = world.dpvs.staticSurfaceCount = 5;
    world.dpvs.staticSurfaceCountNoDecal = 5;
    world.dpvs.surfaces = worldSurfaces.data();
    world.vd.vertices = worldVertices.data();
    world.vertexCount = 15;
    world.indices = worldIndices.data();
    world.indexCount = 15;
    std::uint8_t worldMask[5]{55,55,55,55,55}, worldShadowMask[5]{7,8,9,10,11};
    world.dpvs.surfaceVisData[0] = worldMask;
    world.dpvs.surfaceVisData[1] = worldShadowMask;
    std::uint16_t sortedSurfaces[10]{1,0,3,4,2, 0,3,4,2,1};
    world.dpvs.sortedSurfIndex = sortedSurfaces;
    trees[0].startSurfIndex = 0;
    trees[0].surfaceCount = 2;
    trees[1].startSurfIndex = 2;
    trees[1].surfaceCount = 1;
    trees[0].startSurfIndexNoDecal = 5;
    trees[0].surfaceCountNoDecal = 1;
    trees[1].startSurfIndexNoDecal = 6;
    trees[1].surfaceCountNoDecal = 1;
    GfxCullGroup group{};
    std::copy_n(worldSurfaces[4].bounds[0], 3, group.mins);
    std::copy_n(worldSurfaces[4].bounds[1], 3, group.maxs);
    group.startSurfIndex = 3;
    group.surfaceCount = 1;
    world.cullGroupCount = 1;
    world.dpvs.cullGroups = &group;
    int cellGroup = 0;
    cells[0].cullGroups = &cellGroup;
    cells[0].cullGroupCount = 1;
    std::uint32_t sunCasters = 0x1fu;
    world.dpvs.surfaceCastsSunShadow = &sunCasters;
    std::uint16_t spotSurfaces[2]{1,3}; // includes the camera-invisible surface
    GfxShadowGeometry shadowGeometry{};
    shadowGeometry.surfaceCount = 2;
    shadowGeometry.sortedSurfIndex = spotSurfaces;
    world.shadowGeom = &shadowGeometry;
    world.primaryLightCount = 1;
    fixture.material.stateBitsEntry[2] = 0;
    WebRendererSceneViewDesc sceneView{};
    sceneView.width = sceneView.height = 100;
    sceneView.tanHalfFovX = sceneView.tanHalfFovY = sceneView.zNear = 1;
    sceneView.viewAxis[0][0] = sceneView.viewAxis[1][1] = sceneView.viewAxis[2][2] = 1;
    WebRendererWorldSceneCommand worldCommand;
    assert(WebRenderer_BuildWorldSceneCommand(world, sceneView, worldCommand) ==
        WebRendererWorldSceneResult::Success);
    assert(worldCommand.batches.size() == 2 && worldCommand.surfaceRanges.size() == 4);
    assert(worldCommand.surfaceRanges[2].canonicalSurfaceIndex == 3); // sky gap
    assert(worldCommand.batches[0].indexCount == 9 && worldCommand.batches[0].castsSunShadow);
    assert(worldCommand.spotShadowCasters.size() == 2);
    assert(worldCommand.spotShadowCasters[0].firstIndex == 3);
    const auto shadowIndices = worldCommand.indices;
    const auto shadowBatches = worldCommand.batches;
    const auto spotCasters = worldCommand.spotShadowCasters;
    WebRendererWorldSurfaceDesc descriptor{};
    descriptor.indexCount = 12;
    descriptor.batches = worldCommand.batches.data();
    descriptor.batchCount = 2;
    descriptor.surfaceRanges = worldCommand.surfaceRanges.data();
    descriptor.surfaceRangeCount = 4;
    descriptor.canonicalSurfaceCount = 5;
    assert(WebRenderer_ValidateWorldSurfaceRanges(descriptor));
    std::vector<WebRendererWorldCameraRange> cameraRanges;
    auto computeWorld = [&](float farPlane = 0.0f) {
        assert(R_ComputeStaticCameraVisibility(world, dpvs, view, 0, farPlane, true));
        assert(WebRenderer_BuildWorldCameraRanges(worldCommand.surfaceRanges,
            worldMask, 5, true, cameraRanges));
        pack(true); // static-model LOD/canonical-ID assertions still apply
        assert(worldCommand.indices == shadowIndices);
        assert(std::memcmp(shadowBatches.data(), worldCommand.batches.data(),
            shadowBatches.size() * sizeof(shadowBatches[0])) == 0);
        assert(std::memcmp(spotCasters.data(), worldCommand.spotShadowCasters.data(),
            spotCasters.size() * sizeof(spotCasters[0])) == 0);
        assert(worldShadowMask[0] == 7 && worldShadowMask[4] == 11);
        assert(shadowMask[0] == 7 && shadowMask[2] == 9);
    };
    decals.current.enabled = true;
    computeWorld();
    assert(worldMask[0] == 1 && worldMask[1] == 0 && worldMask[3] == 1 && worldMask[4] == 1);
    assert(cameraRanges.size() == 3);
    assert(cameraRanges[0].firstIndex == 0 && cameraRanges[0].indexCount == 3);
    assert(cameraRanges[1].firstIndex == 6 && cameraRanges[1].batchIndex == 0);
    assert(cameraRanges[2].firstIndex == 9 && cameraRanges[2].batchIndex == 1);
    assert(counts[0] == 1 && counts[1] == 1);
    setView(true);
    computeWorld();
    assert(cameraRanges.empty() && counts[0] == 0 && counts[1] == 0);
    setView(false);
    computeWorld(15);
    assert(worldMask[3] == 0 && cameraRanges.size() == 2 && counts[1] == 0);
    // Unchanged LODs, changed camera visibility; retain original batch order
    // and coalesce only contiguous visible spans, including a skipped sky ID.
    worldSurfaces[1].bounds[0][1] = -1;
    worldSurfaces[1].bounds[1][1] = 1;
    computeWorld();
    assert(cameraRanges.size() == 2 && cameraRanges[0].indexCount == 9);
    assert(cameraRanges[0].surfaceCount == 3 && cameraRanges[1].surfaceCount == 1);
    decals.current.enabled = false;
    computeWorld();
    assert(worldMask[1] == 0 && cameraRanges.size() == 3);
    assert(!WebRenderer_BuildWorldCameraRanges(worldCommand.surfaceRanges,
        worldMask, 5, false, cameraRanges) && cameraRanges.empty());
    assert(!WebRenderer_BuildWorldCameraRanges(worldCommand.surfaceRanges,
        worldMask, 4, true, cameraRanges) && cameraRanges.empty());
    assert(!WebRenderer_BuildWorldCameraRanges(worldCommand.surfaceRanges,
        nullptr, 5, true, cameraRanges) && cameraRanges.empty());
    ++worldCommand.surfaceRanges[1].firstIndex;
    assert(!WebRenderer_ValidateWorldSurfaceRanges(descriptor));
    --worldCommand.surfaceRanges[1].firstIndex;
    worldCommand.surfaceRanges[1].canonicalSurfaceIndex = 5;
    assert(!WebRenderer_ValidateWorldSurfaceRanges(descriptor));
    world.dpvs.surfaceVisData[0] = nullptr;
    assert(!R_ComputeStaticCameraVisibility(world, dpvs, view, 0, 0, true));
    fixture.world.cells = nullptr;
    assert(!R_ComputeStaticCameraVisibility(fixture.world, dpvs, view, 0, 0));
    assert(!WebRenderer_PackStaticModelCameraInstances(command.instances.data(), 3,
        selectedLods, cameraMask, 2, true, packed.data() + 3, offsets, counts));
}

#endif

void TestAmbientProbeShaderIdentitySurvivesPortableBoundary()
{
    Fixture fixture;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lp_amb_t0c0_sm3.hlsl";
    fixture.technique.passArray[0].pixelShader = &pixelShader;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.ambientProbeLighting);
    assert(std::strcmp(command.batches[0].draw.pixelShaderName,
        "lp_amb_t0c0_sm3.hlsl") == 0);
}

void TestCanonicalStaticModelInstancedTechniqueSelection()
{
    Fixture fixture;
    MaterialTechnique instanced{};
    MaterialTechnique instancedSun{};
    MaterialTechnique instancedSunShadow{};
    MaterialPixelShader ambientPixel{};
    MaterialPixelShader sunPixel{};
    ambientPixel.name = "lp_i_amb_t0c0_sm3.hlsl";
    sunPixel.name = "lp_i_amb_sun_t0c0_sm3.hlsl";
    instanced.name = "lprobe_i_amb_t0c0_dtex_sm3";
    instanced.passCount = 1u;
    instanced.passArray[0].pixelShader = &ambientPixel;
    instancedSun.name = "lprobe_i_amb_sun_t0c0_dtex_sm3";
    instancedSun.passCount = 1u;
    instancedSun.passArray[0].pixelShader = &sunPixel;
    instancedSunShadow = instancedSun;
    fixture.techniqueSet.techniques[TECHNIQUE_LIT_INSTANCED_INDEX] =
        &instanced;
    fixture.techniqueSet.techniques[TECHNIQUE_LIT_INSTANCED_SUN_INDEX] =
        &instancedSun;
    fixture.techniqueSet.techniques[
        TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX] = &instancedSunShadow;
    fixture.material.stateBitsEntry[TECHNIQUE_LIT_INSTANCED_INDEX] = 0u;
    fixture.material.stateBitsEntry[TECHNIQUE_LIT_INSTANCED_SUN_INDEX] = 0u;
    fixture.material.stateBitsEntry[
        TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX] = 0u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.techniqueType ==
        TECHNIQUE_LIT_INSTANCED_INDEX);
    assert(command.batches[0].draw.ambientProbeLighting);

    fixture.world.sunPrimaryLightIndex = 1u;
    fixture.instances[0].primaryLightIndex = 1u;
    fixture.instances[1].primaryLightIndex = 1u;
    command = {};
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.techniqueType ==
        TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX);
    assert(std::strcmp(command.batches[0].draw.pixelShaderName,
        "lp_i_amb_sun_t0c0_sm3.hlsl") == 0);
    assert(command.batches[0].draw.ambientProbeLighting);
}

void TestNamedDetailMapAndScaleSurvivePortableBoundary()
{
    Fixture fixture;
    GfxImage detailImage{};
    std::array<MaterialTextureDef, 2> textures{};
    textures[0].nameHash = 0xeb529b4du;
    textures[0].semantic = 2u;
    textures[0].samplerState = 0x13u;
    textures[0].u.image = &detailImage;
    textures[1] = fixture.texture;
    textures[1].nameHash = 0xa0ab1041u;
    fixture.material.textureCount = 2u;
    fixture.material.textureTable = textures.data();
    MaterialConstantDef detailScale{};
    detailScale.nameHash = 0x08d36a09u;
    detailScale.literal[0] = 8.0f;
    detailScale.literal[1] = 16.0f;
    fixture.material.constantCount = 1u;
    fixture.material.constantTable = &detailScale;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lp_r0c0d0_sm3.hlsl";
    fixture.technique.passArray[0].pixelShader = &pixelShader;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    const WebRendererWorldBatchDesc &draw = command.batches[0].draw;
    assert(draw.baseImage == &fixture.image);
    assert(draw.detailImage == &detailImage);
    assert(draw.detailSamplerState == 0x13u);
    assert(draw.detailScale[0] == 8.0f);
    assert(draw.detailScale[1] == 16.0f);
}

void TestLightingAtlasCountsOnlySubmittedCanonicalPlacements()
{
    Fixture fixture;
    fixture.world.dpvs.smodelCount = 3u;
    fixture.instances[2].model = nullptr;
    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.canonicalInstanceCount == 3u);
    assert(command.instances.size() == 2u);
    assert(command.modelLightingFailureCount == 0u);
    assert(command.modelLightingAtlas.entryCount == 2u);
    assert(command.batches[0].draw.lightingMode ==
        WebRendererWorldLightingMode::ModelLightGrid);
}

void TestNativeStaticModelCardinalityIsAccepted()
{
    Fixture fixture;
    std::vector<GfxStaticModelDrawInst> instances(
        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES, fixture.instances[0]);
    fixture.world.dpvs.smodelCount = static_cast<std::uint32_t>(
        instances.size());
    fixture.world.dpvs.smodelDrawInsts = instances.data();
    fixture.world.dpvs.smodelInsts = nullptr;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.canonicalInstanceCount ==
        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES);
    assert(command.instances.size() ==
        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES);
    assert(command.instances.back().canonicalInstanceIndex == 65'535u);
}

void TestMaterialReferencesResolveAtRendererEvaluation()
{
    Fixture fixture;
    Material reference{};
    reference.info.name = ",model/material";
    fixture.materials[0] = &reference;
    g_resolvedMaterial = &fixture.material;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(
        fixture.world, command, nullptr, ResolveMaterial) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.materialIdentity == &fixture.material);
    assert(command.batches[0].draw.baseImage == &fixture.image);
    assert(command.batches[0].draw.technique ==
        WebRendererWorldTechnique::BaseTexture);

    g_resolvedMaterial = nullptr;
}

void TestCameraRegionThreeRemainsShadowOnly()
{
    Fixture fixture;
    fixture.material.cameraRegion = 3u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    const WebRendererWorldBatchDesc &draw = command.batches[0].draw;
    assert(draw.cameraRegion == 3u);
    assert(!WebRenderer_IsCameraVisibleXModelSurface(
        draw.sourceKind, draw.cameraRegion));
    assert(WebRenderer_IsCameraVisibleXModelSurface(
        WebRendererSceneBatchKind::StaticXModel, 2u));
    assert(WebRenderer_IsCameraVisibleXModelSurface(
        WebRendererSceneBatchKind::FxXModel, 3u));
    // The batch is deliberately retained so the backend's shadow partition
    // can still submit its canonical build-shadowmap technique.
    assert(draw.castsSunShadow);
}

void TestSm3SpecularRetainsCanonicalProbeAndSplitsProbeGroups()
{
    Fixture fixture;
    GfxImage normalImage{};
    GfxImage specularImage{};
    GfxImage probeImages[2]{};
    std::array<MaterialTextureDef, 3> textures{};
    textures[0] = fixture.texture;
    textures[1].semantic = 5u;
    textures[1].samplerState = 0x0bu;
    textures[1].u.image = &normalImage;
    textures[2].semantic = 8u;
    textures[2].samplerState = 0x2bu;
    textures[2].u.image = &specularImage;
    fixture.material.textureCount =
        static_cast<std::uint8_t>(textures.size());
    fixture.material.textureTable = textures.data();

    MaterialConstantDef envMapParms{};
    envMapParms.nameHash = 0x3d9994dcu;
    envMapParms.literal[0] = 0.4f;
    envMapParms.literal[1] = 10.0f;
    envMapParms.literal[2] = 2.5f;
    envMapParms.literal[3] = 0.625f;
    fixture.material.constantCount = 1u;
    fixture.material.constantTable = &envMapParms;

    std::uint32_t shaderWords[2]{0xffff0300u, 0x0000ffffu};
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lp_r0c0n0s0_sm3.hlsl";
    pixelShader.prog.loadDef.program = shaderWords;
    pixelShader.prog.loadDef.programSize = 2u;
    fixture.technique.name = "lp_r0c0n0s0_dtex_sm3";
    fixture.technique.flags = 0x1234u;
    fixture.technique.passArray[0].pixelShader = &pixelShader;
    fixture.technique.passArray[0].customSamplerFlags = 1u;

    GfxReflectionProbe probes[2]{};
    probes[0].reflectionImage = &probeImages[0];
    probes[1].reflectionImage = &probeImages[1];
    fixture.world.reflectionProbeCount = 2u;
    fixture.world.reflectionProbes = probes;
    fixture.instances[0].reflectionProbeIndex = 0u;
    fixture.instances[1].reflectionProbeIndex = 1u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 2u);
    assert(command.instances.size() == 2u);
    assert(command.vertices.size() == 3u);
    assert(command.indices.size() == 3u);
    assert(command.batches[0].draw.firstIndex == 0u);
    assert(command.batches[1].draw.firstIndex == 0u);
    for (std::size_t index = 0u; index < command.batches.size(); ++index)
    {
        const WebRendererWorldBatchDesc &draw = command.batches[index].draw;
        assert(draw.technique ==
            WebRendererWorldTechnique::BaseTextureNormalSpecular);
        assert(draw.normalImage == &normalImage);
        assert(draw.specularImage == &specularImage);
        assert(draw.specularSamplerState == 0x2bu);
        assert(draw.reflectionProbeIndex == index);
        assert(draw.reflectionProbeImage == &probeImages[index]);
        assert(draw.envMapParms[0] == 0.4f);
        assert(draw.envMapParms[2] == 2.5f);
        assert(draw.techniqueFlags == 0x1234u);
        assert(draw.customSamplerFlags == 1u);
        assert(std::strcmp(draw.pixelShaderName,
            "lp_r0c0n0s0_sm3.hlsl") == 0);
        assert(draw.pixelShaderProgramHash != 0u);
        assert(WebRenderer_UsesModelEnvironmentSpecular(draw.technique));
    }
}

void TestMalformedIndexAndPlacementFailAtomically()
{
    Fixture fixture;
    WebRendererStaticModelSceneCommand command;
    command.modelCount = 99u;
    fixture.indices[2] = 3u;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::IndexOutOfRange);
    assert(command.modelCount == 99u);

    fixture.indices[2] = 2u;
    fixture.instances[0].placement.scale = 0.0f;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::InvalidPlacement);
    assert(command.modelCount == 99u);
}
} // namespace

int main()
{
    TestTransientLightMaskUsesPackedBoundsAndCanonicalCameraIds();
#if KISAK_TEST_DPVS_CORE
    TestCanonicalCameraVisibilityAndIndependentPacking();
#endif
    TestCanonicalInstancesShareOneMaterialSurfaceBatch();
    TestDeformedSiblingSelectsNativeSkinnedReceiverType();
    TestSunShadowPartitionUsesCanonicalBounds();
    TestSpotShadowVisibilityUsesAuthoredCanonicalMembership();
    TestEveryAuthoredLodIsRetainedForRuntimeSelection();
    TestCanonicalPrimaryLightSplitsStaticInstanceGroups();
    TestCanonicalInstanceIndicesSurviveRegroupingAndLods();
    TestAmbientProbeShaderIdentitySurvivesPortableBoundary();
    TestCanonicalStaticModelInstancedTechniqueSelection();
    TestNamedDetailMapAndScaleSurvivePortableBoundary();
    TestLightingAtlasCountsOnlySubmittedCanonicalPlacements();
    TestNativeStaticModelCardinalityIsAccepted();
    TestMaterialReferencesResolveAtRendererEvaluation();
    TestCameraRegionThreeRemainsShadowOnly();
    TestSm3SpecularRetainsCanonicalProbeAndSplitsProbeGroups();
    TestMalformedIndexAndPlacementFailAtomically();
    return 0;
}
