#include <web/web_renderer_world_scene.h>
#include <web/web_renderer_material_lookup.h>
#include <web/web_frame_profile.h>
#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/r_dynamiclights_core.h>

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
constexpr std::uint32_t TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX = 2u;
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
constexpr std::uint32_t TECHNIQUE_LIT_SUN_INDEX = 8u;
constexpr std::uint32_t TECHNIQUE_LIT_SUN_SHADOW_INDEX = 9u;
constexpr std::uint32_t TECHNIQUE_LIT_SPOT_INDEX = 10u;
constexpr std::uint32_t TECHNIQUE_LIT_OMNI_INDEX = 12u;
constexpr std::uint32_t TECHNIQUE_NONE_INDEX = 36u;
constexpr std::uint32_t DETAIL_SCALE_HASH = 0x08d36a09u;

void UnpackUnitVec(PackedUnitVec packed, float output[3]) noexcept
{
    const float scale =
        (static_cast<float>(packed.array[3]) + 192.0f) / 32385.0f;
    output[0] = (static_cast<float>(packed.array[0]) - 127.0f) * scale;
    output[1] = (static_cast<float>(packed.array[1]) - 127.0f) * scale;
    output[2] = (static_cast<float>(packed.array[2]) - 127.0f) * scale;
}

bool ValidatePrimaryLightFrame(
    const WebRendererPrimaryLightDesc *primaryLights,
    std::uint32_t primaryLightCount) noexcept
{
    if (primaryLightCount > WEB_RENDERER_MAX_PRIMARY_LIGHTS ||
        (primaryLightCount == 0u) != (primaryLights == nullptr))
    {
        return false;
    }
    for (std::uint32_t index = 0u; index < primaryLightCount; ++index)
    {
        const WebRendererPrimaryLightDesc &light = primaryLights[index];
        if (light.type > 3u || !std::isfinite(light.radius) ||
            !std::isfinite(light.cosHalfFovOuter) ||
            !std::isfinite(light.cosHalfFovInner) ||
            !std::isfinite(light.falloffScale) ||
            !std::isfinite(light.falloffShift))
        {
            return false;
        }
        for (std::size_t component = 0u; component < 3u; ++component)
        {
            if (!std::isfinite(light.color[component]) ||
                !std::isfinite(light.direction[component]) ||
                !std::isfinite(light.origin[component]))
            {
                return false;
            }
        }
        const bool localLight = light.type == 2u || light.type == 3u;
        if ((localLight && (light.radius <= 0.0f ||
                light.falloffScale <= 0.0f ||
                light.falloffShift < 0.0f ||
                light.falloffShift + light.falloffScale > 1.0f)) ||
            (light.type == 2u &&
                light.cosHalfFovInner <= light.cosHalfFovOuter))
        {
            return false;
        }
    }
    return true;
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

std::uint32_t HashVertexShaderProgram(
    const MaterialVertexShader *shader) noexcept
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
    const char *vertexShaderName = nullptr;
    std::uint32_t vertexShaderProgramHash = 0u;
    const char *pixelShaderName = nullptr;
    std::uint32_t pixelShaderProgramHash = 0u;
    std::uint8_t customSamplerFlags = 0u;
    std::uint16_t techniqueFlags = 0u;
    std::uint32_t stateBits[2]{};
};

