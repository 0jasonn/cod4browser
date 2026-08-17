#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/material_types.h>
#include <physics/phys_preset.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <script/scr_stringlist.h>
#include <universal/physicalmemory.h>
#include <web/web_database_filesystem.h>

#include <zlib/zlib.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> g_file;
std::size_t g_filePosition = 0;
DBRuntimeTraceSnapshot g_trace{};
alignas(4096) std::array<std::uint8_t, 4 * 1024 * 1024> g_arena{};
std::uint32_t g_lowPosition = 0;
std::uint32_t g_highPosition = static_cast<std::uint32_t>(g_arena.size());
std::string g_scriptString;

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

void AppendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void AppendZeros(std::vector<std::uint8_t> &bytes, std::size_t count)
{
    bytes.insert(bytes.end(), count, 0);
}

std::uint32_t Align4(std::uint32_t value)
{
    return (value + 3u) & ~3u;
}

void AppendF32(std::vector<std::uint8_t> &bytes, float value)
{
    std::uint32_t encoded = 0;
    static_assert(sizeof(encoded) == sizeof(value));
    std::memcpy(&encoded, &value, sizeof(encoded));
    AppendU32(bytes, encoded);
}

void AppendCString(std::vector<std::uint8_t> &bytes, const char *value)
{
    bytes.insert(bytes.end(), value, value + std::strlen(value) + 1);
}

std::vector<std::uint8_t> CompressXFile(const std::vector<std::uint8_t> &inflated)
{
    uLongf compressedSize = static_cast<uLongf>(inflated.size() * 2 + 64);
    std::vector<std::uint8_t> compressed(compressedSize);
    assert(compress2(compressed.data(), &compressedSize, inflated.data(),
        static_cast<uLong>(inflated.size()), Z_BEST_COMPRESSION) == Z_OK);
    compressed.resize(compressedSize);
    std::vector<std::uint8_t> result{'I', 'W', 'f', 'f', 'u', '1', '0', '0', 5, 0, 0, 0};
    result.insert(result.end(), compressed.begin(), compressed.end());
    return result;
}

std::vector<std::uint8_t> MakeGeneratedPrefixXFile()
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, "gate3_script_identity");
    AppendU32(inflated, ASSET_TYPE_RAWFILE);
    AppendU32(inflated, UINT32_MAX - 1u);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 5);
    AppendU32(inflated, 1);
    AppendCString(inflated, "tests/gate3_first.txt");
    AppendCString(inflated, "first");
    assert(inflated.size() == 134);
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakePhysPresetXFile(
    std::uint32_t assetPointer = UINT32_MAX - 1u,
    std::uint32_t namePointer = UINT32_MAX,
    std::uint32_t sndAliasPointer = 0x40000015u,
    bool priorAlias = true,
    bool includeName = true,
    bool terminateName = true,
    std::size_t nameLength = 0)
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, priorAlias ? 2u : 1u);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_PHYSPRESET);
    AppendU32(inflated, assetPointer);
    if (priorAlias)
    {
        AppendU32(inflated, ASSET_TYPE_PHYSPRESET);
        AppendU32(inflated, 0x40000011u);
    }
    if (assetPointer == UINT32_MAX || assetPointer == UINT32_MAX - 1u)
    {
        AppendU32(inflated, namePointer);
        AppendU32(inflated, 7);
        AppendF32(inflated, 12.5f);
        AppendF32(inflated, 0.25f);
        AppendF32(inflated, 0.75f);
        AppendF32(inflated, 2.0f);
        AppendF32(inflated, 3.0f);
        AppendU32(inflated, sndAliasPointer);
        AppendF32(inflated, 0.5f);
        AppendF32(inflated, 4.0f);
        AppendU32(inflated, 1);
        if (includeName)
        {
            const char *name = "physics/gate3";
            if (nameLength)
                inflated.insert(inflated.end(), nameLength, 'x');
            else
                inflated.insert(inflated.end(), name, name + std::strlen(name));
            if (terminateName) inflated.push_back(0);
        }
        if (sndAliasPointer == UINT32_MAX)
            AppendCString(inflated, "metal");
    }
    return CompressXFile(inflated);
}

struct TechniqueSetFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    std::uint32_t techniquePointer = UINT32_MAX;
    bool includeAliasAsset = true;
    bool includeTechniqueAlias = true;
    bool terminateTechniqueName = true;
    std::uint16_t passCount = 1;
    std::uint16_t vertexProgramSize = 1;
};

