#include <web/web_shader_compatibility.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>

namespace kisak::web
{
namespace
{
constexpr std::uint32_t COMMENT_OPCODE = 0xfffeu;
constexpr std::uint32_t END_OPCODE = 0xffffu;
constexpr std::uint32_t CTAB_FOURCC = 0x42415443u;
constexpr std::uint32_t VERTEX_VERSION = 0xfffe0101u;
constexpr std::uint32_t PIXEL_VERSION = 0xffff0200u;
constexpr std::uint32_t EXPECTED_ROUTING_HASH = 0x5bc9b27cu;

constexpr const char *VERTCOL_VERTEX_GLSL = R"glsl(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_texcoord0;
uniform mat4 u_viewProjectionMatrix;
uniform mat4 u_worldMatrix;
out vec4 v_color;
out vec2 v_texcoord0;
void main() {
    gl_Position = u_viewProjectionMatrix * u_worldMatrix * vec4(a_position, 1.0);
    v_color = a_color;
    v_texcoord0 = a_texcoord0;
}
)glsl";

constexpr const char *VERTCOL_FRAGMENT_GLSL = R"glsl(#version 300 es
precision mediump float;
uniform sampler2D u_colorMapSampler;
in vec4 v_color;
in vec2 v_texcoord0;
out vec4 outColor;
void main() {
    vec4 texel = texture(u_colorMapSampler, v_texcoord0);
    if (texel.a <= 0.0)
        discard;
    outColor = texel * v_color;
}
)glsl";

