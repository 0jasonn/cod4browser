#include <web/web_engine_world_surface.h>

#include <gfx_d3d/gfx_world_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFailure final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

void RequireResult(
    WebEngineWorldSurfaceResult actual,
    WebEngineWorldSurfaceResult expected,
    std::string_view context)
{
    if (actual == expected)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += WebEngine_WorldSurfaceResultString(expected);
    message += ", got ";
    message += WebEngine_WorldSurfaceResultString(actual);
    throw TestFailure(message);
}

void RequireNear(float actual, float expected, std::string_view context)
{
    if (std::fabs(actual - expected) <= 0.000001f)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += std::to_string(expected);
    message += ", got ";
    message += std::to_string(actual);
    throw TestFailure(message);
}

WebEngineWorldVertex MakeVertex(
    float x = 0.0f,
    float y = 0.0f,
    float z = 0.0f,
    std::uint32_t color = 0xffffffffu,
    float textureU = 0.0f,
    float textureV = 0.0f)
{
    return {
        {x, y, z},
        1.0f,
        color,
        {textureU, textureV},
        {0.25f, 0.75f},
        0x10203040u,
        0x50607080u,
    };
}

GfxWorldVertex MakeGfxVertex(
    float x, float y, float z, std::uint32_t color = 0xffffffffu)
{
    GfxWorldVertex vertex{};
    vertex.xyz[0] = x;
    vertex.xyz[1] = y;
    vertex.xyz[2] = z;
    vertex.binormalSign = 1.0f;
    vertex.color.packed = color;
    vertex.texCoord[0] = x;
    vertex.texCoord[1] = y;
    return vertex;
}

WebEngineWorldProjection2D IdentityProjection()
{
    return {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
    };
}

template <std::size_t VertexCount, std::size_t IndexCount>
WebEngineWorldSurfaceView MakeView(
    const std::array<WebEngineWorldVertex, VertexCount> &vertices,
    const std::array<std::uint16_t, IndexCount> &indices,
    WebEngineWorldSurfaceRange range)
{
    return {
        {
            vertices.data(),
            static_cast<std::uint32_t>(vertices.size()),
            indices.data(),
            static_cast<std::uint32_t>(indices.size()),
        },
        range,
        WebEngineWorldVertexFormat::Base,
    };
}

struct GoldenFixture
{
    std::array<WebEngineWorldVertex, 7> vertices{};
    std::array<std::uint16_t, 12> indices{};

    GoldenFixture()
    {
        const float poison = std::numeric_limits<float>::quiet_NaN();
        vertices.fill(MakeVertex(poison, poison, poison));
        vertices[2] = MakeVertex(1.0f, 2.0f, 3.0f, 0x80112233u, -2.5f, 3.75f);
        vertices[3] = MakeVertex(-1.0f, 0.5f, 2.0f, 0xff00ff80u, 4.0f, -1.0f);
        vertices[4] = MakeVertex(0.0f, -2.0f, -1.0f, 0x00010203u, 0.0f, 8.0f);
        vertices[5] = MakeVertex(4.0f, 1.0f, 0.25f, 0x7fffff00u, 1.5f, 2.5f);

        indices.fill(std::numeric_limits<std::uint16_t>::max());
        const std::array<std::uint16_t, 6> local = {0u, 1u, 2u, 2u, 3u, 0u};
        std::copy(local.begin(), local.end(), indices.begin() + 3);
    }

    WebEngineWorldSurfaceView View() const
    {
        return MakeView(
            vertices,
            indices,
            {0, 2, 4u, 2u, 3});
    }
};

WebEngineWorldProjection2D GoldenProjection()
{
    return {
        {2.0f, -1.0f, 0.5f, 0.25f},
        {-1.0f, 0.5f, 2.0f, -0.75f},
    };
}

bool SameDraw(const WebRendererDrawDesc &left, const WebRendererDrawDesc &right)
{
    return left.topology == right.topology &&
        left.firstIndex == right.firstIndex &&
        left.indexCount == right.indexCount &&
        left.textureBinding == right.textureBinding;
}

