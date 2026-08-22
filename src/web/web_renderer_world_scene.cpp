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
constexpr std::uint32_t TECHNIQUE_NONE_INDEX = 36u;

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
    // opaque world-surface pass. The WebGL sky pass is not part of this first
    // cgame frame slice, so do not let its enclosing geometry hide the world.
    return surface.material && (surface.material->info.gameFlags & 8u) != 0u;
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

bool TechniqueUsesPrimaryLightmap(
    const MaterialTechnique *technique) noexcept;

struct TechniqueSelection
{
    std::uint32_t type = TECHNIQUE_NONE_INDEX;
    const MaterialTechnique *technique = nullptr;
    const char *identityName = nullptr;
    bool usesPrimaryLightmap = false;
    std::uint32_t stateBits[2]{};
};

bool IsCanonicalWorldColorLitAlias(const MaterialTechniqueSet *techniqueSet)
    noexcept
{
    return techniqueSet && techniqueSet->name &&
        std::strncmp(techniqueSet->name, ",wc_l_", 6u) == 0;
}

TechniqueSelection SelectTechnique(const Material *material) noexcept
{
    TechniqueSelection selection;
    if (!material || !material->techniqueSet || !material->stateBitsTable)
        return selection;
    const MaterialTechniqueSet *techniqueSet =
        material->techniqueSet->remappedTechniqueSet
            ? material->techniqueSet->remappedTechniqueSet
            : material->techniqueSet;
    for (const std::uint32_t type : {
        TECHNIQUE_LIT_INDEX, TECHNIQUE_UNLIT_INDEX,
        TECHNIQUE_EMISSIVE_INDEX})
    {
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
        selection.usesPrimaryLightmap =
            type == TECHNIQUE_LIT_INDEX &&
            TechniqueUsesPrimaryLightmap(technique);
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
    if (IsCanonicalWorldColorLitAlias(material->techniqueSet) &&
        litEntry != 0xffu && litEntry < material->stateBitsCount)
    {
        selection.type = TECHNIQUE_LIT_INDEX;
        selection.identityName = material->techniqueSet->name;
        selection.usesPrimaryLightmap = true;
        selection.stateBits[0] =
            material->stateBitsTable[litEntry].loadBits[0];
        selection.stateBits[1] =
            material->stateBitsTable[litEntry].loadBits[1];
    }
    return selection;
}

bool TechniqueUsesPrimaryLightmap(
    const MaterialTechnique *technique) noexcept
{
    if (!technique) return false;
    for (std::uint32_t pass = 0u; pass < technique->passCount; ++pass)
    {
        if ((technique->passArray[pass].customSamplerFlags & 2u) != 0u)
            return true;
    }
    // Native RB_UploadMaterialTechnique uses the canonical lm_ technique-name
    // contract to select the world vertex declaration. Some retail technique
    // sets do not serialize the otherwise redundant custom-sampler bit, so
    // preserve that same contract for the minimum browser lightmap path.
    return technique->name && std::strncmp(technique->name, "lm_", 3u) == 0;
}

WebRendererWorldBatchDesc MakeBatch(
    const GfxWorld &world,
    const GfxSurface &surface,
    std::uint32_t surfaceIndex,
    std::uint32_t firstIndex) noexcept
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
    batch.modelIdentity = nullptr;
    batch.modelName = "<world>";
    batch.firstInstanceIndex = UINT32_MAX;
    batch.lastInstanceIndex = UINT32_MAX;
    batch.sourceKind = WebRendererSceneBatchKind::WorldSurface;
    batch.samplerState = 0u;
    batch.baseImage = FindBaseImage(surface.material, batch.samplerState);
    const TechniqueSelection technique = SelectTechnique(surface.material);
    batch.techniqueName = technique.identityName
        ? technique.identityName : "<unsupported-technique>";
    batch.techniqueType = static_cast<std::uint8_t>(technique.type);
    batch.stateBits[0] = technique.stateBits[0];
    batch.stateBits[1] = technique.stateBits[1];
    const bool supportsPrimaryLightmap = technique.identityName &&
        technique.type == TECHNIQUE_LIT_INDEX &&
        technique.usesPrimaryLightmap &&
        surface.lightmapIndex != 31u &&
        surface.lightmapIndex < world.lightmapCount && world.lightmaps;
    if (supportsPrimaryLightmap)
    {
        batch.lightmapImage = world.lightmaps[surface.lightmapIndex].primary;
        batch.secondaryLightmapImage =
            world.lightmaps[surface.lightmapIndex].secondary;
    }
    if (!technique.identityName || !batch.baseImage)
        batch.technique = WebRendererWorldTechnique::BackendFallback;
    else if (batch.lightmapImage)
        batch.technique = WebRendererWorldTechnique::BaseTextureLightmap;
    else
        batch.technique = WebRendererWorldTechnique::BaseTexture;
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
        batch.lightmapImage == candidate.lightmapImage &&
        batch.secondaryLightmapImage == candidate.secondaryLightmapImage &&
        batch.stateBits[0] == candidate.stateBits[0] &&
        batch.stateBits[1] == candidate.stateBits[1] &&
        batch.samplerState == candidate.samplerState &&
        batch.lightmapIndex == candidate.lightmapIndex &&
        batch.technique == candidate.technique;
}

} // namespace

WebRendererWorldSceneResult WebRenderer_BuildWorldSceneCommand(
    const GfxWorld &world,
    const WebRendererSceneViewDesc &view,
    WebRendererWorldSceneCommand &destination)
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
                destinationIndex =
                    static_cast<std::uint32_t>(replacement.vertices.size());
                replacement.vertices.push_back(vertex);
                replacement.indices.push_back(destinationIndex);
            }
            WebRendererWorldBatchDesc candidate = MakeBatch(
                world,
                surface,
                surfaceIndex,
                static_cast<std::uint32_t>(replacement.indices.size() - indexCount));
            candidate.indexCount = indexCount;
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
