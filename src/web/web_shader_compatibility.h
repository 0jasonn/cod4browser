#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kisak::web
{

enum class ShaderStage : std::uint8_t
{
    Vertex = 0,
    Pixel,
};

enum class ShaderDecodeError : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidVersion,
    Truncated,
    InvalidToken,
    MissingEnd,
    InvalidConstantTable,
    LimitExceeded,
    AllocationFailed,
};

struct ShaderConstantBinding
{
    std::string name;
    std::uint16_t registerSet = 0u;
    std::uint16_t registerIndex = 0u;
    std::uint16_t registerCount = 0u;
};

struct D3D9ShaderContract
{
    ShaderStage stage = ShaderStage::Vertex;
    std::uint8_t majorVersion = 0u;
    std::uint8_t minorVersion = 0u;
    std::uint32_t programDwords = 0u;
    std::uint32_t programHash = 0u;
    std::uint32_t commentCount = 0u;
    std::uint32_t instructionCount = 0u;
    std::uint32_t movCount = 0u;
    std::uint32_t madCount = 0u;
    std::uint32_t mulCount = 0u;
    std::uint32_t dp4Count = 0u;
    std::uint32_t dclCount = 0u;
    std::uint32_t defCount = 0u;
    std::uint32_t texldCount = 0u;
    std::string compiler;
    std::string target;
    std::vector<ShaderConstantBinding> constants;
};

struct ShaderDecodeLimits
{
    std::uint32_t maxProgramDwords = 16384u;
    std::uint32_t maxInstructions = 4096u;
    std::uint32_t maxConstants = 64u;
    std::uint32_t maxStringBytes = 255u;
};

struct WebGL2ShaderSubstitution
{
    const char *id = nullptr;
    const char *vertexSource = nullptr;
    const char *fragmentSource = nullptr;
    std::uint32_t vertexSourceHash = 0u;
    std::uint32_t fragmentSourceHash = 0u;
};

ShaderDecodeError DecodeD3D9Shader(
    std::span<const std::uint8_t> program,
    const ShaderDecodeLimits &limits,
    D3D9ShaderContract &destination) noexcept;

bool SelectWebGL2ShaderSubstitution(
    const D3D9ShaderContract &vertex,
    const D3D9ShaderContract &pixel,
    std::uint32_t vertexStreamRoutingHash,
    WebGL2ShaderSubstitution &destination) noexcept;

// Resolves only substitutions compiled into this GPL browser port. Imported
// data can select a known contract, but can never supply executable GLSL text.
bool LookupWebGL2ShaderSubstitution(
    std::string_view id,
    WebGL2ShaderSubstitution &destination) noexcept;

const char *ShaderDecodeErrorString(ShaderDecodeError error) noexcept;

} // namespace kisak::web
