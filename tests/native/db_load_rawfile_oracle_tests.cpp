#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_semantic_trace.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{

using kisak::database::SemanticTraceEntry;
using kisak::database::SemanticTraceEventKind;

RawFile *g_publishedRawFile;
RawFile *g_publicationReplacement;
std::vector<std::uint8_t> g_input;
std::size_t g_inputPosition;

RawFile *LoadRawFileOracle(std::uint32_t assetIndex)
{
#if defined(KISAK_GENERATED_DB_LOAD_RAWFILE_ORACLE)
    return DB_LoadGeneratedRawFileOracle(assetIndex);
#else
    return DB_LoadNativeRawFileOracle(assetIndex);
#endif
}

void CollectTrace(const SemanticTraceEntry &entry, void *context)
{
    static_cast<std::vector<SemanticTraceEntry> *>(context)->push_back(entry);
}

void InitializeStreams(
    XZoneMemory &zone,
    std::array<std::uint8_t, 64> &data,
    std::array<std::uint8_t, 96> &virtuals)
{
    zone = {};
    zone.blocks[0] = {data.data(), static_cast<std::uint32_t>(data.size())};
    zone.blocks[4] = {
        virtuals.data(), static_cast<std::uint32_t>(virtuals.size())};
    DB_InitStreams(&zone);
    g_publishedRawFile = nullptr;
    g_publicationReplacement = nullptr;
    g_input.clear();
    g_inputPosition = 0;
}

// Repository-authored synthetic bytes, GPL-3.0 like the surrounding tests.
// Block-4 insert slots reserve memory but consume no serialized input.
void PrepareInlineInput(const std::array<std::uint8_t, 64> &data,
    const std::array<std::uint8_t, 96> &virtuals,
    std::size_t nameOffset, std::size_t stringBytes)
{
    g_input.assign(virtuals.begin(), virtuals.begin() + 4u);
    g_input.insert(g_input.end(), data.begin(), data.begin() + sizeof(RawFile));
    g_input.insert(g_input.end(), virtuals.begin() + nameOffset,
        virtuals.begin() + nameOffset + stringBytes);
}

void AssertFinished(const XZoneMemory &zone, std::size_t virtualBytes)
{
    assert(g_inputPosition == g_input.size());
    assert(g_streamPosStackIndex == 0u && g_streamPosIndex == 0u);
    assert(g_streamPos == zone.blocks[0].data);
    assert(g_streamPosArray[4] == zone.blocks[4].data + virtualBytes);
}

void WriteRawFile(
    std::array<std::uint8_t, 64> &data,
    std::uint32_t nameToken,
    std::uint32_t bufferToken)
{
    RawFile disk{};
    disk.name = reinterpret_cast<const char *>(nameToken);
    disk.len = 4;
    disk.buffer = reinterpret_cast<const char *>(bufferToken);
    std::memcpy(data.data(), &disk, sizeof(disk));
}

void AssertInlineTrace(
    const std::vector<SemanticTraceEntry> &trace, const char *name)
{
    assert(trace.size() == 2u);
    const SemanticTraceEntry &begin = trace[0];
    const SemanticTraceEntry &publish = trace[1];
    assert(begin.kind == SemanticTraceEventKind::AssetBegin);
    assert(publish.kind == SemanticTraceEventKind::AssetPublish);
    assert(begin.assetType == ASSET_TYPE_RAWFILE);
    assert(publish.assetType == ASSET_TYPE_RAWFILE);
    assert(begin.assetIndex == 0u);
    assert(publish.assetIndex == 0u);
    assert(begin.streamBlock == 0u && begin.streamOffset == 0u);
    assert(publish.streamBlock == 0u && publish.streamOffset == 0u);
    assert(begin.relatedBlock == 4u && begin.relatedOffset == 0u);
    assert(publish.relatedBlock == 4u && publish.relatedOffset == 0u);
    assert(begin.name.empty());
    assert(publish.name == name);
}

void TestInlineRawFile()
{
    alignas(16) std::array<std::uint8_t, 64> data{};
    alignas(16) std::array<std::uint8_t, 96> virtuals{};
    WriteRawFile(data, UINT32_MAX, UINT32_MAX);

    const std::uint32_t inlinePointer = UINT32_MAX;
    std::memcpy(virtuals.data(), &inlinePointer, sizeof(inlinePointer));
    constexpr char name[] = "tests/native-oracle";
    constexpr char contents[] = "data";
    std::memcpy(virtuals.data() + 4u, name, sizeof(name));
    std::memcpy(virtuals.data() + 4u + sizeof(name), contents, sizeof(contents));

    XZoneMemory zone{};
    InitializeStreams(zone, data, virtuals);
    PrepareInlineInput(data, virtuals, 4u, sizeof(name) + sizeof(contents));
    data.fill(0);
    virtuals.fill(0);
    std::vector<SemanticTraceEntry> trace;
    kisak::database::SetSemanticTraceObserver(CollectTrace, &trace);
    RawFile *rawFile = LoadRawFileOracle(0u);
    kisak::database::ClearSemanticTraceObserver();

    AssertInlineTrace(trace, name);
    assert(rawFile == reinterpret_cast<RawFile *>(data.data()));
    assert(g_publishedRawFile == rawFile);
    assert(rawFile->name == reinterpret_cast<const char *>(virtuals.data() + 4u));
    assert(rawFile->buffer ==
        reinterpret_cast<const char *>(virtuals.data() + 4u + sizeof(name)));
    assert(std::strcmp(rawFile->buffer, contents) == 0);
    AssertFinished(zone, 4u + sizeof(name) + sizeof(contents));
}