bool SameConverted(
    const WebEngineConvertedWorldSurface &left,
    const WebEngineConvertedWorldSurface &right)
{
    const bool verticesEqual = left.vertices.size() == right.vertices.size() &&
        (left.vertices.empty() || std::memcmp(
            left.vertices.data(),
            right.vertices.data(),
            left.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    return verticesEqual && left.indices == right.indices && SameDraw(left.draw, right.draw);
}

void TestUpstreamLayouts()
{
    Require(std::is_standard_layout_v<WebEngineWorldVertex>,
        "world vertex remains standard-layout");
    Require(sizeof(WebEngineWorldVertex) == 44u, "world vertex is exactly 44 bytes");
    Require(offsetof(WebEngineWorldVertex, xyz) == 0u, "xyz offset matches upstream");
    Require(offsetof(WebEngineWorldVertex, binormalSign) == 12u,
        "binormal sign offset matches upstream");
    Require(offsetof(WebEngineWorldVertex, color) == 16u,
        "color offset matches upstream");
    Require(offsetof(WebEngineWorldVertex, textureCoordinate) == 20u,
        "texture-coordinate offset matches upstream");
    Require(offsetof(WebEngineWorldVertex, lightmapCoordinate) == 28u,
        "lightmap-coordinate offset matches upstream");
    Require(offsetof(WebEngineWorldVertex, normal) == 36u,
        "normal offset matches upstream");
    Require(offsetof(WebEngineWorldVertex, tangent) == 40u,
        "tangent offset matches upstream");

    Require(std::is_standard_layout_v<WebEngineWorldSurfaceRange>,
        "surface range remains standard-layout");
    Require(sizeof(WebEngineWorldSurfaceRange) == 16u,
        "surface range is exactly 16 bytes");
    Require(offsetof(WebEngineWorldSurfaceRange, vertexLayerData) == 0u,
        "layer-data offset matches upstream");
    Require(offsetof(WebEngineWorldSurfaceRange, firstVertex) == 4u,
        "first-vertex offset matches upstream");
    Require(offsetof(WebEngineWorldSurfaceRange, vertexCount) == 8u,
        "vertex-count offset matches upstream");
    Require(offsetof(WebEngineWorldSurfaceRange, triangleCount) == 10u,
        "triangle-count offset matches upstream");
    Require(offsetof(WebEngineWorldSurfaceRange, baseIndex) == 12u,
        "base-index offset matches upstream");
    Require(sizeof(GfxWorldVertex) == sizeof(WebEngineWorldVertex),
        "canonical GfxWorld vertex matches the renderer boundary layout");
    Require(sizeof(srfTriangles_t) == sizeof(WebEngineWorldSurfaceRange),
        "canonical GfxWorld range matches the renderer boundary layout");
}

void TestCanonicalGfxWorldAdapter()
{
    std::array<GfxWorldVertex, 7> vertices{
        MakeGfxVertex(99.0f, 99.0f, 99.0f),
        MakeGfxVertex(98.0f, 98.0f, 98.0f),
        MakeGfxVertex(-2.0f, -1.0f, 4.0f, 0xff112233u),
        MakeGfxVertex(2.0f, -1.0f, 4.5f, 0xff445566u),
        MakeGfxVertex(2.0f, 3.0f, 5.0f, 0xff778899u),
        MakeGfxVertex(-2.0f, 3.0f, 5.5f, 0xffaabbccu),
        MakeGfxVertex(97.0f, 97.0f, 97.0f),
    };
    std::array<std::uint16_t, 9> indices{
        0xffffu, 0xffffu, 0xffffu, 0u, 1u, 2u, 2u, 3u, 0u,
    };
    Material material{};
    material.info.name = "mc/killhouse_wall";
    std::array<GfxSurface, 3> surfaces{};
    surfaces[0].tris = {4, 0, 3u, 1u, 0};
    surfaces[1].tris = {0, 0, 3u, 1u, 0};
    surfaces[1].bounds[0][0] = 1.0f;
    surfaces[1].bounds[1][0] = 1.0f;
    surfaces[1].bounds[0][1] = 2.0f;
    surfaces[1].bounds[1][1] = 2.0f;
    surfaces[1].bounds[0][2] = 3.0f;
    surfaces[1].bounds[1][2] = 3.0f;
    surfaces[2].tris = {0, 2, 4u, 2u, 3};
    surfaces[2].material = &material;
    surfaces[2].bounds[0][0] = -2.0f;
    surfaces[2].bounds[0][1] = -1.0f;
    surfaces[2].bounds[0][2] = 4.0f;
    surfaces[2].bounds[1][0] = 2.0f;
    surfaces[2].bounds[1][1] = 3.0f;
    surfaces[2].bounds[1][2] = 5.5f;

    GfxWorld world{};
    world.vertexCount = static_cast<std::uint32_t>(vertices.size());
    world.vd.vertices = vertices.data();
    world.indexCount = static_cast<int>(indices.size());
    world.indices = indices.data();
    world.surfaceCount = static_cast<int>(surfaces.size());
    world.dpvs.surfaces = surfaces.data();

    WebEngineGfxWorldSurfacePublication publication;
    Require(WebEngine_BuildGfxWorldSurface(world, publication) ==
            WebEngineGfxWorldSurfaceResult::Success,
        "canonical GfxWorld adapter selects a bounded base surface");
    Require(publication.surfaceIndex == 2u,
        "canonical adapter skips layered and degenerate candidates deterministically");
    Require(publication.vertexCount == 4u && publication.triangleCount == 2u,
        "canonical adapter retains selected surface counts");
    Require(std::string_view(publication.materialName) == "mc/killhouse_wall",
        "canonical adapter retains the resolved Material identity by name");
    Require(publication.rendererSurface.indices ==
            std::vector<std::uint16_t>({0u, 1u, 2u, 2u, 3u, 0u}),
        "canonical adapter preserves surface-local indices");
    Require(publication.horizontalAxis == 0u && publication.verticalAxis == 1u &&
            publication.depthAxis == 2u,
        "equal major extents use stable canonical axis order");
    RequireNear(publication.rendererSurface.vertices.front().position[2], -0.3075f,
        "canonical adapter preserves the third world axis as renderer depth");

    const WebEngineGfxWorldSurfacePublication before = publication;
    surfaces[2].bounds[0][0] = std::numeric_limits<float>::quiet_NaN();
    Require(WebEngine_BuildGfxWorldSurface(world, publication) ==
            WebEngineGfxWorldSurfaceResult::InvalidSurfaceBounds,
        "malformed canonical surface bounds are rejected");
    Require(publication.surfaceIndex == before.surfaceIndex &&
            SameConverted(publication.rendererSurface, before.rendererSurface),
        "failed canonical adaptation atomically preserves the prior publication");
}

void TestGoldenNonzeroRanges()
{
    const GoldenFixture fixture;
    WebEngineConvertedWorldSurface converted;
    RequireResult(
        WebEngine_ConvertWorldSurface(
            fixture.View(), GoldenProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "convert bounded world surface");

    Require(converted.vertices.size() == 4u, "only the selected vertices are copied");
    Require(converted.indices ==
        std::vector<std::uint16_t>({0u, 1u, 2u, 2u, 3u, 0u}),
        "surface-local indices are copied without rebasing");
    Require(converted.draw.topology == WebRendererPrimitiveTopology::TriangleList,
        "converted draw is a triangle list");
    Require(converted.draw.firstIndex == 0u, "converted draw starts at index zero");
    Require(converted.draw.indexCount == 6u, "converted draw covers both triangles");
    Require(converted.draw.textureBinding == WebRendererTextureBinding::EngineImage,
        "converted draw selects the engine image");

    const std::array<std::array<float, 2>, 4> expectedPositions = {{
        {1.75f, 5.25f},
        {-1.25f, 4.5f},
        {1.75f, -3.75f},
        {7.375f, -3.75f},
    }};
    for (std::size_t index = 0; index < expectedPositions.size(); ++index)
    {
        RequireNear(converted.vertices[index].position[0], expectedPositions[index][0],
            "affine clip X includes xyz and translation");
        RequireNear(converted.vertices[index].position[1], expectedPositions[index][1],
            "affine clip Y includes xyz and translation");
        RequireNear(converted.vertices[index].position[2], 0.0f,
            "generic world-surface conversion keeps compatibility depth at zero");
    }

    RequireNear(converted.vertices[0].color[0], 0x11u / 255.0f,
        "0xAARRGGBB red is decoded numerically");
    RequireNear(converted.vertices[0].color[1], 0x22u / 255.0f,
        "0xAARRGGBB green is decoded numerically");
    RequireNear(converted.vertices[0].color[2], 0x33u / 255.0f,
        "0xAARRGGBB blue is decoded numerically");
    RequireNear(converted.vertices[1].color[0], 0.0f, "zero red is preserved");
    RequireNear(converted.vertices[1].color[1], 1.0f, "full green is preserved");
    RequireNear(converted.vertices[1].color[2], 0x80u / 255.0f,
        "alpha does not shift the blue channel");

    RequireNear(converted.vertices[0].textureCoordinate[0], -2.5f,
        "negative tiled U is not clamped");
    RequireNear(converted.vertices[0].textureCoordinate[1], 3.75f,
        "tiled V above one is not clamped");
    RequireNear(converted.vertices[1].textureCoordinate[0], 4.0f,
        "large tiled U is preserved");
    RequireNear(converted.vertices[1].textureCoordinate[1], -1.0f,
        "negative tiled V is preserved");
}

void TestNullAndZeroDescriptors()
{
    const std::array<WebEngineWorldVertex, 3> vertices = {
        MakeVertex(), MakeVertex(), MakeVertex(),
    };
    const std::array<std::uint16_t, 3> indices = {0u, 1u, 2u};
    const WebEngineWorldSurfaceRange range{0, 0, 3u, 1u, 0};
    const WebEngineWorldProjection2D projection = IdentityProjection();
    WebEngineConvertedWorldSurface converted;

    WebEngineWorldSurfaceView view = MakeView(vertices, indices, range);
    view.world.vertices = nullptr;
    RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
        WebEngineWorldSurfaceResult::InvalidDescriptor, "null vertex array");

    view = MakeView(vertices, indices, range);
    view.world.indices = nullptr;
    RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
        WebEngineWorldSurfaceResult::InvalidDescriptor, "null index array");

    view = MakeView(vertices, indices, range);
    view.world.vertexCount = 0u;
    RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
        WebEngineWorldSurfaceResult::InvalidDescriptor, "zero shared vertex count");

    view = MakeView(vertices, indices, range);
    view.world.indexCount = 0u;
    RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
        WebEngineWorldSurfaceResult::InvalidDescriptor, "zero shared index count");

    view = MakeView(vertices, indices, range);
    view.surface.vertexCount = 0u;
    RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
        WebEngineWorldSurfaceResult::InvalidDescriptor, "zero selected vertex count");

    view = MakeView(vertices, indices, range);
    view.surface.triangleCount = 0u;
    RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
        WebEngineWorldSurfaceResult::InvalidDescriptor, "zero selected triangle count");
}

