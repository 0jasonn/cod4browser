#include <web/web_engine_xmodel_draw_list.h>

#include <web/web_engine_xmodel_surface.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace
{
std::uint32_t FindOrAppendTexture(
    const WebEngineXModelMaterialImageBinding &binding,
    std::vector<WebEngineXModelMaterialImageBinding> &textures)
{
    for (std::uint32_t index = 0u; index < textures.size(); ++index)
    {
        if (textures[index].imageIdentity == binding.imageIdentity &&
            textures[index].samplerState == binding.samplerState)
            return index;
    }
    if (textures.size() >= WEB_RENDERER_MAX_DRAW_LIST_TEXTURES)
        return UINT32_MAX;
    textures.push_back(binding);
    return static_cast<std::uint32_t>(textures.size() - 1u);
}

bool SelectProjectionAxes(
    const std::array<float, 3> &mins,
    const std::array<float, 3> &maxs,
    std::uint8_t &horizontal,
    std::uint8_t &vertical) noexcept
{
    std::array<std::uint8_t, 3> axes{0u, 1u, 2u};
    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        if (!std::isfinite(mins[axis]) || !std::isfinite(maxs[axis]) ||
            mins[axis] > maxs[axis]) return false;
    }
    std::sort(axes.begin(), axes.end(), [&](std::uint8_t left, std::uint8_t right) {
        return maxs[left] - mins[left] > maxs[right] - mins[right];
    });
    if (maxs[axes[0]] <= mins[axes[0]] || maxs[axes[1]] <= mins[axes[1]])
        return false;
    horizontal = axes[0];
    vertical = axes[1];
    return true;
}

} // namespace