// Shader programs are immutable during one synchronous brush build. Reuse
// only the preceding hashes, including across materials that share shaders.
// Never retain canonical pointers or derived values across builds.
struct LastBrushShaders
{
    const MaterialVertexShader *vertex = nullptr;
    const MaterialPixelShader *pixel = nullptr;
    std::uint32_t vertexHash = 0u;
    std::uint32_t pixelHash = 0u;
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

bool VertexShaderNameIs(
    const TechniqueSelection &technique, const char *name) noexcept
{
    return technique.vertexShaderName &&
        std::strcmp(technique.vertexShaderName, name) == 0;
}

bool UsesDirectionalNormalMap(
    const TechniqueSelection &technique,
    const GfxImage *normalImage) noexcept
{
    if (!normalImage) return false;
    if (ShaderNameIs(technique, "lm_r0c0n0_sm2.hlsl") ||
        ShaderNameIs(technique, "lm_t0c0n0_sm2.hlsl") ||
        (technique.pixelShaderName &&
            std::strncmp(technique.pixelShaderName, "lm_", 3u) == 0 &&
            std::strstr(technique.pixelShaderName, "n0") != nullptr &&
            std::strstr(technique.pixelShaderName, "_sm3.hlsl") != nullptr) ||
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

bool UsesSm3EnvironmentSpecular(
    const TechniqueSelection &technique,
    const GfxImage *specularImage) noexcept
{
    return specularImage && technique.pixelShaderName &&
        std::strncmp(technique.pixelShaderName, "lm_", 3u) == 0 &&
        std::strstr(technique.pixelShaderName, "s0_sm3.hlsl") != nullptr;
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
    const Material *material, std::uint32_t litTechniqueType,
    LastBrushShaders *lastShaders) noexcept
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
        const MaterialVertexShader *vertexShader =
            technique->passArray[0u].vertexShader;
        selection.vertexShaderName =
            vertexShader ? vertexShader->name : nullptr;
        selection.vertexShaderProgramHash = lastShaders && lastShaders->vertex == vertexShader
            ? lastShaders->vertexHash : HashVertexShaderProgram(vertexShader);
        selection.pixelShaderName = pixelShader ? pixelShader->name : nullptr;
        selection.pixelShaderProgramHash = lastShaders && lastShaders->pixel == pixelShader
            ? lastShaders->pixelHash : HashPixelShaderProgram(pixelShader);
        if (lastShaders)
            *lastShaders = {vertexShader, pixelShader,
                selection.vertexShaderProgramHash, selection.pixelShaderProgramHash};
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
    const WebRendererWorldLightTechniqueContext *lightContext,
    LastBrushShaders *lastShaders = nullptr) noexcept
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
    batch.baseImage = WebRenderer_FindBaseImage(surface.material, batch.samplerState);
    batch.normalImage = WebRenderer_FindNormalImage(
        surface.material, batch.normalSamplerState);
    batch.specularImage = WebRenderer_FindSpecularImage(
        surface.material, batch.specularSamplerState);
    batch.water = FindWater(surface.material, batch.waterSamplerState);
    if (surface.reflectionProbeIndex < world.reflectionProbeCount &&
        world.reflectionProbes)
    {
        batch.reflectionProbeIndex = surface.reflectionProbeIndex;
        batch.reflectionProbeImage = world.reflectionProbes[
            surface.reflectionProbeIndex].reflectionImage;
    }
    const std::uint32_t requestedTechniqueType = SelectLitTechniqueType(
        surface.primaryLightIndex, lightContext, sunShadowEnabled);
    const TechniqueSelection technique = SelectTechnique(
        surface.material, requestedTechniqueType, lastShaders);
    batch.techniqueName = technique.identityName
        ? technique.identityName : "<unsupported-technique>";
    batch.techniqueType = static_cast<std::uint8_t>(technique.type);
    batch.customSamplerFlags = technique.customSamplerFlags;
    batch.techniqueFlags = technique.techniqueFlags;
    batch.vertexShaderName = technique.vertexShaderName
        ? technique.vertexShaderName : "<unavailable-vertex-shader>";
    batch.vertexShaderProgramHash = technique.vertexShaderProgramHash;
    batch.pixelShaderName = technique.pixelShaderName
        ? technique.pixelShaderName : "<unavailable-pixel-shader>";
    batch.pixelShaderProgramHash = technique.pixelShaderProgramHash;
    if (batch.pixelShaderName &&
        std::strstr(batch.pixelShaderName, "d0") != nullptr &&
        WebRenderer_CopyMaterialConstant(surface.material, DETAIL_SCALE_HASH,
            batch.detailScale))
    {
        batch.detailImage = WebRenderer_FindDetailImage(
            surface.material, batch.detailSamplerState);
    }
    batch.stateBits[0] = technique.stateBits[0];
    batch.stateBits[1] = technique.stateBits[1];
    if (surface.material && surface.material->stateBitsTable)
    {
        const std::uint8_t entry = surface.material->stateBitsEntry[
            TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX];
        if (entry != 0xffu && entry < surface.material->stateBitsCount)
        {
            batch.castsSpotShadow = true;
            batch.shadowStateBits0 =
                surface.material->stateBitsTable[entry].loadBits[0];
        }
    }
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
    constexpr std::uint32_t FALLOFF_PARMS_HASH = 0xbdde5cf5u;
    constexpr std::uint32_t FALLOFF_BEGIN_COLOR_HASH = 0x3d05a1f2u;
    constexpr std::uint32_t FALLOFF_END_COLOR_HASH = 0x6b1da6fau;
    const bool canonicalSm3Specular = UsesSm3EnvironmentSpecular(
            technique, batch.specularImage) &&
        batch.reflectionProbeImage &&
        WebRenderer_CopyMaterialConstant(surface.material, ENV_MAP_PARMS_HASH,
            batch.envMapParms);
    const bool canonicalWater = ShaderNameIs(technique, "water_l_sun.hlsl") &&
        batch.water && batch.reflectionProbeImage &&
        WebRenderer_CopyMaterialConstant(surface.material, ENV_MAP_PARMS_HASH,
            batch.envMapParms) &&
        WebRenderer_CopyMaterialConstant(surface.material, WATER_COLOR_HASH,
            batch.waterColor);
    const bool canonicalDistanceFalloff = VertexShaderNameIs(
            technique, "vertcol_simple_fog_df.hlsl") &&
        WebRenderer_CopyMaterialConstant(surface.material, FALLOFF_PARMS_HASH,
            batch.falloffParms) &&
        WebRenderer_CopyMaterialConstant(surface.material, FALLOFF_BEGIN_COLOR_HASH,
            batch.falloffBeginColor) &&
        WebRenderer_CopyMaterialConstant(surface.material, FALLOFF_END_COLOR_HASH,
            batch.falloffEndColor);
    if (WebRenderer_IsCinematicMaterial(surface.material, technique.type))
        batch.technique = WebRendererWorldTechnique::Cinematic;
    else if (canonicalWater)
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
    else if (canonicalDistanceFalloff)
        batch.technique =
            WebRendererWorldTechnique::VertexColorDistanceFalloff;
    else if (ShaderNameIs(technique, "mul.hlsl"))
        batch.technique = WebRendererWorldTechnique::VertexColorMultiply;
    else if (ShaderNameIs(
            technique, "vertcol_simple_add_fog.hlsl"))
        batch.technique = WebRendererWorldTechnique::VertexColorAdditive;
    else if (batch.lightingMode ==
            WebRendererWorldLightingMode::SecondaryDirectional)
    {
        const bool normalMapped = UsesDirectionalNormalMap(
            technique, batch.normalImage);
        batch.technique = canonicalSm3Specular
            ? (normalMapped
                ? WebRendererWorldTechnique::BaseTextureLightmapNormalSpecular
                : WebRendererWorldTechnique::BaseTextureLightmapSpecular)
            : (normalMapped
                ? WebRendererWorldTechnique::BaseTextureLightmapNormal
                : WebRendererWorldTechnique::BaseTextureLightmap);
    }
    else
        batch.technique = WebRendererWorldTechnique::BaseTexture;
    batch.castsSunShadow = world.dpvs.surfaceCastsSunShadow &&
        (world.dpvs.surfaceCastsSunShadow[surfaceIndex >> 5u] &
            (1u << (surfaceIndex & 31u))) != 0u &&
        batch.castsSpotShadow;
    if (batch.technique == WebRendererWorldTechnique::Cinematic)
        batch.samplerState = batch.normalSamplerState = batch.detailSamplerState = batch.specularSamplerState = 0x62;
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
        batch.specularImage == candidate.specularImage &&
        batch.lightmapImage == candidate.lightmapImage &&
        batch.secondaryLightmapImage == candidate.secondaryLightmapImage &&
        batch.water == candidate.water &&
        batch.reflectionProbeImage == candidate.reflectionProbeImage &&
        batch.lightingMode == candidate.lightingMode &&
        batch.customSamplerFlags == candidate.customSamplerFlags &&
        batch.techniqueFlags == candidate.techniqueFlags &&
        batch.vertexShaderProgramHash ==
            candidate.vertexShaderProgramHash &&
        batch.pixelShaderProgramHash == candidate.pixelShaderProgramHash &&
        batch.stateBits[0] == candidate.stateBits[0] &&
        batch.stateBits[1] == candidate.stateBits[1] &&
        batch.samplerState == candidate.samplerState &&
        batch.normalSamplerState == candidate.normalSamplerState &&
        batch.specularSamplerState == candidate.specularSamplerState &&
        batch.waterSamplerState == candidate.waterSamplerState &&
        batch.reflectionProbeIndex == candidate.reflectionProbeIndex &&
        batch.lightmapIndex == candidate.lightmapIndex &&
        batch.primaryLightIndex == candidate.primaryLightIndex &&
        batch.technique == candidate.technique &&
        std::memcmp(batch.envMapParms, candidate.envMapParms,
            sizeof(batch.envMapParms)) == 0 &&
        std::memcmp(batch.waterColor, candidate.waterColor,
            sizeof(batch.waterColor)) == 0 &&
        std::memcmp(batch.falloffParms, candidate.falloffParms,
            sizeof(batch.falloffParms)) == 0 &&
        std::memcmp(batch.falloffBeginColor, candidate.falloffBeginColor,
            sizeof(batch.falloffBeginColor)) == 0 &&
        std::memcmp(batch.falloffEndColor, candidate.falloffEndColor,
            sizeof(batch.falloffEndColor)) == 0 &&
        batch.castsSunShadow == candidate.castsSunShadow &&
        batch.castsSpotShadow == candidate.castsSpotShadow;
}

} // namespace

