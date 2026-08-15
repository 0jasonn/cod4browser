#include <web/web_engine_xmodel_draw_list.h>

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void Require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void PutU16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void PutU32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t index = 0u; index < 4u; ++index)
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8u));
}

void PutF32(std::vector<std::uint8_t> &bytes, std::size_t offset, float value)
{
    PutU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

kisak::fastfile::RetailXSurface MakeSurface(
    std::uint32_t surfaceIndex, float xOffset)
{
    kisak::fastfile::RetailXSurface surface;
    surface.index = surfaceIndex;
    surface.vertCount = 3u;
    surface.triCount = 1u;
    surface.retainedPackedVertices.resize(3u * 32u);
    surface.retainedPackedIndices.resize(3u * 2u);
    const float positions[3][3] = {
        {-1.0f + xOffset, -1.0f, 0.0f},
        { 1.0f + xOffset, -1.0f, 0.0f},
        { 0.0f + xOffset,  1.0f, 0.0f},
    };
    for (std::size_t vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        const std::size_t offset = vertexIndex * 32u;
        PutF32(surface.retainedPackedVertices, offset, positions[vertexIndex][0]);
        PutF32(surface.retainedPackedVertices, offset + 4u, positions[vertexIndex][1]);
        PutF32(surface.retainedPackedVertices, offset + 8u, positions[vertexIndex][2]);
        PutF32(surface.retainedPackedVertices, offset + 12u, 1.0f);
        PutU32(surface.retainedPackedVertices, offset + 16u, 0xffffffffu);
        PutU32(surface.retainedPackedVertices, offset + 20u, 0u);
    }
    PutU16(surface.retainedPackedIndices, 0u, 0u);
    PutU16(surface.retainedPackedIndices, 2u, 1u);
    PutU16(surface.retainedPackedIndices, 4u, 2u);
    surface.renderPayloadRetained = true;
    surface.dependenciesTraversed = true;
    return surface;
}

kisak::fastfile::RetailXModelMaterial MakeMaterial(
    std::uint32_t identity, std::uint32_t imageIdentity,
    const char *materialName, const char *imageName)
{
    kisak::fastfile::RetailXModelMaterial material;
    material.name = materialName;
    material.identity = identity;
    material.published = true;
    material.textures.push_back({
        0u, 0u, 0u, 0u, WEB_ENGINE_TEXTURE_SEMANTIC_COLOR_MAP,
        0u, imageIdentity, true,
    });
    kisak::fastfile::RetailXModelImage image;
    image.name = imageName;
    image.mapType = 3u;
    image.width = 4u;
    image.height = 4u;
    image.depth = 1u;
    image.format = 0x31545844u;
    image.identity = imageIdentity;
    image.loadDefTraversed = true;
    image.published = true;
    material.images.push_back(std::move(image));
    return material;
}

kisak::fastfile::RetailWorldXModel MakeModel()
{
    kisak::fastfile::RetailWorldXModel model;
    model.name = "test/model";
    model.lodCount = 1;
    model.lods[0].surfaceIndex = 0u;
    model.lods[0].surfaceCount = 2u;
    model.mins = {-2.0f, -1.0f, -0.5f};
    model.maxs = { 2.0f,  1.0f,  0.5f};
    model.surfaces.push_back(MakeSurface(0u, -1.0f));
    model.surfaces.push_back(MakeSurface(1u, 1.0f));
    model.materialIdentities = {10u, 20u};
    model.materials.push_back(MakeMaterial(10u, 100u, "test/material_a", "test/image_a"));
    model.materials.push_back(MakeMaterial(20u, 200u, "test/material_b", "test/image_b"));
    model.surfaceDependenciesTraversed = true;
    model.materialsTraversed = true;
    model.published = true;
    return model;
}

void TestTwoDrawsAndTextures()
{
    const auto model = MakeModel();
    WebEngineXModelDrawList list;
    Require(WebEngine_BuildXModelDrawList(model, list) ==
        WebEngineXModelDrawListResult::Success, "two-surface LOD builds");
    Require(list.renderer.draws.size() == 2u && list.textures.size() == 2u,
        "two typed material images produce two renderer slots");
    Require(list.renderer.vertices.size() == 6u && list.renderer.indices.size() == 6u,
        "geometry is combined within aggregate limits");
    Require(list.renderer.draws[0].draw.firstIndex == 0u &&
        list.renderer.draws[1].draw.firstIndex == 3u &&
        list.renderer.draws[1].textureSlot == 1u,
        "draw ranges and texture slots retain surface order");
    Require(list.renderer.indices[3] == 3u && list.renderer.indices[5] == 5u,
        "later local indices are rebased into shared geometry");
    Require(list.horizontalAxis == 0u && list.verticalAxis == 1u,
        "every draw uses the same model-wide projection axes");
}

void TestLaterFailurePreservesEarlierDraw()
{
    auto model = MakeModel();
    model.materials[1].images[0].name = ",$identitynormalmap";
    model.materials[1].images[0].mapType = 0u;
    WebEngineXModelDrawList list;
    Require(WebEngine_BuildXModelDrawList(model, list) ==
        WebEngineXModelDrawListResult::Success, "partial draw list still publishes");
    Require(list.renderer.draws.size() == 1u && list.textures.size() == 1u &&
        list.surfaces.size() == 2u && list.surfaces[0].retained &&
        !list.surfaces[1].retained &&
        list.surfaces[1].materialResult ==
            WebEngineXModelMaterialResult::BuiltinUnsupported,
        "unsupported later material cannot discard the earlier draw");
}

void TestFailureIsAtomic()
{
    auto model = MakeModel();
    model.lods[0].surfaceCount = WEB_RENDERER_MAX_DRAW_LIST_DRAWS + 1u;
    WebEngineXModelDrawList destination;
    destination.firstLodSurfaceCount = 99u;
    Require(WebEngine_BuildXModelDrawList(model, destination) ==
        WebEngineXModelDrawListResult::OutputTooLarge,
        "oversized draw count is rejected");
    Require(destination.firstLodSurfaceCount == 99u,
        "failed replacement leaves the destination unchanged");
}
} // namespace

int main()
{
    try
    {
        TestTwoDrawsAndTextures();
        TestLaterFailurePreservesEarlierDraw();
        TestFailureIsAtomic();
        std::cout << "web_engine_xmodel_draw_list_tests: ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "web_engine_xmodel_draw_list_tests: " << error.what() << '\n';
        return 1;
    }
}