void TestLayeredFormatRejection()
{
    const std::array<WebEngineWorldVertex, 3> vertices = {
        MakeVertex(), MakeVertex(), MakeVertex(),
    };
    const std::array<std::uint16_t, 3> indices = {0u, 1u, 2u};
    WebEngineWorldSurfaceView view = MakeView(
        vertices, indices, WebEngineWorldSurfaceRange{0, 0, 3u, 1u, 0});
    view.vertexFormat = WebEngineWorldVertexFormat::Layered;
    WebEngineConvertedWorldSurface converted;
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::UnsupportedVertexFormat,
        "layered world vertices are deliberately out of scope");

    view.vertexFormat = static_cast<WebEngineWorldVertexFormat>(0xffu);
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::UnsupportedVertexFormat,
        "unknown world vertex format is rejected");
}

void TestRangesAndExactEnds()
{
    const std::array<WebEngineWorldVertex, 5> vertices = {
        MakeVertex(), MakeVertex(), MakeVertex(), MakeVertex(), MakeVertex(),
    };
    const std::array<std::uint16_t, 6> indices = {
        0xffffu, 0xffffu, 0xffffu, 0u, 1u, 2u,
    };
    const WebEngineWorldProjection2D projection = IdentityProjection();
    WebEngineConvertedWorldSurface converted;

    WebEngineWorldSurfaceView view = MakeView(
        vertices, indices, WebEngineWorldSurfaceRange{0, 2, 3u, 1u, 3});
    RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
        WebEngineWorldSurfaceResult::Success,
        "vertex and index selections may end exactly at shared-array ends");

    const std::array<WebEngineWorldSurfaceRange, 10> invalidRanges = {{
        {0, -1, 3u, 1u, 3},
        {0, 2, 3u, 1u, -1},
        {0, 3, 3u, 1u, 3},
        {0, 5, 1u, 1u, 3},
        {0, 6, 1u, 1u, 3},
        {0, 2, 3u, 1u, 4},
        {0, 2, 3u, 1u, 6},
        {0, 2, 3u, 1u, 7},
        {0, std::numeric_limits<std::int32_t>::max(), 3u, 1u, 3},
        {0, 2, 3u, 1u, std::numeric_limits<std::int32_t>::max()},
    }};
    for (const WebEngineWorldSurfaceRange &range : invalidRanges)
    {
        view.surface = range;
        RequireResult(WebEngine_ConvertWorldSurface(view, projection, converted),
            WebEngineWorldSurfaceResult::InvalidRange,
            "invalid signed or subtraction-safe range");
    }
}