bool WebRenderer_ValidatePrimaryLightFrame(
    const WebRendererPrimaryLightDesc *primaryLights,
    std::uint32_t primaryLightCount) noexcept
{
    return ValidatePrimaryLightFrame(primaryLights, primaryLightCount);
}

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
    if (world.outdoorImage)
    {
        for (const auto &row : world.outdoorLookupMatrix)
            for (const float component : row)
                if (!std::isfinite(component))
                    return WebRendererWorldSceneResult::InvalidWorld;
        replacement.outdoorImage = world.outdoorImage;
        std::memcpy(replacement.outdoorLookupMatrix,
            world.outdoorLookupMatrix,
            sizeof(replacement.outdoorLookupMatrix));
    }
    try
    {
        struct EmittedSurfaceRange
        {
            std::uint32_t firstIndex = UINT32_MAX;
            std::uint32_t indexCount = 0u;
            std::uint32_t batchIndex = UINT32_MAX;
        };
        std::vector<EmittedSurfaceRange> emittedSurfaces(
            static_cast<std::size_t>(world.surfaceCount));
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
                    !std::isfinite(source.texCoord[1]) ||
                    !std::isfinite(source.lmapCoord[0]) ||
                    !std::isfinite(source.lmapCoord[1]))
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
            emittedSurfaces[surfaceIndex] = {
                candidate.firstIndex,
                candidate.indexCount,
                static_cast<std::uint32_t>(replacement.batches.size() - 1u),
            };
            WebRendererWorldSurfaceRange range{surfaceIndex,
                static_cast<std::uint32_t>(replacement.batches.size() - 1u),
                candidate.firstIndex, candidate.indexCount};
            std::copy(std::begin(surface.bounds[0]),
                std::end(surface.bounds[0]), std::begin(range.mins));
            std::copy(std::begin(surface.bounds[1]),
                std::end(surface.bounds[1]), std::begin(range.maxs));
            replacement.surfaceRanges.push_back(range);
            if (replacement.surfaceCount == 0u)
                replacement.firstSurfaceIndex = surfaceIndex;
            replacement.lastSurfaceIndex = surfaceIndex;
            ++replacement.surfaceCount;
          }
        }

        // Native spot-shadow submission does not render every opaque BSP
        // surface. Each light owns an authored list of sorted surface indices
        // in GfxWorld::shadowGeom; preserve that exact membership at the
        // frontend/backend boundary while translating to uploaded ranges.
        if (world.shadowGeom)
        {
            for (std::uint32_t lightIndex = 0u;
                 lightIndex < world.primaryLightCount; ++lightIndex)
            {
                const GfxShadowGeometry &geometry =
                    world.shadowGeom[lightIndex];
                if (geometry.surfaceCount != 0u &&
                    !geometry.sortedSurfIndex)
                {
                    return WebRendererWorldSceneResult::InvalidSurfaceRange;
                }
                for (std::uint32_t casterIndex = 0u;
                     casterIndex < geometry.surfaceCount; ++casterIndex)
                {
                    const std::uint32_t surfaceIndex =
                        geometry.sortedSurfIndex[casterIndex];
                    if (surfaceIndex >= emittedSurfaces.size())
                        return WebRendererWorldSceneResult::InvalidSurfaceRange;
                    const EmittedSurfaceRange &emitted =
                        emittedSurfaces[surfaceIndex];
                    // Shadow geometry can include brush-model or otherwise
                    // non-world ranges submitted through another scene path.
                    if (emitted.firstIndex == UINT32_MAX) continue;
                    const Material *material =
                        world.dpvs.surfaces[surfaceIndex].material;
                    if (!material || !material->stateBitsTable)
                        return WebRendererWorldSceneResult::InvalidSurfaceRange;
                    const std::uint8_t shadowStateEntry =
                        material->stateBitsEntry[
                            TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX];
                    if (shadowStateEntry == 0xffu ||
                        shadowStateEntry >= material->stateBitsCount)
                    {
                        return WebRendererWorldSceneResult::InvalidSurfaceRange;
                    }
                    replacement.spotShadowCasters.push_back({
                        lightIndex,
                        emitted.firstIndex,
                        emitted.indexCount,
                        emitted.batchIndex,
                        material->stateBitsTable[
                            shadowStateEntry].loadBits[0],
                    });
                }
                if (geometry.smodelCount != 0u && !geometry.smodelIndex)
                    return WebRendererWorldSceneResult::InvalidSurfaceRange;
                for (std::uint32_t modelIndex = 0u;
                     modelIndex < geometry.smodelCount; ++modelIndex)
                {
                    const std::uint32_t canonicalInstanceIndex =
                        geometry.smodelIndex[modelIndex];
                    if (canonicalInstanceIndex >= world.dpvs.smodelCount)
                        return WebRendererWorldSceneResult::InvalidSurfaceRange;
                    replacement.spotShadowStaticModels.push_back({
                        lightIndex,
                        canonicalInstanceIndex,
                    });
                }
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

bool WebRenderer_ValidateWorldSurfaceRanges(
    const WebRendererWorldSurfaceDesc &surface) noexcept
{
    if (!surface.surfaceRanges || !surface.batches ||
        !surface.surfaceRangeCount || !surface.canonicalSurfaceCount ||
        surface.surfaceRangeCount > surface.canonicalSurfaceCount)
        return false;
    std::uint32_t nextIndex = 0u, nextSurface = 0u, batchIndex = 0u;
    for (std::uint32_t i = 0u; i < surface.surfaceRangeCount; ++i)
    {
        const auto &range = surface.surfaceRanges[i];
        if (range.canonicalSurfaceIndex < nextSurface ||
            range.canonicalSurfaceIndex >= surface.canonicalSurfaceCount ||
            range.batchIndex != batchIndex || batchIndex >= surface.batchCount ||
            range.firstIndex != nextIndex || !range.indexCount ||
            range.indexCount % 3u || nextIndex > surface.indexCount ||
            range.indexCount > surface.indexCount - nextIndex ||
            !Finite3(range.mins) || !Finite3(range.maxs) ||
            range.mins[0] > range.maxs[0] ||
            range.mins[1] > range.maxs[1] ||
            range.mins[2] > range.maxs[2])
            return false;
        const auto &batch = surface.batches[batchIndex];
        if (range.firstIndex < batch.firstIndex ||
            range.firstIndex - batch.firstIndex > batch.indexCount ||
            range.indexCount > batch.indexCount - (range.firstIndex - batch.firstIndex))
            return false;
        nextIndex += range.indexCount;
        nextSurface = range.canonicalSurfaceIndex + 1u;
        if (nextIndex - batch.firstIndex == batch.indexCount) ++batchIndex;
    }
    return nextIndex == surface.indexCount && batchIndex == surface.batchCount;
}

static bool BuildWorldLightRanges(
    const std::vector<WebRendererWorldSurfaceRange> &surfaces,
    const std::uint8_t *visibility, std::uint32_t visibilityCount,
    std::vector<WebRendererWorldCameraRange> &destination,
    const GfxLight *receiverLight, float spotNearPlaneOffset)
{
    destination.clear();
    float planes[6][4]{};
    if (receiverLight && !kisak::dynamic_lights::ReceiverPlanes(
            *receiverLight, spotNearPlaneOffset, planes)) return false;
    try
    {
        // Keep capacity across views; no geometry copy or GPU upload is needed.
        destination.reserve(surfaces.size());
        for (const auto &surface : surfaces)
        {
            if (visibility && surface.canonicalSurfaceIndex >= visibilityCount)
            {
                destination.clear();
                return false;
            }
            if (visibility && visibility[surface.canonicalSurfaceIndex] != 1u) continue;
            if (receiverLight && !(receiverLight->type == 2
                ? kisak::dynamic_lights::BoxInPlanes(planes, surface.mins, surface.maxs)
                : kisak::dynamic_lights::BoxInSphere(receiverLight->origin,
                    receiverLight->radius * receiverLight->radius, surface.mins, surface.maxs)))
                continue;
            if (!destination.empty() &&
                destination.back().batchIndex == surface.batchIndex &&
                destination.back().firstIndex + destination.back().indexCount == surface.firstIndex)
            {
                destination.back().indexCount += surface.indexCount;
                ++destination.back().surfaceCount;
            }
            else
                destination.push_back({surface.batchIndex, surface.firstIndex,
                    surface.indexCount, 1u});
        }
    }
    catch (const std::bad_alloc &)
    {
        destination.clear();
        return false;
    }
    return true;
}

WebRendererWorldSceneResult WebRenderer_BuildBrushModelSceneCommand(
    const GfxWorld &world,
    const WebRendererBrushModelSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererBrushModelSceneCommand &destination,
    const WebRendererWorldLightTechniqueContext *lightContext)
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
    LastBrushShaders lastShaders;
#if KISAK_WEB_DIAGNOSTICS
    WebFrameProfileSample *const profile = WebFrameProfile_Current();
    double stageStarted = profile ? WebFrameProfile_Now() : 0.0;
#endif
    try
    {
        constexpr std::uint32_t UNMAPPED =
            std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> vertexRemap(world.vertexCount, UNMAPPED);
        std::vector<std::uint32_t> touchedVertices;
#if KISAK_WEB_DIAGNOSTICS
        if (profile)
            profile->sceneBrushRemapMs += WebFrameProfile_Now() - stageStarted;
#endif
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
#if KISAK_WEB_DIAGNOSTICS
                if (profile) stageStarted = WebFrameProfile_Now();
#endif
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

#if KISAK_WEB_DIAGNOSTICS
                if (profile)
                {
                    const double now = WebFrameProfile_Now();
                    profile->sceneBrushGeometryMs += now - stageStarted;
                    stageStarted = now;
                }
#endif
                WebRendererWorldBatchDesc candidate = MakeBatch(
                    world, surface, surfaceIndex, firstDestinationIndex,
                    false, lightContext, &lastShaders);
                candidate.indexCount = indexCount;
                candidate.sourceKind =
                    WebRendererSceneBatchKind::DynamicBModel;
                candidate.dynamicLightSurfType = 6u;
                candidate.modelName = "<brush-model>";
                // Native R_AddBModelSurfaces admits a moving brush to the
                // sun pass by its build-shadowmap technique. The world-only
                // surfaceCastsSunShadow bitset does not own submodel indices.
                const MaterialTechniqueSet *shadowSet =
                    surface.material ? surface.material->techniqueSet : nullptr;
                if (shadowSet && shadowSet->remappedTechniqueSet)
                    shadowSet = shadowSet->remappedTechniqueSet;
                candidate.castsSunShadow = shadowSet &&
                    shadowSet->techniques[
                        TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX];
                candidate.castsSpotShadow = candidate.castsSunShadow;
                if (candidate.castsSunShadow && surface.material &&
                    surface.material->stateBitsTable)
                {
                    const std::uint8_t shadowStateEntry =
                        surface.material->stateBitsEntry[
                            TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX];
                    if (shadowStateEntry != 0xffu &&
                        shadowStateEntry < surface.material->stateBitsCount)
                    {
                        candidate.shadowStateBits0 =
                            surface.material->stateBitsTable[
                                shadowStateEntry].loadBits[0];
                    }
                }
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
#if KISAK_WEB_DIAGNOSTICS
                if (profile)
                    profile->sceneBrushMaterialMs += WebFrameProfile_Now() - stageStarted;
#endif
            }
#if KISAK_WEB_DIAGNOSTICS
            if (profile) stageStarted = WebFrameProfile_Now();
#endif
            for (const std::uint32_t sourceVertexIndex : touchedVertices)
                vertexRemap[sourceVertexIndex] = UNMAPPED;
            touchedVertices.clear();
#if KISAK_WEB_DIAGNOSTICS
            if (profile)
                profile->sceneBrushRemapMs += WebFrameProfile_Now() - stageStarted;
#endif
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

bool WebRenderer_CopyBrushReceiverBounds(const GfxBrushModel &model,
    WebRendererBrushModelInstanceDesc &instance) noexcept
{
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(model.writable.mins[axis]) || !std::isfinite(model.writable.maxs[axis]) ||
            model.writable.mins[axis] > model.writable.maxs[axis]) return false;
    std::copy_n(model.writable.mins, 3, instance.receiverMins);
    std::copy_n(model.writable.maxs, 3, instance.receiverMaxs);
    return true;
}

bool WebRenderer_BuildWorldCameraRanges(
    const std::vector<WebRendererWorldSurfaceRange> &surfaces,
    const std::uint8_t *visibility, std::uint32_t visibilityCount,
    bool visibilityComputed,
    std::vector<WebRendererWorldCameraRange> &destination,
    const GfxLight *receiverLight, float spotNearPlaneOffset)
{
    destination.clear();
    if (!visibilityComputed || (!surfaces.empty() && !visibility)) return false;
    return BuildWorldLightRanges(surfaces, visibility, visibilityCount,
        destination, receiverLight, spotNearPlaneOffset);
}

bool WebRenderer_BuildWorldTransientSpotShadowRanges(
    const std::vector<WebRendererWorldSurfaceRange> &surfaces,
    const GfxLight &light, float spotNearPlaneOffset,
    std::vector<WebRendererWorldShadowRange> &destination)
{
    destination.clear();
    if (light.type != 2u) return false;
    return BuildWorldLightRanges(surfaces, nullptr, 0u, destination,
        &light, spotNearPlaneOffset);
}

bool WebRenderer_BrushReceivesLight(const WebRendererBrushModelInstanceDesc &instance,
    const GfxLight &light, const float spotPlanes[6][4]) noexcept
{
    return light.type == 2
        ? kisak::dynamic_lights::BoxInPlanes(spotPlanes, instance.receiverMins, instance.receiverMaxs)
        : kisak::dynamic_lights::BoxInSphere(light.origin, light.radius * light.radius,
            instance.receiverMins, instance.receiverMaxs);
}

bool WebRenderer_BrushPlacementIsFinite(
    const WebRendererBrushModelInstanceDesc &instance,
    const std::vector<WebRendererSurfaceVertex> &vertices,
    float maximumCoordinate) noexcept
{
    if (!Finite3(instance.origin)) return false;
    if (!Finite3(instance.receiverMins) || !Finite3(instance.receiverMaxs)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (instance.receiverMins[axis] > instance.receiverMaxs[axis]) return false;
    for (const auto &axis : instance.axis)
        if (!Finite3(axis)) return false;
    bool checkVertices = false;
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        const double bound = std::fabs(static_cast<double>(instance.origin[component])) +
            static_cast<double>(maximumCoordinate) *
                (std::fabs(static_cast<double>(instance.axis[0][component])) +
                 std::fabs(static_cast<double>(instance.axis[1][component])) +
                 std::fabs(static_cast<double>(instance.axis[2][component])));
        checkVertices |= bound > static_cast<double>(std::numeric_limits<float>::max()) * 0.5;
    }
    if (!checkVertices) return true;
    for (const auto &vertex : vertices)
    {
        for (std::size_t component = 0u; component < 3u; ++component)
        {
            const float position = vertex.position[0] * instance.axis[0][component] +
                vertex.position[1] * instance.axis[1][component] +
                vertex.position[2] * instance.axis[2][component] + instance.origin[component];
            const float shaderPosition = instance.origin[component] +
                vertex.position[0] * instance.axis[0][component] +
                vertex.position[1] * instance.axis[1][component] +
                vertex.position[2] * instance.axis[2][component];
            const float normal = vertex.normal[0] * instance.axis[0][component] +
                vertex.normal[1] * instance.axis[1][component] +
                vertex.normal[2] * instance.axis[2][component];
            const float tangent = vertex.tangent[0] * instance.axis[0][component] +
                vertex.tangent[1] * instance.axis[1][component] +
                vertex.tangent[2] * instance.axis[2][component];
            if (!std::isfinite(position) || !std::isfinite(shaderPosition) ||
                !std::isfinite(normal) || !std::isfinite(tangent))
                return false;
        }
    }
    return true;
}

bool WebRenderer_BuildWorldShadowRanges(
    const std::vector<WebRendererWorldSurfaceRange> &surfaces,
    const std::array<float, 16> &shadowMatrix,
    std::vector<WebRendererWorldShadowRange> &destination)
{
    destination.clear();
    for (const float value : shadowMatrix)
        if (!std::isfinite(value)) return false;
    try
    {
        destination.reserve(surfaces.size());
        for (const auto &surface : surfaces)
        {
            float center[3], extent[3];
            for (std::size_t component = 0u; component < 3u; ++component)
            {
                if (!std::isfinite(surface.mins[component]) ||
                    !std::isfinite(surface.maxs[component]) ||
                    surface.mins[component] > surface.maxs[component])
                {
                    destination.clear();
                    return false;
                }
                center[component] =
                    (surface.mins[component] + surface.maxs[component]) * 0.5f;
                extent[component] =
                    (surface.maxs[component] - surface.mins[component]) * 0.5f;
            }
            float clipCenter[4], clipExtent[3];
            for (std::size_t row = 0u; row < 4u; ++row)
            {
                clipCenter[row] = shadowMatrix[row] * center[0] +
                    shadowMatrix[4u + row] * center[1] +
                    shadowMatrix[8u + row] * center[2] +
                    shadowMatrix[12u + row];
                if (row < 3u)
                    clipExtent[row] = std::fabs(shadowMatrix[row]) * extent[0] +
                        std::fabs(shadowMatrix[4u + row]) * extent[1] +
                        std::fabs(shadowMatrix[8u + row]) * extent[2];
            }
            const float w = clipCenter[3];
            const bool visible =
                clipCenter[0] + clipExtent[0] >= -w &&
                clipCenter[0] - clipExtent[0] <= w &&
                clipCenter[1] + clipExtent[1] >= -w &&
                clipCenter[1] - clipExtent[1] <= w &&
                clipCenter[2] + clipExtent[2] >= -w &&
                clipCenter[2] - clipExtent[2] <= w;
            if (!visible) continue;
            if (!destination.empty() &&
                destination.back().batchIndex == surface.batchIndex &&
                destination.back().firstIndex + destination.back().indexCount ==
                    surface.firstIndex)
            {
                destination.back().indexCount += surface.indexCount;
                ++destination.back().surfaceCount;
            }
            else
                destination.push_back({surface.batchIndex, surface.firstIndex,
                    surface.indexCount, 1u});
        }
    }
    catch (const std::bad_alloc &)
    {
        destination.clear();
        return false;
    }
    return true;
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