WebEngineXModelDrawListResult WebEngine_BuildXModelDrawList(
    const kisak::fastfile::RetailWorldXModel &model,
    WebEngineXModelDrawList &destination)
{
    using namespace kisak::fastfile;
    if (!model.published || !model.surfaceDependenciesTraversed ||
        !model.materialsTraversed || model.lodCount <= 0)
        return WebEngineXModelDrawListResult::InvalidModel;

    const RetailXModelLod &lod = model.lods[0];
    const std::uint32_t first = lod.surfaceIndex;
    const std::uint32_t count = lod.surfaceCount;
    if (count == 0u || count > WEB_RENDERER_MAX_DRAW_LIST_DRAWS ||
        first > model.surfaces.size() || count > model.surfaces.size() - first ||
        first > model.materialIdentities.size() ||
        count > model.materialIdentities.size() - first)
        return count > WEB_RENDERER_MAX_DRAW_LIST_DRAWS
            ? WebEngineXModelDrawListResult::OutputTooLarge
            : WebEngineXModelDrawListResult::InvalidModel;

    WebEngineXModelDrawList replacement;
    replacement.mins.fill(std::numeric_limits<float>::infinity());
    replacement.maxs.fill(-std::numeric_limits<float>::infinity());
    replacement.firstLodSurfaceIndex = first;
    replacement.firstLodSurfaceCount = count;
    bool haveBounds = false;
    for (std::uint32_t offset = 0u; offset < count; ++offset)
    {
        const std::uint32_t sourceIndex = first + offset;
        const RetailXSurface &surface = model.surfaces[sourceIndex];
        WebEngineXModelMaterialImageBinding binding;
        if (!surface.renderPayloadRetained ||
            WebEngine_SelectXModelColorMap(
                model, model.materialIdentities[sourceIndex], binding) !=
                WebEngineXModelMaterialResult::Success)
            continue;
        const WebEnginePackedXSurfaceView view{
            surface.retainedPackedVertices.data(),
            surface.retainedPackedVertices.size(),
            surface.vertCount,
            surface.retainedPackedIndices.data(),
            surface.retainedPackedIndices.size(),
            surface.triCount,
            model.materialIdentities[sourceIndex],
        };
        WebEngineConvertedXModelSurface converted;
        if (WebEngine_ConvertPackedXModelSurface(view, converted) !=
            WebEngineXModelSurfaceResult::Success) continue;
        for (std::size_t axis = 0u; axis < 3u; ++axis)
        {
            replacement.mins[axis] = std::min(
                replacement.mins[axis], converted.mins[axis]);
            replacement.maxs[axis] = std::max(
                replacement.maxs[axis], converted.maxs[axis]);
        }
        haveBounds = true;
    }
    if (!haveBounds || !SelectProjectionAxes(
            replacement.mins, replacement.maxs,
            replacement.horizontalAxis, replacement.verticalAxis))
        return WebEngineXModelDrawListResult::InvalidModel;

    const WebEngineXModelProjectionBounds projection{
        replacement.mins, replacement.maxs,
    };
    try
    {
        replacement.surfaces.reserve(count);
        replacement.renderer.draws.reserve(count);
        replacement.textures.reserve(
            std::min<std::uint32_t>(count, WEB_RENDERER_MAX_DRAW_LIST_TEXTURES));

        for (std::uint32_t offset = 0u; offset < count; ++offset)
        {
            const std::uint32_t sourceIndex = first + offset;
            const RetailXSurface &surface = model.surfaces[sourceIndex];
            WebEngineXModelDrawPublication publication;
            publication.surfaceIndex = surface.index;
            publication.materialIdentity = model.materialIdentities[sourceIndex];
            publication.vertexCount = surface.vertCount;
            publication.triangleCount = surface.triCount;

            WebEngineXModelMaterialImageBinding binding;
            publication.materialResult = WebEngine_SelectXModelColorMap(
                model, publication.materialIdentity, binding);
            if (publication.materialResult != WebEngineXModelMaterialResult::Success ||
                !surface.renderPayloadRetained ||
                surface.retainedPackedVertices.empty() ||
                surface.retainedPackedIndices.empty())
            {
                replacement.surfaces.push_back(publication);
                continue;
            }

            if (replacement.renderer.vertices.size() + surface.vertCount >
                    WEB_RENDERER_MAX_DRAW_LIST_VERTICES ||
                replacement.renderer.indices.size() +
                    static_cast<std::size_t>(surface.triCount) * 3u >
                    WEB_RENDERER_MAX_DRAW_LIST_INDICES)
            {
                replacement.surfaces.push_back(publication);
                continue;
            }

            WebEngineConvertedXModelSurface converted;
            const WebEnginePackedXSurfaceView view{
                surface.retainedPackedVertices.data(),
                surface.retainedPackedVertices.size(),
                surface.vertCount,
                surface.retainedPackedIndices.data(),
                surface.retainedPackedIndices.size(),
                surface.triCount,
                publication.materialIdentity,
            };
            if (WebEngine_ConvertPackedXModelSurfaceWithBounds(
                    view, projection, converted) !=
                WebEngineXModelSurfaceResult::Success)
            {
                replacement.surfaces.push_back(publication);
                continue;
            }

            const std::uint32_t textureSlot =
                FindOrAppendTexture(binding, replacement.textures);
            if (textureSlot == UINT32_MAX)
            {
                replacement.surfaces.push_back(publication);
                continue;
            }

            const std::uint32_t baseVertex =
                static_cast<std::uint32_t>(replacement.renderer.vertices.size());
            const std::uint32_t firstIndex =
                static_cast<std::uint32_t>(replacement.renderer.indices.size());
            replacement.renderer.vertices.insert(
                replacement.renderer.vertices.end(),
                converted.rendererSurface.vertices.begin(),
                converted.rendererSurface.vertices.end());
            for (const std::uint16_t localIndex : converted.rendererSurface.indices)
            {
                const std::uint32_t combined = baseVertex + localIndex;
                if (combined > std::numeric_limits<std::uint16_t>::max())
                    return WebEngineXModelDrawListResult::OutputTooLarge;
                replacement.renderer.indices.push_back(
                    static_cast<std::uint16_t>(combined));
            }
            replacement.renderer.draws.push_back({
                {
                    WebRendererPrimitiveTopology::TriangleList,
                    firstIndex,
                    static_cast<std::uint32_t>(converted.rendererSurface.indices.size()),
                    WebRendererTextureBinding::EngineImage,
                },
                textureSlot,
            });
            publication.textureSlot = textureSlot;
            publication.retained = true;
            replacement.surfaces.push_back(publication);
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebEngineXModelDrawListResult::AllocationFailed;
    }

    if (replacement.renderer.draws.empty())
        return WebEngineXModelDrawListResult::NoSupportedDraw;
    replacement.renderer.textureCount =
        static_cast<std::uint32_t>(replacement.textures.size());
    destination = std::move(replacement);
    return WebEngineXModelDrawListResult::Success;
}

const char *WebEngine_XModelDrawListResultString(
    WebEngineXModelDrawListResult result) noexcept
{
    switch (result)
    {
    case WebEngineXModelDrawListResult::Success: return "success";
    case WebEngineXModelDrawListResult::InvalidModel:
        return "the first XModel LOD is not a complete bounded dependency";
    case WebEngineXModelDrawListResult::NoSupportedDraw:
        return "the first XModel LOD contains no supported textured draw";
    case WebEngineXModelDrawListResult::OutputTooLarge:
        return "the first XModel LOD exceeds draw-list limits";
    case WebEngineXModelDrawListResult::AllocationFailed:
        return "the XModel draw list could not be allocated";
    }
    return "unknown XModel draw-list error";
}