std::vector<std::uint8_t> MakeTechniqueSetXFile(
    const TechniqueSetFixtureOptions &options = {})
{
    constexpr const char *setName = "techsets/gate3";
    constexpr const char *pixelShaderName = "ps_gate3";
    constexpr const char *techniqueName = "tech_gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;

    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_TECHNIQUE_SET);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_TECHNIQUE_SET);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    std::uint32_t block4Offset = assetCount * sizeof(XAsset);
    if (options.assetPointer == UINT32_MAX - 1u)
        block4Offset = Align4(block4Offset) + 4u;
    const std::uint32_t setNameOffset = block4Offset;
    block4Offset += static_cast<std::uint32_t>(std::strlen(setName) + 1u);
    const std::uint32_t techniqueOffset = Align4(block4Offset);
    const std::uint32_t techniqueAlias = 0x40000001u + techniqueOffset;
    const std::uint32_t setNameAlias = 0x40000001u + setNameOffset;

    AppendU32(inflated, UINT32_MAX);
    inflated.push_back(2);
    inflated.push_back(0);
    inflated.push_back(0);
    inflated.push_back(0);
    AppendU32(inflated, 0);
    AppendU32(inflated, options.techniquePointer);
    AppendU32(inflated, options.includeTechniqueAlias &&
        options.techniquePointer == UINT32_MAX ? techniqueAlias : 0u);
    for (std::uint32_t index = 2; index < 34; ++index) AppendU32(inflated, 0);
    AppendCString(inflated, setName);

    if (options.techniquePointer != UINT32_MAX)
        return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU16(inflated, 0x12u);
    AppendU16(inflated, options.passCount);
    if (options.passCount != 1)
        return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    inflated.push_back(1);
    inflated.push_back(0);
    inflated.push_back(0);
    inflated.push_back(0);
    AppendU32(inflated, 1);

    inflated.push_back(1);
    inflated.push_back(0);
    inflated.push_back(0);
    inflated.push_back(0);
    AppendZeros(inflated, 96);

    AppendU32(inflated, setNameAlias);
    AppendU32(inflated, 0);
    AppendU32(inflated, 1);
    AppendU16(inflated, options.vertexProgramSize);
    AppendU16(inflated, 0);
    if (options.vertexProgramSize != 1)
        return CompressXFile(inflated);
    AppendU32(inflated, 0x56530001u);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    AppendU32(inflated, 1);
    AppendU16(inflated, 1);
    AppendU16(inflated, 0);
    AppendCString(inflated, pixelShaderName);
    AppendU32(inflated, 0x50530001u);

    AppendU16(inflated, 1);
    AppendU16(inflated, 3);
    AppendU32(inflated, UINT32_MAX);
    AppendF32(inflated, 1.0f);
    AppendF32(inflated, 2.0f);
    AppendF32(inflated, 3.0f);
    AppendF32(inflated, 4.0f);
    inflated.insert(inflated.end(), techniqueName,
        techniqueName + std::strlen(techniqueName));
    if (options.terminateTechniqueName) inflated.push_back(0);
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakeEmptyXFile(
    const std::array<std::uint32_t, 9> &blocks)
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 4096);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : blocks) AppendU32(inflated, size);
    for (int index = 0; index < 4; ++index) AppendU32(inflated, 0);
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakeScriptListXFile()
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 2);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, "gate3_alpha");
    AppendCString(inflated, "gate3_beta");
    return CompressXFile(inflated);
}

void Reset(const std::vector<std::uint8_t> &file)
{
    g_file = file;
    g_filePosition = 0;
    g_trace = {};
    g_lowPosition = 0;
    g_highPosition = static_cast<std::uint32_t>(g_arena.size());
    std::fill(g_arena.begin(), g_arena.end(), 0);
    g_scriptString.clear();
    std::memset(g_zones, 0, sizeof(g_zones));
    g_zones[1].flags = 1;
    DB_InitAssetPools();
    DB_SetLoadingZoneIndex(1);
}

void RunPrepared(XZoneMemory &zone)
{
    zone = {};
    alignas(16) static std::array<std::uint8_t, 0x80000> inputBuffer{};
    DB_LoadXFile("zone/english/synthetic.ff", reinterpret_cast<void *>(1),
        "synthetic", &zone, nullptr, inputBuffer.data(), 0);
    DB_LoadXFileInternal();
}

