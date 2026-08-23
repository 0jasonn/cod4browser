#include <web/web_renderer_world_scene.h>

#include <gfx_d3d/gfx_world_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace
{
constexpr float BYTE_TO_UNIT = 1.0f / 255.0f;
constexpr std::uint32_t TECHNIQUE_UNLIT_INDEX = 4u;
constexpr std::uint32_t TECHNIQUE_EMISSIVE_INDEX = 5u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
constexpr std::uint32_t TECHNIQUE_LIT_SUN_INDEX = 8u;
constexpr std::uint32_t TECHNIQUE_LIT_SUN_SHADOW_INDEX = 9u;
constexpr std::uint32_t TECHNIQUE_LIT_SPOT_INDEX = 10u;
constexpr std::uint32_t TECHNIQUE_LIT_OMNI_INDEX = 12u;
constexpr std::uint32_t TECHNIQUE_NONE_INDEX = 36u;

void UnpackUnitVec(PackedUnitVec packed, float output[3]) noexcept
{
    const float scale =
        (static_cast<float>(packed.array[3]) + 192.0f) / 32385.0f;
    output[0] = (static_cast<float>(packed.array[0]) - 127.0f) * scale;
    output[1] = (static_cast<float>(packed.array[1]) - 127.0f) * scale;
    output[2] = (static_cast<float>(packed.array[2]) - 127.0f) * scale;
}

bool Finite3(const float value[3]) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool ViewIsValid(const WebRendererSceneViewDesc &view) noexcept
{
    if (view.width == 0u || view.height == 0u ||
        !std::isfinite(view.tanHalfFovX) || view.tanHalfFovX <= 0.0f ||
        !std::isfinite(view.tanHalfFovY) || view.tanHalfFovY <= 0.0f ||
        !std::isfinite(view.zNear) || view.zNear <= 0.0f ||
        !Finite3(view.viewOrigin))
    {
        return false;
    }
    for (const auto &axis : view.viewAxis)
    {
        if (!Finite3(axis)) return false;
    }
    return true;
}

bool SurfaceBoundsAreFinite(const GfxSurface &surface) noexcept
{
    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        if (!std::isfinite(surface.bounds[0][axis]) ||
            !std::isfinite(surface.bounds[1][axis]) ||
            surface.bounds[0][axis] > surface.bounds[1][axis])
        {
            return false;
        }
    }
    return true;
}

bool SurfaceUsesSkyPass(const GfxSurface &surface) noexcept
{
    // R_LoadWorld marks sky materials with gameFlags bit 3 and canonical
    // traversal submits them through R_AddSkySurfacesDpvs, not the ordinary
    // opaque world-surface pass. The backend draws s_world.skyImage as a
    // cubemap before opaque geometry, so its enclosing geometry must not hide
    // the world.
    return surface.material && (surface.material->info.gameFlags & 8u) != 0u;
}

bool MaterialUsesWater(const Material *material) noexcept
{
    if (!material || !material->textureTable) return false;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 11u && texture.u.water) return true;
    }
    return false;
}

const water_t *FindWater(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 11u && texture.u.water)
        {
            sampler = texture.samplerState;
            return texture.u.water;
        }
    }
    return nullptr;
}

bool CopyMaterialConstant(
    const Material *material,
    std::uint32_t nameHash,
    float output[4]) noexcept
{
    if (!material || !material->constantTable) return false;
    for (std::uint32_t index = 0u; index < material->constantCount; ++index)
    {
        const MaterialConstantDef &constant = material->constantTable[index];
        if (constant.nameHash == nameHash)
        {
            std::copy_n(constant.literal, 4u, output);
            return true;
        }
    }
    return false;
}

bool SurfaceRangeIsValid(const GfxWorld &world, const GfxSurface &surface) noexcept
{
    if (surface.tris.firstVertex < 0 || surface.tris.baseIndex < 0 ||
        surface.tris.vertexCount == 0u || surface.tris.triCount == 0u)
    {
        return false;
    }
    const std::uint32_t firstVertex =
        static_cast<std::uint32_t>(surface.tris.firstVertex);
    const std::uint32_t firstIndex =
        static_cast<std::uint32_t>(surface.tris.baseIndex);
    const std::uint32_t indexCount =
        static_cast<std::uint32_t>(surface.tris.triCount) * 3u;
    return firstVertex <= world.vertexCount &&
        surface.tris.vertexCount <= world.vertexCount - firstVertex &&
        firstIndex <= static_cast<std::uint32_t>(world.indexCount) &&
        indexCount <= static_cast<std::uint32_t>(world.indexCount) - firstIndex;
}

