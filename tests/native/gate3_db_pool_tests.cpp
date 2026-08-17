#include <database/db_registry_pools.h>

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

void MyAssertHandler(const char *filename, int line, int, const char *format, ...)
{
    std::fprintf(stderr, "assert:%s:%d:", filename, line);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::abort();
}

void track_static_alloc_internal(void *, int, const char *, int) {}

int main()
{
    DB_InitAssetPools();
    assert(DB_AreAssetPoolsInitialized());
    assert(DB_GetInitializedAssetPoolCount() == 26);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    assert(g_freeAssetEntryHead == g_assetEntryPool + 16);
    assert(g_assetEntryPool[0x7fff].next == nullptr);

    std::uint32_t semanticHash = 2166136261u;
    for (int type = 0; type < ASSET_TYPE_COUNT; ++type)
    {
        semanticHash ^= static_cast<std::uint32_t>(type);
        semanticHash *= 16777619u;
        semanticHash ^= static_cast<std::uint32_t>(g_poolSize[type]);
        semanticHash *= 16777619u;
        semanticHash ^= DB_XAssetPool[type] ? 1u : 0u;
        semanticHash *= 16777619u;
    }
    std::printf("gate3-db-pools pools=%zu free=%zu hash=%08x\n",
        DB_GetInitializedAssetPoolCount(), DB_GetFreeAssetEntryCount(), semanticHash);
    return 0;
}