std::uint16_t ReadU16(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadU32(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
        static_cast<std::uint32_t>(bytes[1]) << 8u |
        static_cast<std::uint32_t>(bytes[2]) << 16u |
        static_cast<std::uint32_t>(bytes[3]) << 24u;
}

std::uint32_t Fnv1a32(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint32_t value = 2166136261u;
    for (const std::uint8_t byte : bytes)
    {
        value ^= byte;
        value *= 16777619u;
    }
    return value;
}

std::uint32_t Fnv1a32(std::string_view text) noexcept
{
    return Fnv1a32({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

bool ReadString(
    std::span<const std::uint8_t> table,
    std::uint32_t offset,
    std::uint32_t maxBytes,
    std::string &destination) noexcept
{
    if (offset >= table.size()) return false;
    const std::size_t available = table.size() - offset;
    const std::size_t limit = std::min<std::size_t>(available, maxBytes);
    const auto begin = table.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = begin + static_cast<std::ptrdiff_t>(limit);
    const auto terminator = std::find(begin, end, 0u);
    if (terminator == end) return false;
    try
    {
        destination.assign(reinterpret_cast<const char *>(table.data() + offset),
            static_cast<std::size_t>(terminator - begin));
    }
    catch (...) { return false; }
    return !destination.empty();
}

ShaderDecodeError ParseConstantTable(
    std::span<const std::uint8_t> table,
    const ShaderDecodeLimits &limits,
    std::uint32_t version,
    D3D9ShaderContract &contract) noexcept
{
    constexpr std::uint32_t HEADER_BYTES = 28u;
    constexpr std::uint32_t INFO_BYTES = 20u;
    if (table.size() < HEADER_BYTES || ReadU32(table.data()) != HEADER_BYTES ||
        ReadU32(table.data() + 8u) != version)
        return ShaderDecodeError::InvalidConstantTable;
    const std::uint32_t count = ReadU32(table.data() + 12u);
    const std::uint32_t infoOffset = ReadU32(table.data() + 16u);
    if (count > limits.maxConstants) return ShaderDecodeError::LimitExceeded;
    const std::uint64_t infoEnd = static_cast<std::uint64_t>(infoOffset) +
        static_cast<std::uint64_t>(count) * INFO_BYTES;
    if (infoOffset < HEADER_BYTES || infoEnd > table.size())
        return ShaderDecodeError::InvalidConstantTable;
    if (!ReadString(table, ReadU32(table.data() + 4u),
            limits.maxStringBytes, contract.compiler) ||
        !ReadString(table, ReadU32(table.data() + 24u),
            limits.maxStringBytes, contract.target))
        return ShaderDecodeError::InvalidConstantTable;
    try { contract.constants.reserve(count); }
    catch (...) { return ShaderDecodeError::AllocationFailed; }
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const std::uint8_t *info = table.data() + infoOffset + index * INFO_BYTES;
        ShaderConstantBinding binding;
        if (!ReadString(table, ReadU32(info), limits.maxStringBytes, binding.name))
            return ShaderDecodeError::InvalidConstantTable;
        binding.registerSet = ReadU16(info + 4u);
        binding.registerIndex = ReadU16(info + 6u);
        binding.registerCount = ReadU16(info + 8u);
        const std::uint32_t typeOffset = ReadU32(info + 12u);
        if (binding.registerCount == 0u || typeOffset > table.size() ||
            table.size() - typeOffset < 16u)
            return ShaderDecodeError::InvalidConstantTable;
        try { contract.constants.push_back(std::move(binding)); }
        catch (...) { return ShaderDecodeError::AllocationFailed; }
    }
    return ShaderDecodeError::None;
}

std::uint32_t Vertex11OperandCount(std::uint32_t opcode) noexcept
{
    switch (opcode)
    {
    case 0x01u: return 2u; // mov
    case 0x04u: return 4u; // mad
    case 0x09u: return 3u; // dp4
    case 0x1fu: return 2u; // dcl
    case 0x51u: return 5u; // def
    default: return UINT32_MAX;
    }
}

void CountOpcode(std::uint32_t opcode, D3D9ShaderContract &contract) noexcept
{
    switch (opcode)
    {
    case 0x01u: ++contract.movCount; break;
    case 0x04u: ++contract.madCount; break;
    case 0x05u: ++contract.mulCount; break;
    case 0x09u: ++contract.dp4Count; break;
    case 0x1fu: ++contract.dclCount; break;
    case 0x42u: ++contract.texldCount; break;
    case 0x51u: ++contract.defCount; break;
    default: break;
    }
}

bool HasBinding(const D3D9ShaderContract &contract, std::string_view name,
    std::uint16_t set, std::uint16_t index, std::uint16_t count) noexcept
{
    return std::any_of(contract.constants.begin(), contract.constants.end(),
        [&](const ShaderConstantBinding &binding) {
            return binding.name == name && binding.registerSet == set &&
                binding.registerIndex == index && binding.registerCount == count;
        });
}
} // namespace

ShaderDecodeError DecodeD3D9Shader(
    std::span<const std::uint8_t> program,
    const ShaderDecodeLimits &limits,
    D3D9ShaderContract &destination) noexcept
{
    if (program.empty() || program.size() % 4u != 0u ||
        limits.maxProgramDwords == 0u || limits.maxInstructions == 0u ||
        limits.maxConstants == 0u || limits.maxStringBytes == 0u)
        return ShaderDecodeError::InvalidArgument;
    const std::size_t dwordCount = program.size() / 4u;
    if (dwordCount > limits.maxProgramDwords || dwordCount > UINT32_MAX)
        return ShaderDecodeError::LimitExceeded;
    D3D9ShaderContract contract;
    const std::uint32_t version = ReadU32(program.data());
    if (version == VERTEX_VERSION)
    {
        contract.stage = ShaderStage::Vertex;
        contract.majorVersion = 1u;
        contract.minorVersion = 1u;
    }
    else if (version == PIXEL_VERSION)
    {
        contract.stage = ShaderStage::Pixel;
        contract.majorVersion = 2u;
        contract.minorVersion = 0u;
    }
    else return ShaderDecodeError::InvalidVersion;
    contract.programDwords = static_cast<std::uint32_t>(dwordCount);
    contract.programHash = Fnv1a32(program);
    bool foundEnd = false;
    bool foundConstantTable = false;
    std::size_t cursor = 1u;
    while (cursor < dwordCount)
    {
        const std::uint32_t token = ReadU32(program.data() + cursor * 4u);
        const std::uint32_t opcode = token & 0xffffu;
        if (opcode == END_OPCODE)
        {
            if (cursor + 1u != dwordCount) return ShaderDecodeError::InvalidToken;
            foundEnd = true;
            break;
        }
        if (opcode == COMMENT_OPCODE)
        {
            const std::uint32_t length = (token >> 16u) & 0x7fffu;
            if (length == 0u || length > dwordCount - cursor - 1u)
                return ShaderDecodeError::Truncated;
            ++contract.commentCount;
            if (ReadU32(program.data() + (cursor + 1u) * 4u) == CTAB_FOURCC)
            {
                if (foundConstantTable || length == 1u)
                    return ShaderDecodeError::InvalidConstantTable;
                const auto table = program.subspan((cursor + 2u) * 4u,
                    static_cast<std::size_t>(length - 1u) * 4u);
                const ShaderDecodeError error = ParseConstantTable(
                    table, limits, version, contract);
                if (error != ShaderDecodeError::None) return error;
                foundConstantTable = true;
            }
            cursor += static_cast<std::size_t>(length) + 1u;
            continue;
        }
        std::uint32_t operandCount = 0u;
        if (contract.stage == ShaderStage::Pixel)
            operandCount = (token >> 24u) & 0x0fu;
        else
            operandCount = Vertex11OperandCount(opcode);
        if (operandCount == UINT32_MAX || operandCount > dwordCount - cursor - 1u)
            return ShaderDecodeError::InvalidToken;
        ++contract.instructionCount;
        if (contract.instructionCount > limits.maxInstructions)
            return ShaderDecodeError::LimitExceeded;
        CountOpcode(opcode, contract);
        cursor += static_cast<std::size_t>(operandCount) + 1u;
    }
    if (!foundEnd) return ShaderDecodeError::MissingEnd;
    if (!foundConstantTable) return ShaderDecodeError::InvalidConstantTable;
    destination = std::move(contract);
    return ShaderDecodeError::None;
}

bool SelectWebGL2ShaderSubstitution(
    const D3D9ShaderContract &vertex,
    const D3D9ShaderContract &pixel,
    std::uint32_t vertexStreamRoutingHash,
    WebGL2ShaderSubstitution &destination) noexcept
{
    if (vertex.stage != ShaderStage::Vertex || vertex.majorVersion != 1u ||
        vertex.minorVersion != 1u || vertex.instructionCount != 15u ||
        vertex.defCount != 1u || vertex.dclCount != 3u || vertex.madCount != 1u ||
        vertex.dp4Count != 8u || vertex.movCount != 2u ||
        !HasBinding(vertex, "viewProjectionMatrix", 2u, 0u, 4u) ||
        !HasBinding(vertex, "worldMatrix", 2u, 4u, 4u) ||
        pixel.stage != ShaderStage::Pixel || pixel.majorVersion != 2u ||
        pixel.minorVersion != 0u || pixel.instructionCount != 6u ||
        pixel.dclCount != 3u || pixel.texldCount != 1u ||
        pixel.mulCount != 1u || pixel.movCount != 1u ||
        !HasBinding(pixel, "colorMapSampler", 3u, 0u, 1u) ||
        vertexStreamRoutingHash != EXPECTED_ROUTING_HASH)
        return false;
    return LookupWebGL2ShaderSubstitution(
        "webgl2.vertcol_simple2d.v1", destination);
}

bool LookupWebGL2ShaderSubstitution(
    std::string_view id,
    WebGL2ShaderSubstitution &destination) noexcept
{
    if (id != "webgl2.vertcol_simple2d.v1") return false;
    WebGL2ShaderSubstitution selected;
    selected.id = "webgl2.vertcol_simple2d.v1";
    selected.vertexSource = VERTCOL_VERTEX_GLSL;
    selected.fragmentSource = VERTCOL_FRAGMENT_GLSL;
    selected.vertexSourceHash = Fnv1a32(VERTCOL_VERTEX_GLSL);
    selected.fragmentSourceHash = Fnv1a32(VERTCOL_FRAGMENT_GLSL);
    destination = selected;
    return true;
}

const char *ShaderDecodeErrorString(ShaderDecodeError error) noexcept
{
    switch (error)
    {
    case ShaderDecodeError::None: return "none";
    case ShaderDecodeError::InvalidArgument: return "invalid argument";
    case ShaderDecodeError::InvalidVersion: return "unsupported shader version";
    case ShaderDecodeError::Truncated: return "truncated shader token stream";
    case ShaderDecodeError::InvalidToken: return "invalid shader token stream";
    case ShaderDecodeError::MissingEnd: return "shader END token missing";
    case ShaderDecodeError::InvalidConstantTable: return "invalid shader CTAB";
    case ShaderDecodeError::LimitExceeded: return "shader decode limit exceeded";
    case ShaderDecodeError::AllocationFailed: return "shader decode allocation failed";
    }
    return "unknown shader decode error";
}

} // namespace kisak::web