void TestSelectedCaps()
{
    const WebEngineWorldVertex dummyVertex = MakeVertex();
    const std::uint16_t dummyIndex = 0u;
    WebEngineWorldSurfaceView view{
        {&dummyVertex, std::numeric_limits<std::uint32_t>::max(),
            &dummyIndex, std::numeric_limits<std::uint32_t>::max()},
        {0, 0, static_cast<std::uint16_t>(WEB_RENDERER_MAX_SURFACE_VERTICES + 1u),
            1u, 0},
        WebEngineWorldVertexFormat::Base,
    };
    WebEngineConvertedWorldSurface converted;
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::OutputTooLarge,
        "selected vertex count is bounded before source dereference");

    view.surface.vertexCount = 1u;
    view.surface.triangleCount = static_cast<std::uint16_t>(
        WEB_RENDERER_MAX_SURFACE_INDICES / 3u + 1u);
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::OutputTooLarge,
        "selected triangle count is bounded before source dereference");

    std::vector<WebEngineWorldVertex> vertices(
        WEB_RENDERER_MAX_SURFACE_VERTICES, MakeVertex());
    std::vector<std::uint16_t> indices(WEB_RENDERER_MAX_SURFACE_INDICES, 0u);
    view = {
        {vertices.data(), static_cast<std::uint32_t>(vertices.size()),
            indices.data(), static_cast<std::uint32_t>(indices.size())},
        {0, 0, static_cast<std::uint16_t>(vertices.size()),
            static_cast<std::uint16_t>(indices.size() / 3u), 0},
        WebEngineWorldVertexFormat::Base,
    };
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "the exact selected vertex and index caps are accepted");
    Require(converted.vertices.size() == WEB_RENDERER_MAX_SURFACE_VERTICES,
        "exact cap retains every selected vertex");
    Require(converted.indices.size() == WEB_RENDERER_MAX_SURFACE_INDICES,
        "exact cap retains every selected index");
}

