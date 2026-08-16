#include <web/web_retail_fastfile_census.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

#include <zlib.h>

namespace
{
kisak::fastfile::RetailCensusLimits FuzzLimits() noexcept
{
    kisak::fastfile::RetailCensusLimits limits;
    limits.maxFileBytes = 256u * 1024u;
    limits.maxSourceChunkBytes = 4096u;
    limits.maxInflatedPrefixBytes = 512u * 1024u;
    limits.maxBlockBytes = 512u * 1024u;
    limits.maxTotalBlockBytes = 1024u * 1024u;
    limits.maxScriptStrings = 128u;
    limits.maxTotalScriptStringBytes = 16u * 1024u;
    limits.maxAssets = 256u;
    limits.maxRegistryAssets = 512u;
    limits.maxRegistryAliases = 2048u;
    limits.maxRegistryNameBytes = 64u * 1024u;
    limits.maxWorldXModels = 128u;
    limits.maxXAnimParts = 128u;
    limits.maxWeapons = 64u;
    limits.maxRawFiles = 128u;
    limits.maxLocalizeEntries = 128u;
    limits.maxSoundAliasLists = 128u;
    limits.maxFxEffects = 128u;
    limits.maxRetainedXAnimBytes = 256u * 1024u;
    limits.maxRetainedWeaponBytes = 256u * 1024u;
    limits.maxRetainedRawFileBytes = 256u * 1024u;
    limits.maxRetainedLocalizeBytes = 128u * 1024u;
    limits.maxRetainedSoundBytes = 256u * 1024u;
    limits.maxSemanticTraceEntries = 1024u;
    return limits;
}

void RunDispatcher(std::span<const std::uint8_t> input)
{
    kisak::fastfile::RetailFastfileCensusJob job;
    if (job.BeginStreaming(
            kisak::fastfile::RetailCensusMode::WorldAssetLoader,
            FuzzLimits()) != kisak::fastfile::RetailCensusError::None)
        return;

    std::size_t offset = 0u;
    std::size_t iterations = 0u;
    constexpr std::size_t MAX_ITERATIONS = 4096u;
    while (job.Progress() == kisak::fastfile::RetailCensusProgress::Running &&
           iterations++ < MAX_ITERATIONS)
    {
        if (job.NeedsSource())
        {
            const std::size_t remaining = input.size() - offset;
            const std::size_t chunk = std::min<std::size_t>(
                remaining,
                1u + ((input.size() + iterations * 131u) % 4096u));
            const bool final = chunk == remaining;
            if (job.FeedSource(input.subspan(offset, chunk), final) !=
                    kisak::fastfile::RetailCensusError::None)
                break;
            offset += chunk;
        }
        (void)job.Step({4096u, 8u});
    }

    kisak::fastfile::RetailFastfileCensus result;
    if (job.Progress() == kisak::fastfile::RetailCensusProgress::Succeeded)
        (void)job.TakeResult(result);
    job.Reset();
}

std::vector<std::uint8_t> WrapAsSyntheticFastfile(
    std::span<const std::uint8_t> inflated)
{
    constexpr std::size_t MAX_INFLATED_INPUT = 192u * 1024u;
    if (inflated.size() > MAX_INFLATED_INPUT) return {};

    uLongf compressedSize = compressBound(static_cast<uLong>(inflated.size()));
    std::vector<std::uint8_t> file(12u + compressedSize);
    constexpr std::uint8_t HEADER[12] = {
        'I', 'W', 'f', 'f', 'u', '1', '0', '0', 5u, 0u, 0u, 0u,
    };
    std::copy(std::begin(HEADER), std::end(HEADER), file.begin());
    constexpr std::uint8_t EMPTY_INPUT = 0u;
    const std::uint8_t *source = inflated.empty()
        ? &EMPTY_INPUT : inflated.data();
    if (compress2(
            file.data() + 12u, &compressedSize,
            source, static_cast<uLong>(inflated.size()),
            Z_BEST_SPEED) != Z_OK)
    {
        return {};
    }
    file.resize(12u + compressedSize);
    return file;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t *data,
    std::size_t size)
{
    if (!data && size != 0u) return 0;

    const std::span<const std::uint8_t> input(data, size);
    // Exercise both malformed physical files and arbitrary inflated stream
    // bytes behind a valid legal synthetic fastfile envelope.  The latter lets
    // mutations reach the real dispatcher, pointer tokens, counts, nested
    // arrays, publication paths, and stream-block transitions instead of
    // concentrating only on the outer header and zlib decoder.
    RunDispatcher(input);
    const std::vector<std::uint8_t> wrapped = WrapAsSyntheticFastfile(input);
    if (!wrapped.empty()) RunDispatcher(wrapped);
    return 0;
}
