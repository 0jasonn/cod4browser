#include <database/database.h>
#include <database/db_runtime_prefix.h>
#include <qcommon/qcommon.h>
#include <script/scr_stringlist.h>
#include <universal/physicalmemory.h>
#include <web/web_database_filesystem.h>

#include <zlib/zlib.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdarg>
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

void Reset(const std::vector<std::uint8_t> &file)
{
    g_file = file;
    g_filePosition = 0;
    g_trace = {};
    g_lowPosition = 0;
    g_highPosition = static_cast<std::uint32_t>(g_arena.size());
}

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

std::vector<std::uint8_t> MakeXFile(const std::array<std::uint32_t, 9> &blocks)
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 4096);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : blocks) AppendU32(inflated, size);
    uLongf compressedSize = static_cast<uLongf>(inflated.size() * 2 + 64);
    std::vector<std::uint8_t> compressed(compressedSize);
    assert(compress2(compressed.data(), &compressedSize, inflated.data(),
        static_cast<uLong>(inflated.size()), Z_BEST_COMPRESSION) == Z_OK);
    compressed.resize(compressedSize);
    std::vector<std::uint8_t> result{'I', 'W', 'f', 'f', 'u', '1', '0', '0', 5, 0, 0, 0};
    result.insert(result.end(), compressed.begin(), compressed.end());
    return result;
}

void Run(const std::vector<std::uint8_t> &file, XZoneMemory &zone)
{
    Reset(file);
    zone = {};
    alignas(16) static std::array<std::uint8_t, 0x80000> inputBuffer{};
    DB_LoadXFile("zone/english/synthetic.ff", reinterpret_cast<void *>(1),
        "synthetic", &zone, nullptr, inputBuffer.data(), 0);
    DB_LoadXFileInternal();
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

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    throw std::runtime_error("canonical DB assertion");
}
void Com_Error(errorParm_t, const char *, ...) { throw std::runtime_error("canonical DB error"); }
void Com_PrintError(int, const char *, ...) {}
void Com_Printf(int, const char *, ...) {}
std::uint32_t SL_GetString(const char *, std::uint32_t) { return 0; }

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
    const std::array<std::uint32_t, 9> blocks{1024, 0, 0, 0, 2048, 0, 0, 64, 32};
    Run(MakeXFile(blocks), zone);
    assert(g_trace.headerValid);
    assert(g_trace.inflateInitialized);
    assert(g_trace.decompressedBytesProduced == sizeof(XFile));
    assert(g_trace.compressedBytesConsumed > 0);
    assert(g_trace.xfileSize == 4096);
    assert(std::memcmp(g_trace.blockSizes, blocks.data(), sizeof(blocks)) == 0);
    assert(g_trace.blockAllocationCount == 4);
    assert(g_trace.blockAllocationBytes == 3168);
    assert(g_trace.streamInitialized && g_trace.streamBlock == 0 && g_trace.streamOffset == 0);
    assert(g_trace.cleanupComplete);
    assert(std::strcmp(g_trace.stopStage,
        "Load_XAssetListCustom/generated-loader-closure") == 0);
    assert(g_streamZoneMem == &zone);
    assert(g_streamPos == zone.blocks[0].data);

    const std::array<std::uint32_t, 9> oversized{0x08000001u, 0, 0, 0, 0, 0, 0, 0, 0};
    Run(MakeXFile(oversized), zone);
    assert(std::strcmp(g_trace.stopStage, "XFile/block allocation exhaustion") == 0);
    assert(g_trace.blockAllocationCount == 0);
    assert(!g_trace.streamInitialized && g_trace.cleanupComplete);

    const std::array<std::uint32_t, 9> overflowing{0xfffffff8u, 0, 0, 0, 0, 0, 0, 0, 0};
    Run(MakeXFile(overflowing), zone);
    assert(std::strcmp(g_trace.stopStage, "XFile/block allocation exhaustion") == 0);
    assert(g_trace.blockAllocationCount == 0);
    assert(!g_trace.streamInitialized && g_trace.cleanupComplete);

    // A failed request must leave the global load descriptor reusable by the
    // next canonical request rather than retaining EOF/inflate state.
    Run(MakeXFile(blocks), zone);
    assert(std::strcmp(g_trace.stopStage,
        "Load_XAssetListCustom/generated-loader-closure") == 0);
    assert(g_trace.decompressedBytesProduced == sizeof(XFile));
    assert(g_trace.blockAllocationCount == 4);
    assert(g_trace.streamInitialized && g_trace.cleanupComplete);

    std::printf("gate3-db-stream produced=44 blocks=1024,0,0,0,2048,0,0,64,32 allocations=4 stop=generated-loader-closure\n");
    return 0;
}