void TestSelectedFiniteFields()
{
    std::array<WebEngineWorldVertex, 5> vertices = {
        MakeVertex(), MakeVertex(), MakeVertex(), MakeVertex(), MakeVertex(),
    };
    const std::array<std::uint16_t, 3> indices = {0u, 1u, 2u};
    WebEngineWorldSurfaceView view = MakeView(
        vertices, indices, WebEngineWorldSurfaceRange{0, 1, 3u, 1u, 0});
    const float poison = std::numeric_limits<float>::quiet_NaN();
    vertices[0] = MakeVertex(poison, poison, poison);
    vertices[0].binormalSign = poison;
    vertices[0].textureCoordinate[0] = poison;
    vertices[0].textureCoordinate[1] = poison;
    vertices[0].lightmapCoordinate[0] = poison;
    vertices[0].lightmapCoordinate[1] = poison;
    vertices[4] = vertices[0];

    WebEngineConvertedWorldSurface converted;
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "non-finite guard vertices outside the selected slice are ignored");

    using Poisoner = std::function<void(WebEngineWorldVertex &)>;
    const std::array<std::pair<const char *, Poisoner>, 8> poisoners = {{
        {"xyz[0]", [](WebEngineWorldVertex &vertex) {
            vertex.xyz[0] = std::numeric_limits<float>::quiet_NaN();
        }},
        {"xyz[1]", [](WebEngineWorldVertex &vertex) {
            vertex.xyz[1] = std::numeric_limits<float>::infinity();
        }},
        {"xyz[2]", [](WebEngineWorldVertex &vertex) {
            vertex.xyz[2] = -std::numeric_limits<float>::infinity();
        }},
        {"binormalSign", [](WebEngineWorldVertex &vertex) {
            vertex.binormalSign = std::numeric_limits<float>::quiet_NaN();
        }},
        {"textureCoordinate[0]", [](WebEngineWorldVertex &vertex) {
            vertex.textureCoordinate[0] = std::numeric_limits<float>::infinity();
        }},
        {"textureCoordinate[1]", [](WebEngineWorldVertex &vertex) {
            vertex.textureCoordinate[1] = std::numeric_limits<float>::quiet_NaN();
        }},
        {"lightmapCoordinate[0]", [](WebEngineWorldVertex &vertex) {
            vertex.lightmapCoordinate[0] = -std::numeric_limits<float>::infinity();
        }},
        {"lightmapCoordinate[1]", [](WebEngineWorldVertex &vertex) {
            vertex.lightmapCoordinate[1] = std::numeric_limits<float>::quiet_NaN();
        }},
    }};

    for (const auto &[field, poisonField] : poisoners)
    {
        vertices[2] = MakeVertex();
        poisonField(vertices[2]);
        RequireResult(
            WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
            WebEngineWorldSurfaceResult::NonFiniteVertex,
            std::string("selected non-finite ") + field);
    }
}