const GfxImage *FindBaseImage(const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 2u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

const GfxImage *FindNormalImage(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 5u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

std::uint32_t HashPixelShaderProgram(
    const MaterialPixelShader *shader) noexcept
{
    if (!shader || !shader->prog.loadDef.program ||
        shader->prog.loadDef.programSize == 0u)
    {
        return 0u;
    }
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

struct TechniqueSelection
{
    std::uint32_t type = TECHNIQUE_NONE_INDEX;
    const MaterialTechnique *technique = nullptr;
    const char *identityName = nullptr;
    const char *pixelShaderName = nullptr;
    std::uint32_t pixelShaderProgramHash = 0u;
    std::uint8_t customSamplerFlags = 0u;
    std::uint16_t techniqueFlags = 0u;
    std::uint32_t stateBits[2]{};
};

bool IsCanonicalWorldColorLitAlias(const MaterialTechniqueSet *techniqueSet)
    noexcept
{
    return techniqueSet && techniqueSet->name &&
        std::strncmp(techniqueSet->name, ",wc_l_", 6u) == 0;
}

bool ShaderNameIs(const TechniqueSelection &technique, const char *name)
    noexcept
{
    return technique.pixelShaderName &&
        std::strcmp(technique.pixelShaderName, name) == 0;
}

bool UsesDirectionalNormalMap(
    const TechniqueSelection &technique,
    const GfxImage *normalImage) noexcept
{
    if (!normalImage) return false;
    if (ShaderNameIs(technique, "lm_r0c0n0_sm2.hlsl") ||
        ShaderNameIs(technique, "lm_t0c0n0_sm2.hlsl") ||
        (technique.pixelShaderName &&
            std::strncmp(technique.pixelShaderName,
                "lm_spot_", 8u) == 0 &&
            std::strstr(technique.pixelShaderName, "n0") != nullptr) ||
        (technique.pixelShaderName &&
            std::strncmp(technique.pixelShaderName, "lm_sm_sun_", 10u) == 0 &&
            std::strstr(technique.pixelShaderName, "n0") != nullptr))
    {
        return true;
    }
    return technique.identityName &&
        std::strncmp(technique.identityName, ",wc_l_", 6u) == 0 &&
        std::strstr(technique.identityName, "n0") != nullptr;
}

std::uint32_t SelectLitTechniqueType(
    std::uint8_t primaryLightIndex,
    const WebRendererWorldLightTechniqueContext *lightContext,
    bool sunShadowEnabled) noexcept
{
    if (!lightContext || !lightContext->primaryLights ||
        primaryLightIndex >= lightContext->primaryLightCount)
    {
        return sunShadowEnabled
            ? TECHNIQUE_LIT_SUN_SHADOW_INDEX : TECHNIQUE_LIT_INDEX;
    }
    const WebRendererPrimaryLightDesc &light =
        lightContext->primaryLights[primaryLightIndex];
    switch (light.type)
    {
    case 0u:
        return TECHNIQUE_LIT_INDEX;
    case 1u:
        return lightContext->sunShadowEnabled &&
                primaryLightIndex == lightContext->sunPrimaryLightIndex
            ? TECHNIQUE_LIT_SUN_SHADOW_INDEX : TECHNIQUE_LIT_SUN_INDEX;
    case 2u:
        return TECHNIQUE_LIT_SPOT_INDEX;
    case 3u:
        return TECHNIQUE_LIT_OMNI_INDEX;
    default:
        return TECHNIQUE_NONE_INDEX;
    }
}

TechniqueSelection SelectTechnique(
    const Material *material, std::uint32_t litTechniqueType) noexcept
{
    TechniqueSelection selection;
    if (!material || !material->techniqueSet || !material->stateBitsTable)
        return selection;
    const MaterialTechniqueSet *techniqueSet =
        material->techniqueSet->remappedTechniqueSet
            ? material->techniqueSet->remappedTechniqueSet
            : material->techniqueSet;
    const bool localLight = litTechniqueType == TECHNIQUE_LIT_SPOT_INDEX ||
        litTechniqueType == TECHNIQUE_LIT_OMNI_INDEX;
    const std::array<std::uint32_t, 4u> types = localLight
        ? std::array<std::uint32_t, 4u>{
            litTechniqueType, TECHNIQUE_NONE_INDEX,
            TECHNIQUE_NONE_INDEX, TECHNIQUE_NONE_INDEX}
        : std::array<std::uint32_t, 4u>{
            litTechniqueType, TECHNIQUE_LIT_INDEX,
            TECHNIQUE_UNLIT_INDEX, TECHNIQUE_EMISSIVE_INDEX};
    for (const std::uint32_t type : types)
    {
        if (type == TECHNIQUE_NONE_INDEX) continue;
        const MaterialTechnique *technique =
            techniqueSet->techniques[type];
        const std::uint8_t entry = material->stateBitsEntry[type];
        if (!technique || technique->passCount == 0u ||
            entry == 0xffu || entry >= material->stateBitsCount)
        {
            continue;
        }
        selection.type = type;
        selection.technique = technique;
        selection.identityName = technique->name;
        selection.techniqueFlags = technique->flags;
        for (std::uint32_t pass = 0u; pass < technique->passCount; ++pass)
            selection.customSamplerFlags |=
                technique->passArray[pass].customSamplerFlags;
        const MaterialPixelShader *pixelShader =
            technique->passArray[0u].pixelShader;
        selection.pixelShaderName = pixelShader ? pixelShader->name : nullptr;
        selection.pixelShaderProgramHash = HashPixelShaderProgram(pixelShader);
        selection.stateBits[0] = material->stateBitsTable[entry].loadBits[0];
        selection.stateBits[1] = material->stateBitsTable[entry].loadBits[1];
        return selection;
    }
    // Retail world-color aliases preserve canonical material/state identity
    // but intentionally omit their D3D pass pointers. Native recognizes this
    // named lit family during technique remapping. Carry the same family into
    // the portable diffuse/lightmap pass instead of turning the surface into
    // backend fallback geometry.
    const std::uint8_t litEntry =
        material->stateBitsEntry[TECHNIQUE_LIT_INDEX];
    if (litTechniqueType == TECHNIQUE_LIT_INDEX &&
        IsCanonicalWorldColorLitAlias(material->techniqueSet) &&
        litEntry != 0xffu && litEntry < material->stateBitsCount)
    {
        selection.type = TECHNIQUE_LIT_INDEX;
        selection.identityName = material->techniqueSet->name;
        // Killhouse resolves this alias to the same lm_r0c0_sm2 native family
        // whose pass requests sampler bit 0x04 (secondary lightmap only).
        selection.customSamplerFlags = 4u;
        selection.stateBits[0] =
            material->stateBitsTable[litEntry].loadBits[0];
        selection.stateBits[1] =
            material->stateBitsTable[litEntry].loadBits[1];
    }
    return selection;
}

WebRendererWorldBatchDesc MakeBatch(
    const GfxWorld &world,
    const GfxSurface &surface,
    std::uint32_t surfaceIndex,
    std::uint32_t firstIndex,
    bool sunShadowEnabled,
    const WebRendererWorldLightTechniqueContext *lightContext) noexcept
{
    WebRendererWorldBatchDesc batch{};
    batch.firstIndex = firstIndex;
    batch.surfaceCount = 1u;
    batch.firstSurfaceIndex = surfaceIndex;
    batch.lastSurfaceIndex = surfaceIndex;
    batch.materialIdentity = surface.material;
    batch.materialName = surface.material && surface.material->info.name
        ? surface.material->info.name : "<null-material>";
    batch.lightmapIndex = surface.lightmapIndex;
    batch.primaryLightIndex = surface.primaryLightIndex;
    batch.modelIdentity = nullptr;
    batch.modelName = "<world>";
    batch.firstInstanceIndex = UINT32_MAX;
    batch.lastInstanceIndex = UINT32_MAX;
    batch.sourceKind = WebRendererSceneBatchKind::WorldSurface;
    batch.samplerState = 0u;
    batch.baseImage = FindBaseImage(surface.material, batch.samplerState);
    batch.normalImage = FindNormalImage(
        surface.material, batch.normalSamplerState);
    batch.water = FindWater(surface.material, batch.waterSamplerState);
    if (batch.water &&
        surface.reflectionProbeIndex < world.reflectionProbeCount &&
        world.reflectionProbes)
    {
        batch.reflectionProbeIndex = surface.reflectionProbeIndex;
        batch.reflectionProbeImage = world.reflectionProbes[
            surface.reflectionProbeIndex].reflectionImage;
    }
    const std::uint32_t requestedTechniqueType = SelectLitTechniqueType(
        surface.primaryLightIndex, lightContext, sunShadowEnabled);
    const TechniqueSelection technique = SelectTechnique(
        surface.material, requestedTechniqueType);
    batch.techniqueName = technique.identityName
        ? technique.identityName : "<unsupported-technique>";
    batch.techniqueType = static_cast<std::uint8_t>(technique.type);
    batch.customSamplerFlags = technique.customSamplerFlags;
    batch.techniqueFlags = technique.techniqueFlags;
    batch.pixelShaderName = technique.pixelShaderName
        ? technique.pixelShaderName : "<unavailable-pixel-shader>";
    batch.pixelShaderProgramHash = technique.pixelShaderProgramHash;
    batch.stateBits[0] = technique.stateBits[0];
    batch.stateBits[1] = technique.stateBits[1];
    const bool hasCanonicalLightmap = technique.identityName &&
        technique.type >= TECHNIQUE_LIT_INDEX &&
        technique.type <= 13u &&
        surface.lightmapIndex != 31u &&
        surface.lightmapIndex < world.lightmapCount && world.lightmaps;
    if (hasCanonicalLightmap)
    {
        if ((technique.customSamplerFlags & 2u) != 0u)
            batch.lightmapImage =
                world.lightmaps[surface.lightmapIndex].primary;
        if ((technique.customSamplerFlags & 4u) != 0u)
            batch.secondaryLightmapImage =
                world.lightmaps[surface.lightmapIndex].secondary;
        if (batch.secondaryLightmapImage)
            batch.lightingMode =
                WebRendererWorldLightingMode::SecondaryDirectional;
    }
    constexpr std::uint32_t ENV_MAP_PARMS_HASH = 0x3d9994dcu;
    constexpr std::uint32_t WATER_COLOR_HASH = 0xb82a51e8u;
    const bool canonicalWater = ShaderNameIs(technique, "water_l_sun.hlsl") &&
        batch.water && batch.reflectionProbeImage &&
        CopyMaterialConstant(surface.material, ENV_MAP_PARMS_HASH,
            batch.envMapParms) &&
        CopyMaterialConstant(surface.material, WATER_COLOR_HASH,
            batch.waterColor);
    if (canonicalWater)
        batch.technique = WebRendererWorldTechnique::WaterLitSun;
    else if (!technique.identityName &&
        (requestedTechniqueType == TECHNIQUE_LIT_SPOT_INDEX ||
            requestedTechniqueType == TECHNIQUE_LIT_OMNI_INDEX))
    {
        batch.technique =
            WebRendererWorldTechnique::NativeTechniqueUnavailable;
    }
    else if (!technique.identityName || !batch.baseImage)
        batch.technique = WebRendererWorldTechnique::BackendFallback;
    else if (ShaderNameIs(technique, "mul.hlsl"))
        batch.technique = WebRendererWorldTechnique::VertexColorMultiply;
    else if (ShaderNameIs(
            technique, "vertcol_simple_add_fog.hlsl"))
        batch.technique = WebRendererWorldTechnique::VertexColorAdditive;
    else if (batch.lightingMode ==
            WebRendererWorldLightingMode::SecondaryDirectional)
        batch.technique = UsesDirectionalNormalMap(
                technique, batch.normalImage)
            ? WebRendererWorldTechnique::BaseTextureLightmapNormal
            : WebRendererWorldTechnique::BaseTextureLightmap;
    else
        batch.technique = WebRendererWorldTechnique::BaseTexture;
    batch.castsSunShadow = world.dpvs.surfaceCastsSunShadow &&
        (world.dpvs.surfaceCastsSunShadow[surfaceIndex >> 5u] &
            (1u << (surfaceIndex & 31u))) != 0u;
    return batch;
}

bool BatchMatches(
    const WebRendererWorldBatchDesc &batch,
    const WebRendererWorldBatchDesc &candidate) noexcept
{
    return batch.materialIdentity == candidate.materialIdentity &&
        batch.modelIdentity == candidate.modelIdentity &&
        batch.sourceKind == candidate.sourceKind &&
        batch.baseImage == candidate.baseImage &&
        batch.normalImage == candidate.normalImage &&
        batch.lightmapImage == candidate.lightmapImage &&
        batch.secondaryLightmapImage == candidate.secondaryLightmapImage &&
        batch.water == candidate.water &&
        batch.reflectionProbeImage == candidate.reflectionProbeImage &&
        batch.lightingMode == candidate.lightingMode &&
        batch.customSamplerFlags == candidate.customSamplerFlags &&
        batch.techniqueFlags == candidate.techniqueFlags &&
        batch.pixelShaderProgramHash == candidate.pixelShaderProgramHash &&
        batch.stateBits[0] == candidate.stateBits[0] &&
        batch.stateBits[1] == candidate.stateBits[1] &&
        batch.samplerState == candidate.samplerState &&
        batch.normalSamplerState == candidate.normalSamplerState &&
        batch.waterSamplerState == candidate.waterSamplerState &&
        batch.reflectionProbeIndex == candidate.reflectionProbeIndex &&
        batch.lightmapIndex == candidate.lightmapIndex &&
        batch.primaryLightIndex == candidate.primaryLightIndex &&
        batch.technique == candidate.technique &&
        std::memcmp(batch.envMapParms, candidate.envMapParms,
            sizeof(batch.envMapParms)) == 0 &&
        std::memcmp(batch.waterColor, candidate.waterColor,
            sizeof(batch.waterColor)) == 0 &&
        batch.castsSunShadow == candidate.castsSunShadow;
}

} // namespace

WebRendererWorldSceneResult WebRenderer_BuildWorldSceneCommand(
    const GfxWorld &world,
    const WebRendererSceneViewDesc &view,
    WebRendererWorldSceneCommand &destination,
    const WebRendererWorldLightTechniqueContext *lightContext)
{
    if (world.surfaceCount <= 0 || world.vertexCount == 0u ||
        world.indexCount <= 0 || !world.vd.vertices || !world.indices ||
        !world.dpvs.surfaces || world.modelCount <= 0 || !world.models)
    {
        return WebRendererWorldSceneResult::InvalidWorld;
    }
    if (!ViewIsValid(view))
        return WebRendererWorldSceneResult::InvalidView;

    const GfxBrushModel &worldModel = world.models[0];
    const std::uint32_t modelBegin = worldModel.startSurfIndex;
    const std::uint32_t modelEnd = modelBegin + worldModel.surfaceCount;
    if (modelBegin > static_cast<std::uint32_t>(world.surfaceCount) ||
        worldModel.surfaceCount >
            static_cast<std::uint32_t>(world.surfaceCount) - modelBegin)
    {
        return WebRendererWorldSceneResult::InvalidSurfaceRange;
    }

    struct SurfaceRange
    {
        std::uint32_t begin;
        std::uint32_t end;
    };
    std::array<SurfaceRange, 3u> ranges{};
    std::size_t rangeCount = 0u;
    const bool canonicalRangesValid =
        world.dpvs.litSurfsBegin >= modelBegin &&
        world.dpvs.litSurfsBegin <= world.dpvs.litSurfsEnd &&
        world.dpvs.litSurfsEnd <= world.dpvs.decalSurfsBegin &&
        world.dpvs.decalSurfsBegin <= world.dpvs.decalSurfsEnd &&
        world.dpvs.decalSurfsEnd <= world.dpvs.emissiveSurfsBegin &&
        world.dpvs.emissiveSurfsBegin <= world.dpvs.emissiveSurfsEnd &&
        world.dpvs.emissiveSurfsEnd <= modelEnd &&
        world.dpvs.litSurfsEnd > world.dpvs.litSurfsBegin;
    if (canonicalRangesValid)
    {
        ranges[rangeCount++] = {
            world.dpvs.litSurfsBegin, world.dpvs.litSurfsEnd};
        if (world.dpvs.decalSurfsEnd > world.dpvs.decalSurfsBegin)
            ranges[rangeCount++] = {
                world.dpvs.decalSurfsBegin, world.dpvs.decalSurfsEnd};
        if (world.dpvs.emissiveSurfsEnd > world.dpvs.emissiveSurfsBegin)
            ranges[rangeCount++] = {
                world.dpvs.emissiveSurfsBegin,
                world.dpvs.emissiveSurfsEnd};
    }
    else
    {
        const std::uint32_t fallbackCount =
            worldModel.surfaceCountNoDecal != 0u
                ? worldModel.surfaceCountNoDecal
                : worldModel.surfaceCount;
        ranges[rangeCount++] = {modelBegin, modelBegin + fallbackCount};
    }

    WebRendererWorldSceneCommand replacement;
    try
    {
        std::vector<std::uint32_t> vertexRemap(
            world.vertexCount, std::numeric_limits<std::uint32_t>::max());
        std::vector<const Material *> waterMaterials;
        replacement.vertices.reserve(world.vertexCount);
        replacement.indices.reserve(static_cast<std::size_t>(world.indexCount));
        for (std::size_t rangeIndex = 0u;
             rangeIndex < rangeCount; ++rangeIndex)
        {
          for (std::uint32_t surfaceIndex = ranges[rangeIndex].begin;
               surfaceIndex < ranges[rangeIndex].end; ++surfaceIndex)
          {
            const GfxSurface &surface = world.dpvs.surfaces[surfaceIndex];
            if (SurfaceUsesSkyPass(surface) ||
                surface.tris.vertexCount == 0u || surface.tris.triCount == 0u)
            {
                continue;
            }
            if (!SurfaceBoundsAreFinite(surface))
                return WebRendererWorldSceneResult::InvalidSurfaceBounds;
            if (!SurfaceRangeIsValid(world, surface))
                return WebRendererWorldSceneResult::InvalidSurfaceRange;
            const std::uint32_t vertexCount = surface.tris.vertexCount;
            const std::uint32_t indexCount =
                static_cast<std::uint32_t>(surface.tris.triCount) * 3u;
            const std::uint32_t firstIndex =
                static_cast<std::uint32_t>(surface.tris.baseIndex);
            for (std::uint32_t index = 0u; index < indexCount; ++index)
            {
                if (world.indices[firstIndex + index] >= vertexCount)
                    return WebRendererWorldSceneResult::IndexOutOfRange;
            }

            const std::uint32_t sourceBase =
                static_cast<std::uint32_t>(surface.tris.firstVertex);
            for (std::uint32_t index = 0u; index < indexCount; ++index)
            {
                const std::uint32_t localIndex = world.indices[firstIndex + index];
                const std::uint32_t vertexIndex = sourceBase + localIndex;
                std::uint32_t &destinationIndex = vertexRemap[vertexIndex];
                if (destinationIndex != std::numeric_limits<std::uint32_t>::max())
                {
                    replacement.indices.push_back(destinationIndex);
                    continue;
                }
                const GfxWorldVertex &source =
                    world.vd.vertices[vertexIndex];
                if (!Finite3(source.xyz) ||
                    !std::isfinite(source.texCoord[0]) ||
                    !std::isfinite(source.texCoord[1]))
                {
                    return WebRendererWorldSceneResult::InvalidSurfaceBounds;
                }
                WebRendererSurfaceVertex vertex{};
                std::copy(std::begin(source.xyz), std::end(source.xyz),
                    std::begin(vertex.position));
                vertex.color[0] = static_cast<float>(
                    (source.color.packed >> 16u) & 0xffu) * BYTE_TO_UNIT;
                vertex.color[1] = static_cast<float>(
                    (source.color.packed >> 8u) & 0xffu) * BYTE_TO_UNIT;
                vertex.color[2] = static_cast<float>(
                    source.color.packed & 0xffu) * BYTE_TO_UNIT;
                vertex.color[3] = static_cast<float>(
                    (source.color.packed >> 24u) & 0xffu) * BYTE_TO_UNIT;
                vertex.textureCoordinate[0] = source.texCoord[0];
                vertex.textureCoordinate[1] = source.texCoord[1];
                vertex.lightmapCoordinate[0] = source.lmapCoord[0];
                vertex.lightmapCoordinate[1] = source.lmapCoord[1];
                UnpackUnitVec(source.normal, vertex.normal);
                UnpackUnitVec(source.tangent, vertex.tangent);
                vertex.binormalSign = source.binormalSign;
                if (!Finite3(vertex.normal) || !Finite3(vertex.tangent) ||
                    !std::isfinite(vertex.binormalSign))
                {
                    return WebRendererWorldSceneResult::InvalidSurfaceBounds;
                }
                destinationIndex =
                    static_cast<std::uint32_t>(replacement.vertices.size());
                replacement.vertices.push_back(vertex);
                replacement.indices.push_back(destinationIndex);
            }
            WebRendererWorldBatchDesc candidate = MakeBatch(
                world,
                surface,
                surfaceIndex,
                static_cast<std::uint32_t>(replacement.indices.size() - indexCount),
                view.sunShadowEnabled, lightContext);
            candidate.indexCount = indexCount;
            if (MaterialUsesWater(surface.material))
            {
                ++replacement.waterSurfaceCount;
                if (std::find(waterMaterials.begin(), waterMaterials.end(),
                        surface.material) == waterMaterials.end())
                {
                    waterMaterials.push_back(surface.material);
                    replacement.waterMaterialCount =
                        static_cast<std::uint32_t>(waterMaterials.size());
                }
            }
            if ((candidate.techniqueFlags & 2u) != 0u)
                ++replacement.resolvedSceneSurfaceCount;
            if ((candidate.techniqueFlags & 1u) != 0u)
                ++replacement.resolvedPostSunSurfaceCount;
            if (!replacement.batches.empty() &&
                BatchMatches(replacement.batches.back(), candidate) &&
                replacement.batches.back().firstIndex +
                    replacement.batches.back().indexCount == candidate.firstIndex)
            {
                WebRendererWorldBatchDesc &batch = replacement.batches.back();
                batch.indexCount += candidate.indexCount;
                ++batch.surfaceCount;
                batch.lastSurfaceIndex = surfaceIndex;
            }
            else
            {
                replacement.batches.push_back(candidate);
            }
            if (replacement.surfaceCount == 0u)
                replacement.firstSurfaceIndex = surfaceIndex;
            replacement.lastSurfaceIndex = surfaceIndex;
            ++replacement.surfaceCount;
          }
        }

    }
    catch (const std::bad_alloc &)
    {
        return WebRendererWorldSceneResult::AllocationFailed;
    }

    if (replacement.surfaceCount == 0u)
        return WebRendererWorldSceneResult::NoVisibleSurface;
    destination = std::move(replacement);
    return WebRendererWorldSceneResult::Success;
}

WebRendererWorldSceneResult WebRenderer_BuildBrushModelSceneCommand(
    const GfxWorld &world,
    const WebRendererBrushModelSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererBrushModelSceneCommand &destination)
{
    if (world.surfaceCount <= 0 || world.vertexCount == 0u ||
        world.indexCount <= 0 || !world.vd.vertices || !world.indices ||
        !world.dpvs.surfaces || world.modelCount <= 0 || !world.models)
    {
        return WebRendererWorldSceneResult::InvalidWorld;
    }
    if (submissionCount == 0u)
        return WebRendererWorldSceneResult::NoVisibleSurface;
    if (!submissions ||
        submissionCount > WEB_RENDERER_MAX_DYNAMIC_BMODEL_SUBMISSIONS)
    {
        return WebRendererWorldSceneResult::InvalidSurfaceRange;
    }

    WebRendererBrushModelSceneCommand replacement;
    try
    {
        constexpr std::uint32_t UNMAPPED =
            std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> vertexRemap(world.vertexCount, UNMAPPED);
        std::vector<std::uint32_t> touchedVertices;
        for (std::uint32_t submissionIndex = 0u;
             submissionIndex < submissionCount; ++submissionIndex)
        {
            const WebRendererBrushModelSubmission &submission =
                submissions[submissionIndex];
            const GfxBrushModel *model = submission.model;
            if (!model || !Finite3(submission.origin))
                return WebRendererWorldSceneResult::InvalidSurfaceRange;
            for (const auto &axis : submission.axis)
                if (!Finite3(axis))
                    return WebRendererWorldSceneResult::InvalidSurfaceBounds;

            const std::uint32_t surfaceBegin = model->startSurfIndex;
            const std::uint32_t surfaceEnd =
                surfaceBegin + model->surfaceCount;
            if (surfaceBegin > static_cast<std::uint32_t>(world.surfaceCount) ||
                model->surfaceCount >
                    static_cast<std::uint32_t>(world.surfaceCount) -
                        surfaceBegin)
            {
                return WebRendererWorldSceneResult::InvalidSurfaceRange;
            }
            const std::size_t firstSubmissionBatch =
                replacement.batches.size();
            std::uint32_t emittedSurfaces = 0u;
            for (std::uint32_t surfaceIndex = surfaceBegin;
                 surfaceIndex < surfaceEnd; ++surfaceIndex)
            {
                const GfxSurface &surface = world.dpvs.surfaces[surfaceIndex];
                if (SurfaceUsesSkyPass(surface) ||
                    surface.tris.vertexCount == 0u ||
                    surface.tris.triCount == 0u)
                {
                    continue;
                }
                if (!SurfaceBoundsAreFinite(surface))
                    return WebRendererWorldSceneResult::InvalidSurfaceBounds;
                if (!SurfaceRangeIsValid(world, surface))
                    return WebRendererWorldSceneResult::InvalidSurfaceRange;

                const std::uint32_t vertexCount = surface.tris.vertexCount;
                const std::uint32_t indexCount =
                    static_cast<std::uint32_t>(surface.tris.triCount) * 3u;
                const std::uint32_t firstSourceIndex =
                    static_cast<std::uint32_t>(surface.tris.baseIndex);
                const std::uint32_t sourceVertexBase =
                    static_cast<std::uint32_t>(surface.tris.firstVertex);
                if (replacement.indices.size() >
                    WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES - indexCount)
                {
                    return WebRendererWorldSceneResult::OutputTooLarge;
                }
                const std::uint32_t firstDestinationIndex =
                    static_cast<std::uint32_t>(replacement.indices.size());
                for (std::uint32_t index = 0u; index < indexCount; ++index)
                {
                    const std::uint32_t localIndex =
                        world.indices[firstSourceIndex + index];
                    if (localIndex >= vertexCount)
                        return WebRendererWorldSceneResult::IndexOutOfRange;
                    const std::uint32_t sourceVertexIndex =
                        sourceVertexBase + localIndex;
                    std::uint32_t &destinationIndex =
                        vertexRemap[sourceVertexIndex];
                    if (destinationIndex == UNMAPPED)
                    {
                        if (replacement.vertices.size() >=
                            WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES)
                        {
                            return WebRendererWorldSceneResult::OutputTooLarge;
                        }
                        const GfxWorldVertex &source =
                            world.vd.vertices[sourceVertexIndex];
                        if (!Finite3(source.xyz) ||
                            !std::isfinite(source.texCoord[0]) ||
                            !std::isfinite(source.texCoord[1]) ||
                            !std::isfinite(source.lmapCoord[0]) ||
                            !std::isfinite(source.lmapCoord[1]))
                        {
                            return WebRendererWorldSceneResult::InvalidSurfaceBounds;
                        }
                        WebRendererSurfaceVertex vertex{};
                        for (std::size_t component = 0u;
                             component < 3u; ++component)
                        {
                            vertex.position[component] =
                                source.xyz[0] * submission.axis[0][component] +
                                source.xyz[1] * submission.axis[1][component] +
                                source.xyz[2] * submission.axis[2][component] +
                                submission.origin[component];
                        }
                        vertex.color[0] = static_cast<float>(
                            (source.color.packed >> 16u) & 0xffu) * BYTE_TO_UNIT;
                        vertex.color[1] = static_cast<float>(
                            (source.color.packed >> 8u) & 0xffu) * BYTE_TO_UNIT;
                        vertex.color[2] = static_cast<float>(
                            source.color.packed & 0xffu) * BYTE_TO_UNIT;
                        vertex.color[3] = static_cast<float>(
                            (source.color.packed >> 24u) & 0xffu) * BYTE_TO_UNIT;
                        std::copy_n(source.texCoord, 2u,
                            vertex.textureCoordinate);
                        std::copy_n(source.lmapCoord, 2u,
                            vertex.lightmapCoordinate);
                        float sourceNormal[3]{};
                        float sourceTangent[3]{};
                        UnpackUnitVec(source.normal, sourceNormal);
                        UnpackUnitVec(source.tangent, sourceTangent);
                        for (std::size_t component = 0u;
                             component < 3u; ++component)
                        {
                            vertex.normal[component] =
                                sourceNormal[0] * submission.axis[0][component] +
                                sourceNormal[1] * submission.axis[1][component] +
                                sourceNormal[2] * submission.axis[2][component];
                            vertex.tangent[component] =
                                sourceTangent[0] * submission.axis[0][component] +
                                sourceTangent[1] * submission.axis[1][component] +
                                sourceTangent[2] * submission.axis[2][component];
                        }
                        vertex.binormalSign = source.binormalSign;
                        if (!Finite3(vertex.normal) ||
                            !Finite3(vertex.tangent) ||
                            !std::isfinite(vertex.binormalSign))
                        {
                            return WebRendererWorldSceneResult::InvalidSurfaceBounds;
                        }
                        destinationIndex = static_cast<std::uint32_t>(
                            replacement.vertices.size());
                        replacement.vertices.push_back(vertex);
                        touchedVertices.push_back(sourceVertexIndex);
                    }
                    replacement.indices.push_back(destinationIndex);
                }

                WebRendererWorldBatchDesc candidate = MakeBatch(
                    world, surface, surfaceIndex, firstDestinationIndex,
                    false, nullptr);
                candidate.indexCount = indexCount;
                candidate.sourceKind =
                    WebRendererSceneBatchKind::DynamicBModel;
                candidate.modelName = "<brush-model>";
                if (replacement.batches.size() > firstSubmissionBatch &&
                    BatchMatches(replacement.batches.back(), candidate) &&
                    replacement.batches.back().firstIndex +
                        replacement.batches.back().indexCount ==
                            candidate.firstIndex)
                {
                    WebRendererWorldBatchDesc &batch =
                        replacement.batches.back();
                    batch.indexCount += candidate.indexCount;
                    ++batch.surfaceCount;
                    batch.lastSurfaceIndex = surfaceIndex;
                }
                else
                {
                    replacement.batches.push_back(candidate);
                }
                ++replacement.surfaceCount;
                ++emittedSurfaces;
            }
            for (const std::uint32_t sourceVertexIndex : touchedVertices)
                vertexRemap[sourceVertexIndex] = UNMAPPED;
            touchedVertices.clear();
            if (emittedSurfaces != 0u) ++replacement.modelCount;
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererWorldSceneResult::AllocationFailed;
    }
    if (replacement.surfaceCount == 0u)
        return WebRendererWorldSceneResult::NoVisibleSurface;
    destination = std::move(replacement);
    return WebRendererWorldSceneResult::Success;
}

const char *WebRenderer_WorldSceneResultString(
    WebRendererWorldSceneResult result) noexcept
{
    switch (result)
    {
    case WebRendererWorldSceneResult::Success: return "success";
    case WebRendererWorldSceneResult::InvalidWorld:
        return "invalid canonical GfxWorld";
    case WebRendererWorldSceneResult::InvalidView:
        return "invalid canonical cgame view";
    case WebRendererWorldSceneResult::InvalidSurfaceRange:
        return "world surface range is outside canonical arrays";
    case WebRendererWorldSceneResult::InvalidSurfaceBounds:
        return "world surface contains invalid bounds or vertices";
    case WebRendererWorldSceneResult::IndexOutOfRange:
        return "world surface index is outside its local vertex range";
    case WebRendererWorldSceneResult::OutputTooLarge:
        return "combined world/static XModel command exceeds backend limits";
    case WebRendererWorldSceneResult::NoVisibleSurface:
        return "canonical base world has no opaque surfaces for this pass";
    case WebRendererWorldSceneResult::AllocationFailed:
        return "portable world scene command allocation failed";
    }
    return "unknown portable world scene error";
}