void Run(const std::vector<std::uint8_t> &file, XZoneMemory &zone)
{
    Reset(file);
    RunPrepared(zone);
}
} // namespace

WebDatabaseFile WebDatabaseFS_Open(const char *) { return 0; }
std::int64_t WebDatabaseFS_Size(WebDatabaseFile) { return static_cast<std::int64_t>(g_file.size()); }
bool WebDatabaseFS_Seek(WebDatabaseFile, std::uint32_t offset)
{
    if (offset > g_file.size()) return false;
    g_filePosition = offset;
    return true;
}
std::int32_t WebDatabaseFS_Read(WebDatabaseFile, void *destination, std::uint32_t length)
{
    const std::size_t count = std::min<std::size_t>(length, g_file.size() - g_filePosition);
    if (count) std::memcpy(destination, g_file.data() + g_filePosition, count);
    g_filePosition += count;
    return static_cast<std::int32_t>(count);
}
void WebDatabaseFS_Close(WebDatabaseFile) {}

void DB_RuntimeTraceStage(const char *) {}
void DB_RuntimeTraceStop(const char *stage) { g_trace.stopStage = stage; }
void DB_RuntimeTraceHeaderRead(std::uint32_t, std::uint32_t) { g_trace.headerValid = true; }
void DB_RuntimeTraceInputRefill(std::uint32_t bytesRead)
{
    g_trace.bytesRead += bytesRead;
    ++g_trace.inputRefillCount;
}
void DB_RuntimeTraceInflate(std::uint32_t consumed, std::uint32_t produced)
{
    g_trace.compressedBytesConsumed = consumed;
    g_trace.decompressedBytesProduced = produced;
}
void DB_RuntimeTraceInflateInitialized() { g_trace.inflateInitialized = true; }
void DB_RuntimeTraceXFile(std::uint32_t size, std::uint32_t externalSize,
    const std::uint32_t *blockSizes)
{
    g_trace.xfileSize = size;
    g_trace.xfileExternalSize = externalSize;
    std::memcpy(g_trace.blockSizes, blockSizes, sizeof(g_trace.blockSizes));
}
void DB_RuntimeTraceBlockAllocation(std::uint32_t, std::uint32_t size)
{
    ++g_trace.blockAllocationCount;
    g_trace.blockAllocationBytes += size;
}
void DB_RuntimeTraceStreamsInitialized(std::uint32_t block, std::uint32_t offset)
{
    g_trace.streamBlock = block;
    g_trace.streamOffset = offset;
    g_trace.streamInitialized = true;
}
void DB_RuntimeTraceCleanupComplete() { g_trace.cleanupComplete = true; }
void DB_RuntimeTraceXAssetListBegin(std::int32_t strings, std::int32_t assets)
{
    g_trace.xassetListBegin = true;
    g_trace.scriptStringCount = strings >= 0 ? strings : UINT32_MAX;
    g_trace.xassetCount = assets >= 0 ? assets : UINT32_MAX;
}
void DB_RuntimeTraceXAssetListEnd()
{
    g_trace.xassetListEnd = true;
    for (std::uint32_t index = 0; index < 9; ++index)
    {
        const std::uint8_t *position = index == g_streamPosIndex
            ? g_streamPos : g_streamPosArray[index];
        const std::uint8_t *base = g_streamZoneMem->blocks[index].data;
        g_trace.streamOffsets[index] = position && base && position >= base
            ? static_cast<std::uint32_t>(position - base) : 0u;
    }
}
void DB_RuntimeTraceScriptString(std::uint32_t index, const char *identity)
{
    if (index < std::size(g_trace.scriptStringIdentities))
        std::snprintf(g_trace.scriptStringIdentities[index],
            sizeof(g_trace.scriptStringIdentities[index]), "%s", identity ? identity : "");
    g_trace.scriptStringObservedCount = index + 1;
}
void DB_RuntimeTraceAssetBegin(std::uint32_t index, XAssetType type,
    const char *classification)
{
    g_trace.assetIndex = index;
    g_trace.assetType = type;
    std::snprintf(g_trace.pointerClassification,
        sizeof(g_trace.pointerClassification), "%s", classification ? classification : "");
}
void DB_RuntimeTraceAssetLoaded(const char *name)
{
    std::snprintf(g_trace.assetName, sizeof(g_trace.assetName), "%s", name ? name : "");
}
void DB_RuntimeTracePublicationBegin(XAssetType type, const char *name,
    std::size_t freeCount)
{
    g_trace.publicationBegin = true;
    g_trace.assetType = type;
    g_trace.freeEntryCountBefore = static_cast<std::uint32_t>(freeCount);
    std::snprintf(g_trace.assetName, sizeof(g_trace.assetName), "%s", name ? name : "");
}
void DB_RuntimeTracePublicationEnd(XAssetType type, const char *name,
    std::uint32_t entryIndex, std::uint32_t poolIndex, std::size_t freeBefore,
    std::size_t freeAfter, std::uint32_t hash, std::uint32_t zoneIndex)
{
    g_trace.publicationEnd = true;
    g_trace.assetType = type;
    g_trace.assetEntryIndex = entryIndex;
    g_trace.assetPoolIndex = poolIndex;
    g_trace.freeEntryCountBefore = static_cast<std::uint32_t>(freeBefore);
    g_trace.freeEntryCountAfter = static_cast<std::uint32_t>(freeAfter);
    g_trace.assetHash = hash;
    g_trace.assetZoneIndex = zoneIndex;
    std::snprintf(g_trace.assetName, sizeof(g_trace.assetName), "%s", name ? name : "");
}
void DB_RuntimeGeneratedFailure(const char *stage)
{
    if (!g_trace.generatedLoadFailed)
    {
        g_trace.generatedLoadFailed = true;
        DB_FailXFileLoad(stage);
    }
}
bool DB_RuntimeGeneratedLoadFailed()
{
    return g_trace.generatedLoadFailed || DB_HasXFileLoadFailure();
}
bool DB_RuntimeStreamCanRead(std::size_t size)
{
    if (g_streamPosIndex >= 9 || !g_streamZoneMem || !g_streamPos) return false;
    const XBlock &block = g_streamZoneMem->blocks[g_streamPosIndex];
    if (!block.data || g_streamPos < block.data) return size == 0;
    const std::size_t offset = static_cast<std::size_t>(g_streamPos - block.data);
    return offset <= block.size && size <= block.size - offset;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    throw std::runtime_error("canonical DB assertion");
}
void Com_Error(errorParm_t, const char *, ...) { throw std::runtime_error("canonical DB error"); }
void Com_PrintError(int, const char *, ...) {}
void Com_Printf(int, const char *, ...) {}
int I_stricmp(const char *left, const char *right)
{
    while (*left && std::tolower(static_cast<unsigned char>(*left)) ==
        std::tolower(static_cast<unsigned char>(*right)))
    {
        ++left;
        ++right;
    }
    return std::tolower(static_cast<unsigned char>(*left)) -
        std::tolower(static_cast<unsigned char>(*right));
}
void track_static_alloc_internal(void *, int, const char *, int) {}
void Sys_LockWrite(FastCriticalSection *section)
{
    section->writeCount = section->writeCount + 1;
}
void Sys_UnlockWrite(FastCriticalSection *section)
{
    section->writeCount = section->writeCount - 1;
}
std::uint32_t SL_GetString(const char *value, std::uint32_t)
{
    g_scriptString = value ? value : "";
    return g_scriptString.empty() ? 0u : 1u;
}
const char *SL_ConvertToString(std::uint32_t value)
{
    return value == 1 ? g_scriptString.c_str() : "";
}