void TestNonFiniteProjectionAndResult()
{
    const std::array<WebEngineWorldVertex, 3> vertices = {
        MakeVertex(2.0f, 0.0f, 0.0f), MakeVertex(), MakeVertex(),
    };
    const std::array<std::uint16_t, 3> indices = {0u, 1u, 2u};
    const WebEngineWorldSurfaceView view = MakeView(
        vertices, indices, WebEngineWorldSurfaceRange{0, 0, 3u, 1u, 0});
    WebEngineConvertedWorldSurface converted;

    for (std::size_t component = 0; component < 8u; ++component)
    {
        WebEngineWorldProjection2D projection = IdentityProjection();
        float &selected = component < 4u
            ? projection.clipXFromWorld[component]
            : projection.clipYFromWorld[component - 4u];
        selected = component % 2u == 0u
            ? std::numeric_limits<float>::quiet_NaN()
            : std::numeric_limits<float>::infinity();
        RequireResult(
            WebEngine_ConvertWorldSurface(view, projection, converted),
            WebEngineWorldSurfaceResult::NonFiniteProjection,
            "every non-finite affine projection component is rejected");
    }

    WebEngineWorldProjection2D overflowing = IdentityProjection();
    overflowing.clipXFromWorld[0] = std::numeric_limits<float>::max();
    RequireResult(
        WebEngine_ConvertWorldSurface(view, overflowing, converted),
        WebEngineWorldSurfaceResult::NonFiniteProjection,
        "finite inputs whose affine result exceeds float range are rejected");
}

void TestLocalIndexBounds()
{
    const std::array<WebEngineWorldVertex, 7> vertices = {
        MakeVertex(), MakeVertex(), MakeVertex(), MakeVertex(),
        MakeVertex(), MakeVertex(), MakeVertex(),
    };
    std::array<std::uint16_t, 6> indices = {0xffffu, 0xffffu, 0xffffu, 0u, 1u, 2u};
    WebEngineWorldSurfaceView view = MakeView(
        vertices, indices, WebEngineWorldSurfaceRange{0, 2, 3u, 1u, 3});
    WebEngineConvertedWorldSurface converted;
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "indices are interpreted relative to firstVertex");

    indices[5] = 3u;
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::IndexOutOfRange,
        "index equal to the local vertex count is rejected");

    indices[5] = 4u;
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::IndexOutOfRange,
        "global-valid but surface-local-invalid index is rejected");

    indices[5] = std::numeric_limits<std::uint16_t>::max();
    RequireResult(
        WebEngine_ConvertWorldSurface(view, IdentityProjection(), converted),
        WebEngineWorldSurfaceResult::IndexOutOfRange,
        "maximum local index is rejected for a small surface");
}

