#include <web/web_engine_xmodel_surface.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
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
    for (std::size_t byte = 0u; byte < 4u; ++byte)
        bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8u));
}

void PutF32(std::vector<std::uint8_t> &bytes, std::size_t offset, float value)
{
    PutU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void PutVertex(std::vector<std::uint8_t> &bytes, std::size_t index,
    float x, float y, float z, std::uint32_t color,
    std::uint16_t u, std::uint16_t v)
{
    const std::size_t offset = index * 32u;
    PutF32(bytes, offset, x);
    PutF32(bytes, offset + 4u, y);
    PutF32(bytes, offset + 8u, z);
    PutF32(bytes, offset + 12u, 1.0f);
    PutU32(bytes, offset + 16u, color);
    PutU32(bytes, offset + 20u,
        static_cast<std::uint32_t>(u) << 16u | v);
    PutU32(bytes, offset + 24u, 0x7f7fffffu);
    PutU32(bytes, offset + 28u, 0x7f7fffffu);
}

struct Fixture
{
    std::vector<std::uint8_t> vertices = std::vector<std::uint8_t>(4u * 32u);
    std::vector<std::uint8_t> indices = std::vector<std::uint8_t>(6u * 2u);

    Fixture()
    {
        PutVertex(vertices, 0u, -2.0f, 5.0f, -1.0f, 0xff204060u, 0x0000u, 0x0000u);
        PutVertex(vertices, 1u, -2.0f, 5.0f,  1.0f, 0xff80a0c0u, 0x0000u, 0x3c00u);
        PutVertex(vertices, 2u,  2.0f, 5.0f,  1.0f, 0xffffffffu, 0x3c00u, 0x3c00u);
        PutVertex(vertices, 3u,  2.0f, 5.0f, -1.0f, 0xff0000ffu, 0x3c00u, 0x0000u);
        const std::uint16_t values[] = {0u, 1u, 2u, 2u, 3u, 0u};
        for (std::size_t index = 0u; index < 6u; ++index)
            PutU16(indices, index * 2u, values[index]);
    }

    WebEnginePackedXSurfaceView View() const
    {
        return {vertices.data(), vertices.size(), 4u,
            indices.data(), indices.size(), 2u, 17u};
    }
};

void TestGoldenPackedSurface()
{
    const Fixture fixture;
    WebEngineConvertedXModelSurface converted;
    Require(WebEngine_ConvertPackedXModelSurface(fixture.View(), converted) ==
        WebEngineXModelSurfaceResult::Success, "packed surface converts");
    Require(converted.materialIdentity == 17u, "material identity is retained");
    Require(converted.horizontalAxis == 0u && converted.verticalAxis == 2u,
        "largest non-degenerate axes select X and Z");
    Require(converted.rendererSurface.vertices.size() == 4u &&
        converted.rendererSurface.indices ==
            std::vector<std::uint16_t>({0u, 1u, 2u, 2u, 3u, 0u}),
        "local indexed geometry is retained");
    Require(std::fabs(converted.rendererSurface.vertices[0].position[0] + 0.82f) < 0.00001f &&
        std::fabs(converted.rendererSurface.vertices[0].position[1] + 0.41f) < 0.00001f,
        "orthographic fit preserves aspect and clip margin");
    Require(std::fabs(converted.rendererSurface.vertices[0].position[2]) < 0.00001f,
        "constant third axis remains centered at zero depth");
    Require(converted.rendererSurface.vertices[2].textureCoordinate[0] == 1.0f &&
        converted.rendererSurface.vertices[2].textureCoordinate[1] == 1.0f,
        "packed half-float UVs decode in upstream component order");
    Require(std::fabs(converted.rendererSurface.vertices[0].color[0] - 32.0f / 255.0f) < 0.00001f,
        "packed vertex color crosses the existing renderer conversion");
}

void TestThirdAxisPreservesDepth()
{
    Fixture fixture;
    PutF32(fixture.vertices, 4u, 4.5f);
    PutF32(fixture.vertices, 32u + 4u, 4.75f);
    PutF32(fixture.vertices, 64u + 4u, 5.25f);
    PutF32(fixture.vertices, 96u + 4u, 5.5f);
    WebEngineConvertedXModelSurface converted;
    Require(WebEngine_ConvertPackedXModelSurface(fixture.View(), converted) ==
        WebEngineXModelSurfaceResult::Success, "non-planar packed surface converts");
    Require(converted.horizontalAxis == 0u && converted.verticalAxis == 2u,
        "the two largest extents remain projection axes");
    Require(converted.rendererSurface.vertices[0].position[2] < 0.0f &&
        converted.rendererSurface.vertices[3].position[2] > 0.0f,
        "the remaining model axis crosses the renderer seam as signed depth");
}

void TestFailuresAreAtomic()
{
    Fixture fixture;
    WebEngineConvertedXModelSurface destination;
    destination.materialIdentity = 99u;
    destination.rendererSurface.vertices.resize(1u);

    auto view = fixture.View();
    view.materialIdentity = 0u;
    Require(WebEngine_ConvertPackedXModelSurface(view, destination) ==
        WebEngineXModelSurfaceResult::MissingMaterial, "missing material is rejected");
    Require(destination.materialIdentity == 99u &&
        destination.rendererSurface.vertices.size() == 1u,
        "missing material leaves destination unchanged");

    view = fixture.View();
    PutU16(fixture.indices, 0u, 4u);
    Require(WebEngine_ConvertPackedXModelSurface(view, destination) ==
        WebEngineXModelSurfaceResult::IndexOutOfRange, "bad local index is rejected");

    fixture = Fixture{};
    view = fixture.View();
    PutF32(fixture.vertices, 0u, std::numeric_limits<float>::infinity());
    Require(WebEngine_ConvertPackedXModelSurface(view, destination) ==
        WebEngineXModelSurfaceResult::NonFiniteVertex, "non-finite position is rejected");

    fixture = Fixture{};
    view = fixture.View();
    view.packedVertexBytes -= 1u;
    Require(WebEngine_ConvertPackedXModelSurface(view, destination) ==
        WebEngineXModelSurfaceResult::InvalidDescriptor, "truncated vertex bytes are rejected");
}
} // namespace

int main()
{
    try
    {
        TestGoldenPackedSurface();
        TestThirdAxisPreservesDepth();
        TestFailuresAreAtomic();
        std::cout << "web_engine_xmodel_surface_tests: ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "web_engine_xmodel_surface_tests: " << error.what() << '\n';
        return 1;
    }
}
