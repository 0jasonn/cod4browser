#include <qcommon/iwd_archive.h>
#include <qcommon/iwi_image.h>
#include <web/web_shader_compatibility.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace
{
void ExerciseIwi(std::span<const std::uint8_t> bytes)
{
    kisak::iwi::Metadata metadata;
    (void)kisak::iwi::Parse(bytes, metadata);
    kisak::iwi::Rgba8Image image;
    (void)kisak::iwi::DecodeRgba8(bytes, image);
}

void ExerciseShader(std::span<const std::uint8_t> bytes)
{
    kisak::web::D3D9ShaderContract contract;
    (void)kisak::web::DecodeD3D9Shader(bytes, {16384u, 4096u, 64u, 255u}, contract);
}

void ExerciseMember(
    std::span<const std::uint8_t> archive,
    const kisak::iwd::CentralDirectoryLocator &locator,
    const kisak::iwd::Entry &entry)
{
    constexpr std::size_t LOCAL_PREFIX_SIZE = 30u;
    if (entry.localHeaderOffset > archive.size() ||
        archive.size() - entry.localHeaderOffset < LOCAL_PREFIX_SIZE)
    {
        return;
    }

    const auto prefix = archive.subspan(entry.localHeaderOffset, LOCAL_PREFIX_SIZE);
    std::uint32_t requiredHeaderBytes = 0;
    if (kisak::iwd::RequiredLocalHeaderBytes(prefix, requiredHeaderBytes) !=
            kisak::iwd::Error::None ||
        requiredHeaderBytes > archive.size() - entry.localHeaderOffset)
    {
        return;
    }

    kisak::iwd::MemberLocation location;
    if (kisak::iwd::ValidateLocalHeader(
            entry,
            locator,
            archive.subspan(entry.localHeaderOffset, requiredHeaderBytes),
            location) != kisak::iwd::Error::None ||
        location.dataOffset > archive.size() ||
        location.compressedSize > archive.size() - location.dataOffset)
    {
        return;
    }

    kisak::iwd::MemberDecoder decoder;
    if (decoder.Begin(entry) != kisak::iwd::Error::None)
    {
        return;
    }

    const auto compressed = archive.subspan(location.dataOffset, location.compressedSize);
    std::array<std::uint8_t, 257> output{};
    std::size_t inputOffset = 0;
    std::size_t iterations = 0;
    constexpr std::size_t MAX_ITERATIONS = 1u << 18u;
    while ((inputOffset < compressed.size() ||
            decoder.Progress() == kisak::iwd::DecoderProgress::NeedsOutput) &&
        iterations++ < MAX_ITERATIONS)
    {
        const std::size_t inputLength = std::min<std::size_t>(
            compressed.size() - inputOffset,
            113u);
        std::size_t consumed = 0;
        std::size_t produced = 0;
        if (decoder.Consume(
                compressed.subspan(inputOffset, inputLength),
                output,
                consumed,
                produced) != kisak::iwd::Error::None ||
            consumed > inputLength ||
            (consumed == 0u && produced == 0u))
        {
            return;
        }
        inputOffset += consumed;
    }
    if (iterations < MAX_ITERATIONS)
    {
        (void)decoder.Finish();
    }
}

void ExerciseIwd(std::span<const std::uint8_t> archive)
{
    if (archive.size() < 22u ||
        archive.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return;
    }

    constexpr std::size_t MAX_TAIL_SIZE = 22u + 0xffffu;
    const std::size_t tailSize = std::min(archive.size(), MAX_TAIL_SIZE);
    const std::size_t tailOffset = archive.size() - tailSize;
    kisak::iwd::CentralDirectoryLocator locator;
    if (kisak::iwd::LocateCentralDirectory(
            archive.subspan(tailOffset),
            tailOffset,
            archive.size(),
            {},
            locator) != kisak::iwd::Error::None ||
        locator.centralOffset > archive.size() ||
        locator.centralSize > archive.size() - locator.centralOffset)
    {
        return;
    }

    kisak::iwd::ArchiveIndex index;
    if (kisak::iwd::ParseCentralDirectory(
            archive.subspan(locator.centralOffset, locator.centralSize),
            locator,
            {},
            index) != kisak::iwd::Error::None)
    {
        return;
    }

    std::size_t exercised = 0;
    for (const kisak::iwd::Entry &entry : index.Entries())
    {
        if (!entry.directory)
        {
            ExerciseMember(archive, locator, entry);
            if (++exercised == 16u)
            {
                break;
            }
        }
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    if (!data && size != 0u)
    {
        return 0;
    }
    const std::span<const std::uint8_t> bytes(data, size);
    ExerciseIwi(bytes);
    ExerciseIwd(bytes);
    ExerciseShader(bytes);
    return 0;
}