void TestSourceIndependence()
{
    GoldenFixture fixture;
    WebEngineConvertedWorldSurface converted;
    RequireResult(
        WebEngine_ConvertWorldSurface(
            fixture.View(), GoldenProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "initial owned conversion");
    const WebEngineConvertedWorldSurface expected = converted;

    fixture.vertices[2] = MakeVertex(99.0f, 98.0f, 97.0f, 0u, 96.0f, 95.0f);
    fixture.indices[3] = 3u;
    fixture.indices[4] = 3u;
    Require(SameConverted(converted, expected),
        "converted world surface does not alias callback-scoped source arrays");
}

void TestAtomicInvalidReplacement()
{
    GoldenFixture fixture;
    WebEngineConvertedWorldSurface destination;
    RequireResult(
        WebEngine_ConvertWorldSurface(
            fixture.View(), GoldenProjection(), destination),
        WebEngineWorldSurfaceResult::Success,
        "seed destination with a valid conversion");
    const WebEngineConvertedWorldSurface before = destination;

    fixture.indices[8] = 4u;
    RequireResult(
        WebEngine_ConvertWorldSurface(
            fixture.View(), GoldenProjection(), destination),
        WebEngineWorldSurfaceResult::IndexOutOfRange,
        "reject invalid replacement after building temporary vertices");
    Require(SameConverted(destination, before),
        "failed conversion atomically preserves vertices, indices, and draw");
}

void TestResultStrings()
{
    const std::array<std::pair<WebEngineWorldSurfaceResult, const char *>, 9> expected = {{
        {WebEngineWorldSurfaceResult::Success, "success"},
        {WebEngineWorldSurfaceResult::InvalidDescriptor,
            "invalid engine world-surface descriptor"},
        {WebEngineWorldSurfaceResult::UnsupportedVertexFormat,
            "unsupported engine world vertex format"},
        {WebEngineWorldSurfaceResult::InvalidRange,
            "engine world-surface range is outside its shared arrays"},
        {WebEngineWorldSurfaceResult::OutputTooLarge,
            "engine world surface exceeds the bounded renderer slice"},
        {WebEngineWorldSurfaceResult::NonFiniteProjection,
            "engine world-surface projection is not finite"},
        {WebEngineWorldSurfaceResult::NonFiniteVertex,
            "engine world surface contains a non-finite vertex"},
        {WebEngineWorldSurfaceResult::IndexOutOfRange,
            "engine world surface contains an out-of-range local index"},
        {WebEngineWorldSurfaceResult::AllocationFailed,
            "engine world-surface conversion allocation failed"},
    }};
    for (const auto &[result, description] : expected)
    {
        Require(std::strcmp(WebEngine_WorldSurfaceResultString(result), description) == 0,
            "result has the stable printable description");
    }
    Require(std::strcmp(
        WebEngine_WorldSurfaceResultString(
            static_cast<WebEngineWorldSurfaceResult>(0xffu)),
        "unknown engine world-surface conversion error") == 0,
        "unknown result has a printable fallback");
}

class Runner
{
public:
    void Run(const char *name, const std::function<void()> &test)
    {
        try
        {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception &error)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    int Result() const
    {
        std::cout << passed_ << " passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};
} // namespace

int main()
{
    Runner runner;
    runner.Run("upstream-compatible layouts", TestUpstreamLayouts);
    runner.Run("canonical GfxWorld adapter", TestCanonicalGfxWorldAdapter);
    runner.Run("golden nonzero ranges", TestGoldenNonzeroRanges);
    runner.Run("null and zero descriptors", TestNullAndZeroDescriptors);
    runner.Run("layered format rejection", TestLayeredFormatRejection);
    runner.Run("range validation and exact ends", TestRangesAndExactEnds);
    runner.Run("selected caps", TestSelectedCaps);
    runner.Run("selected finite fields", TestSelectedFiniteFields);
    runner.Run("non-finite projection and result", TestNonFiniteProjectionAndResult);
    runner.Run("surface-local index bounds", TestLocalIndexBounds);
    runner.Run("source independence", TestSourceIndependence);
    runner.Run("atomic invalid replacement", TestAtomicInvalidReplacement);
    runner.Run("world-surface result strings", TestResultStrings);
    return runner.Result();
}