void TestInsertedRawFilePointer(bool replaceAtPublication)
{
    alignas(16) std::array<std::uint8_t, 64> data{};
    alignas(16) std::array<std::uint8_t, 96> virtuals{};
    WriteRawFile(data, UINT32_MAX, UINT32_MAX);

    const std::uint32_t insertedPointer = UINT32_MAX - 1u;
    std::memcpy(virtuals.data(), &insertedPointer, sizeof(insertedPointer));
    constexpr char name[] = "tests/native-oracle";
    constexpr char contents[] = "data";
    std::memcpy(virtuals.data() + 8u, name, sizeof(name));
    std::memcpy(virtuals.data() + 8u + sizeof(name), contents, sizeof(contents));

    XZoneMemory zone{};
    InitializeStreams(zone, data, virtuals);
    PrepareInlineInput(data, virtuals, 8u, sizeof(name) + sizeof(contents));
    data.fill(0);
    virtuals.fill(0);
    RawFile canonical{"tests/published-oracle", 4, "keep"};
    if (replaceAtPublication) g_publicationReplacement = &canonical;
    std::vector<SemanticTraceEntry> trace;
    kisak::database::SetSemanticTraceObserver(CollectTrace, &trace);
    RawFile *rawFile = LoadRawFileOracle(0u);
    kisak::database::ClearSemanticTraceObserver();

    AssertInlineTrace(trace, replaceAtPublication ? canonical.name : name);
    RawFile *storedPointer = nullptr;
    std::memcpy(&storedPointer, virtuals.data() + 4u, sizeof(storedPointer));
    const RawFile *loaded = reinterpret_cast<RawFile *>(data.data());
    assert(rawFile == (replaceAtPublication ? &canonical : loaded));
    assert(storedPointer == rawFile);
    assert(g_publishedRawFile == loaded);
    assert(loaded->name == reinterpret_cast<const char *>(virtuals.data() + 8u));
    assert(std::strcmp(loaded->buffer, contents) == 0);
    AssertFinished(zone, 8u + sizeof(name) + sizeof(contents));
}

void TestRawFileAlias()
{
    alignas(16) std::array<std::uint8_t, 64> data{};
    alignas(16) std::array<std::uint8_t, 96> virtuals{};
    RawFile target{"tests/native-oracle-alias", 4, "data"};

    constexpr std::uint32_t aliasToken = 0x40000005u;
    std::memcpy(virtuals.data(), &aliasToken, sizeof(aliasToken));
    RawFile *targetPointer = &target;
    std::memcpy(virtuals.data() + 4u, &targetPointer, sizeof(targetPointer));

    XZoneMemory zone{};
    InitializeStreams(zone, data, virtuals);
    g_input.assign(virtuals.begin(), virtuals.begin() + 4u);
    std::vector<SemanticTraceEntry> trace;
    kisak::database::SetSemanticTraceObserver(CollectTrace, &trace);
    RawFile *rawFile = LoadRawFileOracle(0u);
    kisak::database::ClearSemanticTraceObserver();

    assert(rawFile == &target);
    assert(trace.empty());
    assert(g_publishedRawFile == nullptr);
    AssertFinished(zone, 4u);
}

} // namespace

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

void __cdecl DB_LoadXFileData(std::uint8_t *destination, std::uint32_t size)
{
    assert(g_inputPosition <= g_input.size());
    assert(size <= g_input.size() - g_inputPosition);
    std::memcpy(destination, g_input.data() + g_inputPosition, size);
    g_inputPosition += size;
}

#if defined(KISAK_GENERATED_DB_LOAD_RAWFILE_ORACLE)
void DB_RuntimeGeneratedFailure(const char *)
{
    std::abort();
}

bool DB_RuntimeGeneratedLoadFailed()
{
    return false;
}

void DB_RuntimeTraceStreamsInitialized(std::uint32_t, std::uint32_t)
{
}

void DB_RuntimeTraceAssetLoaded(const char *)
{
}
#endif

void __cdecl Load_RawFileAsset(XAssetHeader *rawFile)
{
    assert(rawFile);
    g_publishedRawFile = rawFile->rawfile;
    if (g_publicationReplacement) rawFile->rawfile = g_publicationReplacement;
}

int main()
{
    static_assert(sizeof(void *) == 4u);
    static_assert(sizeof(RawFile) == 12u);
    TestInlineRawFile();
    TestInsertedRawFilePointer(false);
    TestInsertedRawFilePointer(true);
    TestRawFileAlias();
#if defined(KISAK_GENERATED_DB_LOAD_RAWFILE_ORACLE)
    constexpr const char *loader = "adapted-generated";
#else
    constexpr const char *loader = "native-db-load";
#endif
    std::printf(
        "%s-rawfile inline=published insert=-2 "
        "alias=block4:4 replacement=published reads=exact trace=asset-begin,asset-publish\n",
        loader);
    return 0;
}