std::uint8_t *PMem_Alloc(std::uint32_t size, std::uint32_t alignment,
    std::uint32_t, std::uint32_t allocType)
{
    const std::uint32_t mask = alignment - 1;
    if (allocType == 0)
    {
        const std::uint32_t start = (g_lowPosition + mask) & ~mask;
        if (start > g_highPosition || size > g_highPosition - start) return nullptr;
        g_lowPosition = start + size;
        return g_arena.data() + start;
    }
    if (size > g_highPosition) return nullptr;
    const std::uint32_t start = (g_highPosition - size) & ~mask;
    if (start < g_lowPosition) return nullptr;
    g_highPosition = start;
    return g_arena.data() + start;
}
std::uint32_t PMem_GetFreeAmount() { return g_highPosition - g_lowPosition; }
int PMem_GetOverAllocatedSize() { return 0; }
const PhysicalMemory *PMem_GetState()
{
    static PhysicalMemory memory{};
    memory.buf = g_arena.data();
    memory.prim[0].pos = g_lowPosition;
    memory.prim[1].pos = g_highPosition;
    return &memory;
}

int main()
{
    XZoneMemory zone{};
    const std::array<std::uint32_t, 9> prefixBlocks{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0};
    Run(MakeEmptyXFile(prefixBlocks), zone);
    assert(g_trace.decompressedBytesProduced == 60);
    assert(g_trace.xassetListBegin && g_trace.xassetListEnd);
    assert(g_trace.scriptStringCount == 0 && g_trace.xassetCount == 0);
    assert(std::all_of(std::begin(g_trace.streamOffsets),
        std::end(g_trace.streamOffsets), [](std::uint32_t value) { return value == 0; }));
    assert(std::strcmp(g_trace.stopStage, "Load_XAssetHeader/next-family-closure") == 0);

    Run(MakeScriptListXFile(), zone);
    assert(g_trace.scriptStringCount == 2 && g_trace.scriptStringObservedCount == 2);
    assert(std::strcmp(g_trace.scriptStringIdentities[0], "gate3_alpha") == 0);
    assert(std::strcmp(g_trace.scriptStringIdentities[1], "gate3_beta") == 0);
    assert(g_trace.xassetCount == 0 && g_trace.streamOffsets[4] == 31);
    assert(!g_trace.publicationBegin && !g_trace.generatedLoadFailed);

    const std::vector<std::uint8_t> generated = MakeGeneratedPrefixXFile();
    Run(generated, zone);
    assert(g_trace.headerValid && g_trace.inflateInitialized);
    assert(g_trace.decompressedBytesProduced == 134);
    assert(g_trace.xfileSize == 8192 && g_trace.blockAllocationCount == 2);
    assert(g_trace.blockAllocationBytes == 8192 && g_trace.streamInitialized);
    assert(g_trace.xassetListBegin && g_trace.xassetListEnd);
    assert(g_trace.scriptStringCount == 1 && g_trace.scriptStringObservedCount == 1);
    assert(std::strcmp(g_trace.scriptStringIdentities[0], "gate3_script_identity") == 0);
    assert(g_trace.xassetCount == 1 && g_trace.assetIndex == 0);
    assert(g_trace.assetType == ASSET_TYPE_RAWFILE);
    assert(std::strcmp(g_trace.pointerClassification, "inline-insert/-2") == 0);
    assert(std::strcmp(g_trace.assetName, "tests/gate3_first.txt") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 && g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "tests/gate3_first.txt", ASSET_TYPE_RAWFILE));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == 0 && g_trace.streamOffsets[4] == 68);
    assert(g_trace.cleanupComplete && !g_trace.generatedLoadFailed);
    assert(std::strcmp(g_trace.stopStage, "Load_XAssetHeader/next-family-closure") == 0);
    const XAssetHeader published = DB_FindXAssetHeader(
        ASSET_TYPE_RAWFILE, "tests/gate3_first.txt");
    assert(published.rawfile && published.rawfile->len == 5);
    assert(std::strcmp(published.rawfile->buffer, "first") == 0);

    const std::vector<std::uint8_t> physInsertAlias = MakePhysPresetXFile();
    Run(physInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_PHYSPRESET);
    assert(std::strcmp(g_trace.pointerClassification, "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 && g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "physics/gate3", ASSET_TYPE_PHYSPRESET));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == sizeof(PhysPreset));
    assert(g_trace.streamOffsets[4] == 34);
    const XAssetHeader publishedPhys = DB_FindXAssetHeader(
        ASSET_TYPE_PHYSPRESET, "physics/gate3");
    assert(publishedPhys.physPreset);
    assert(publishedPhys.physPreset->type == 7);
    assert(publishedPhys.physPreset->mass == 12.5f);
    assert(publishedPhys.physPreset->tempDefaultToCylinder);
    assert(publishedPhys.physPreset->sndAliasPrefix ==
        publishedPhys.physPreset->name);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_PHYSPRESET) == 63);

    Run(MakePhysPresetXFile(UINT32_MAX, UINT32_MAX, UINT32_MAX,
        false), zone);
    assert(std::strcmp(g_trace.pointerClassification, "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetPoolIndex == 0);
    const XAssetHeader sharedPhys = DB_FindXAssetHeader(
        ASSET_TYPE_PHYSPRESET, "physics/gate3");
    assert(sharedPhys.physPreset);
    assert(std::strcmp(sharedPhys.physPreset->sndAliasPrefix, "metal") == 0);

    const std::vector<std::uint8_t> techniqueInsertAlias =
        MakeTechniqueSetXFile();
    Run(techniqueInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_TECHNIQUE_SET);
    assert(std::strcmp(g_trace.pointerClassification, "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 &&
        g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "techsets/gate3", ASSET_TYPE_TECHNIQUE_SET));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == sizeof(MaterialTechniqueSet));
    assert(g_trace.streamOffsets[4] == 251);
    const XAssetHeader publishedTechniqueSet = DB_FindXAssetHeader(
        ASSET_TYPE_TECHNIQUE_SET, "techsets/gate3");
    assert(publishedTechniqueSet.techniqueSet);
    assert(publishedTechniqueSet.techniqueSet->worldVertFormat == 2);
    assert(publishedTechniqueSet.techniqueSet->remappedTechniqueSet ==
        publishedTechniqueSet.techniqueSet);
    assert(publishedTechniqueSet.techniqueSet->techniques[0]);
    assert(publishedTechniqueSet.techniqueSet->techniques[1] ==
        publishedTechniqueSet.techniqueSet->techniques[0]);
    const MaterialTechnique *publishedTechnique =
        publishedTechniqueSet.techniqueSet->techniques[0];
    assert(publishedTechnique->flags == 0x12u &&
        publishedTechnique->passCount == 1);
    assert(std::strcmp(publishedTechnique->name, "tech_gate3") == 0);
    const MaterialPass &publishedPass = publishedTechnique->passArray[0];
    assert(publishedPass.vertexDecl && publishedPass.vertexDecl->isLoaded);
    assert(std::all_of(std::begin(publishedPass.vertexDecl->routing.decl),
        std::end(publishedPass.vertexDecl->routing.decl),
        [](const void *decl) { return decl == nullptr; }));
    assert(publishedPass.vertexShader && publishedPass.pixelShader);
    assert(publishedPass.vertexShader->name ==
        publishedTechniqueSet.techniqueSet->name);
    assert(publishedPass.vertexShader->prog.vs == nullptr);
    assert(publishedPass.vertexShader->prog.loadDef.program &&
        *static_cast<std::uint32_t *>(
            publishedPass.vertexShader->prog.loadDef.program) == 0x56530001u);
    assert(std::strcmp(publishedPass.pixelShader->name, "ps_gate3") == 0);
    assert(publishedPass.pixelShader->prog.ps == nullptr);
    assert(publishedPass.pixelShader->prog.loadDef.program &&
        *static_cast<std::uint32_t *>(
            publishedPass.pixelShader->prog.loadDef.program) == 0x50530001u);
    assert(publishedPass.args && publishedPass.args[0].type == 1 &&
        publishedPass.args[0].dest == 3);
    assert(publishedPass.args[0].u.literalConst[0] == 1.0f &&
        publishedPass.args[0].u.literalConst[3] == 4.0f);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_TECHNIQUE_SET) == 1023);
    const XAsset *techniqueAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(techniqueAssets[0].header.techniqueSet == publishedTechniqueSet.techniqueSet);
    assert(techniqueAssets[1].header.techniqueSet == publishedTechniqueSet.techniqueSet);

    TechniqueSetFixtureOptions sharedTechniqueOptions{};
    sharedTechniqueOptions.assetPointer = UINT32_MAX;
    sharedTechniqueOptions.includeAliasAsset = false;
    sharedTechniqueOptions.includeTechniqueAlias = false;
    Run(MakeTechniqueSetXFile(sharedTechniqueOptions), zone);
    assert(std::strcmp(g_trace.pointerClassification, "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetPoolIndex == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_TECHNIQUE_SET,
        "techsets/gate3").techniqueSet);

    TechniqueSetFixtureOptions nullTechniqueOptions{};
    nullTechniqueOptions.assetPointer = 0;
    nullTechniqueOptions.includeAliasAsset = false;
    Run(MakeTechniqueSetXFile(nullTechniqueOptions), zone);
    assert(std::strcmp(g_trace.pointerClassification, "null") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_TECHNIQUE_SET) == 1024);

    TechniqueSetFixtureOptions invalidTechniqueAssetOptions{};
    invalidTechniqueAssetOptions.assetPointer = UINT32_MAX - 2u;
    invalidTechniqueAssetOptions.includeAliasAsset = false;
    Run(MakeTechniqueSetXFile(invalidTechniqueAssetOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    TechniqueSetFixtureOptions invalidTechniqueChildOptions{};
    invalidTechniqueChildOptions.techniquePointer = UINT32_MAX - 1u;
    invalidTechniqueChildOptions.includeAliasAsset = false;
    invalidTechniqueChildOptions.includeTechniqueAlias = false;
    Run(MakeTechniqueSetXFile(invalidTechniqueChildOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid pointer offset") == 0);

    TechniqueSetFixtureOptions excessivePassOptions{};
    excessivePassOptions.includeAliasAsset = false;
    excessivePassOptions.includeTechniqueAlias = false;
    excessivePassOptions.passCount = 1000;
    Run(MakeTechniqueSetXFile(excessivePassOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "MaterialTechnique/pass array") == 0);

    TechniqueSetFixtureOptions excessiveProgramOptions{};
    excessiveProgramOptions.includeAliasAsset = false;
    excessiveProgramOptions.includeTechniqueAlias = false;
    excessiveProgramOptions.vertexProgramSize = 1024;
    Run(MakeTechniqueSetXFile(excessiveProgramOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "MaterialShader/program array") == 0);

    TechniqueSetFixtureOptions truncatedTechniqueOptions{};
    truncatedTechniqueOptions.includeAliasAsset = false;
    truncatedTechniqueOptions.includeTechniqueAlias = false;
    truncatedTechniqueOptions.terminateTechniqueName = false;
    Run(MakeTechniqueSetXFile(truncatedTechniqueOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    Run(MakePhysPresetXFile(0, 0, 0, false, false), zone);
    assert(std::strcmp(g_trace.pointerClassification, "null") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_PHYSPRESET) == 64);

    Run(MakePhysPresetXFile(UINT32_MAX - 2u, 0, 0, false, false), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    Run(MakePhysPresetXFile(UINT32_MAX - 1u, UINT32_MAX, 0x4000000du,
        false, true, false), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    Run(MakePhysPresetXFile(UINT32_MAX - 1u, UINT32_MAX, 0x4000000du,
        false, true, false, 4084), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    Reset(physInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_PHYSPRESET]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(
        ASSET_TYPE_PHYSPRESET, "physics/gate3") == nullptr);

    Reset(techniqueInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_TECHNIQUE_SET]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_TECHNIQUE_SET,
        "techsets/gate3") == nullptr);
    std::uint32_t failedInsertion = UINT32_MAX;
    std::memcpy(&failedInsertion, zone.blocks[4].data + 16,
        sizeof(failedInsertion));
    assert(failedInsertion == 0);

    Reset(techniqueInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_TECHNIQUE_SET,
        "techsets/gate3") == nullptr);
    std::memcpy(&failedInsertion, zone.blocks[4].data + 16,
        sizeof(failedInsertion));
    assert(failedInsertion == 0);

    Reset(physInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset entry exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(
        ASSET_TYPE_PHYSPRESET, "physics/gate3") == nullptr);

    Reset(generated);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_RAWFILE]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_RAWFILE,
        "tests/gate3_first.txt") == nullptr);

    Reset(generated);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset entry exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_RAWFILE,
        "tests/gate3_first.txt") == nullptr);

    const std::array<std::uint32_t, 9> oversized{0x08000001u, 0, 0, 0, 0, 0, 0, 0, 0};
    Run(MakeEmptyXFile(oversized), zone);
    assert(std::strcmp(g_trace.stopStage, "XFile/block allocation exhaustion") == 0);
    assert(g_trace.blockAllocationCount == 0 && !g_trace.streamInitialized);

    const std::array<std::uint32_t, 9> overflowing{0xfffffff8u, 0, 0, 0, 0, 0, 0, 0, 0};
    Run(MakeEmptyXFile(overflowing), zone);
    assert(std::strcmp(g_trace.stopStage, "XFile/block allocation exhaustion") == 0);
    assert(g_trace.blockAllocationCount == 0 && !g_trace.streamInitialized);

    Run(generated, zone);
    assert(g_trace.publicationEnd && g_trace.cleanupComplete);
    assert(std::strcmp(g_trace.stopStage, "Load_XAssetHeader/next-family-closure") == 0);

    std::printf("gate3-db-stream rawfile=published physpreset=published technique-set=published insert=-2 alias=block4:16 technique=block4:36 direct-xstring=block4:20 material-children=251 entry=16 pool=0 free=32752->32751 zone=1 stop=next-family-closure\n");
    return 0;
}
