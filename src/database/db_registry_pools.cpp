#include <database/db_registry_pools.h>

#include <qcommon/mem_track.h>
#include <universal/q_shared.h>


#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

static_assert(sizeof(void *) == 4,
    "The canonical IW3 registry pool storage requires the 32-bit engine ABI");

namespace
{
template <std::size_t Stride, std::size_t Count>
struct CanonicalPoolStorage
{
    void *freeHead;
    alignas(4) std::array<std::byte, Stride * Count> entries;
};

constexpr std::array<std::uint32_t, ASSET_TYPE_COUNT> g_poolStride = {
    12, 44, 88, 220, 80, 148, 36, 12, 72, 44,
    0, 0, 0, 0, 0, 12, 0, 16, 0, 24, 12, 284, 8, 2168,
    0, 32, 8, 0, 0, 0, 0, 12, 16,
};

CanonicalPoolStorage<12, 64> g_XModelPiecesPool{};
CanonicalPoolStorage<44, 64> g_PhysPresetPool{};
CanonicalPoolStorage<88, 4096> g_XAnimPartsPool{};
CanonicalPoolStorage<220, 1000> g_XModelPool{};
CanonicalPoolStorage<80, 2048> g_MaterialPool{};
CanonicalPoolStorage<148, 1024> g_MaterialTechniqueSetPool{};
CanonicalPoolStorage<36, 2400> g_GfxImagePool{};
CanonicalPoolStorage<12, 16000> g_SoundPool{};
CanonicalPoolStorage<72, 64> g_SndCurvePool{};
CanonicalPoolStorage<44, 1200> g_LoadedSoundPool{};
CanonicalPoolStorage<12, 2> g_MapEntsPool{};
CanonicalPoolStorage<16, 32> g_GfxLightDefPool{};
CanonicalPoolStorage<24, 16> g_FontPool{};
CanonicalPoolStorage<12, 128> g_MenuListPool{};
CanonicalPoolStorage<284, 640> g_MenuPool{};
CanonicalPoolStorage<8, 6144> g_LocalizeEntryPool{};
CanonicalPoolStorage<2168, 128> g_WeaponDefPool{};
CanonicalPoolStorage<32, 400> g_FxEffectDefPool{};
CanonicalPoolStorage<8, 4> g_FxImpactTablePool{};
CanonicalPoolStorage<12, 1024> g_RawFilePool{};
CanonicalPoolStorage<16, 50> g_StringTablePool{};

#define ASSERT_POOL_LAYOUT(pool, stride, count) \
    static_assert(sizeof(pool) == sizeof(void *) + (stride) * (count)); \
    static_assert(alignof(decltype(pool)) == alignof(void *))
ASSERT_POOL_LAYOUT(g_XModelPiecesPool, 12, 64);
ASSERT_POOL_LAYOUT(g_PhysPresetPool, 44, 64);
ASSERT_POOL_LAYOUT(g_XAnimPartsPool, 88, 4096);
ASSERT_POOL_LAYOUT(g_XModelPool, 220, 1000);
ASSERT_POOL_LAYOUT(g_MaterialPool, 80, 2048);
ASSERT_POOL_LAYOUT(g_MaterialTechniqueSetPool, 148, 1024);
ASSERT_POOL_LAYOUT(g_GfxImagePool, 36, 2400);
ASSERT_POOL_LAYOUT(g_SoundPool, 12, 16000);
ASSERT_POOL_LAYOUT(g_SndCurvePool, 72, 64);
ASSERT_POOL_LAYOUT(g_LoadedSoundPool, 44, 1200);
ASSERT_POOL_LAYOUT(g_MapEntsPool, 12, 2);
ASSERT_POOL_LAYOUT(g_GfxLightDefPool, 16, 32);
ASSERT_POOL_LAYOUT(g_FontPool, 24, 16);
ASSERT_POOL_LAYOUT(g_MenuListPool, 12, 128);
ASSERT_POOL_LAYOUT(g_MenuPool, 284, 640);
ASSERT_POOL_LAYOUT(g_LocalizeEntryPool, 8, 6144);
ASSERT_POOL_LAYOUT(g_WeaponDefPool, 2168, 128);
ASSERT_POOL_LAYOUT(g_FxEffectDefPool, 32, 400);
ASSERT_POOL_LAYOUT(g_FxImpactTablePool, 8, 4);
ASSERT_POOL_LAYOUT(g_RawFilePool, 12, 1024);
ASSERT_POOL_LAYOUT(g_StringTablePool, 16, 50);
#undef ASSERT_POOL_LAYOUT

// DB_InitSingleton only validates a count of one. These stable identities are
// replaced by their canonical subsystem-owned object bodies as those TUs join
// the Web target; this prefix never decodes or publishes singleton assets.
alignas(4) std::array<std::byte, 4> g_clipMapIdentity{};
alignas(4) std::array<std::byte, 4> g_comWorldIdentity{};
alignas(4) std::array<std::byte, 4> g_gameWorldSpIdentity{};
alignas(4) std::array<std::byte, 4> g_gfxWorldIdentity{};

bool g_assetPoolsInitialized = false;

void InitPool(void *storage, std::uint32_t stride, std::int32_t count)
{
    iassert(storage && stride >= sizeof(void *) && count > 0);
    auto *base = static_cast<std::byte *>(storage) + sizeof(void *);
    *static_cast<void **>(storage) = base;
    for (std::int32_t index = 0; index < count; ++index)
    {
        void *next = index + 1 < count ? base + stride * (index + 1) : nullptr;
        std::memcpy(base + stride * index, &next, sizeof(next));
    }
}
} // namespace

#if !defined(KISAK_WEB) && !defined(KISAK_DB_POOL_STANDALONE)
extern clipMap_t cm;
extern ComWorld comWorld;
extern GfxWorld s_world;
#if defined(KISAK_MP)
extern GameWorldMp gameWorldMp;
#else
extern GameWorldSp gameWorldSp;
#endif
#endif

int32_t g_poolSize[ASSET_TYPE_COUNT] = {
    64, 64, 4096, 1000, 2048, 1024, 2400, 16000, 64, 1200,
    1, 1, 1, 1, 1, 2, 1, 32, 0, 16, 128, 640, 6144, 128,
    1, 400, 4, 0, 0, 0, 0, 1024, 50,
};

void *DB_XAssetPool[ASSET_TYPE_COUNT] = {
    &g_XModelPiecesPool, &g_PhysPresetPool, &g_XAnimPartsPool, &g_XModelPool,
    &g_MaterialPool, &g_MaterialTechniqueSetPool, &g_GfxImagePool,
    &g_SoundPool, &g_SndCurvePool, &g_LoadedSoundPool,
 #if defined(KISAK_WEB) || defined(KISAK_DB_POOL_STANDALONE)
    g_clipMapIdentity.data(), g_clipMapIdentity.data(), g_comWorldIdentity.data(),
#if defined(KISAK_MP)
    nullptr, g_gameWorldSpIdentity.data(),
#else
    g_gameWorldSpIdentity.data(), nullptr,
#endif
    &g_MapEntsPool, g_gfxWorldIdentity.data(), &g_GfxLightDefPool, nullptr,
 #else
    &cm, &cm, &comWorld,
 #if defined(KISAK_MP)
    nullptr, &gameWorldMp,
 #else
    &gameWorldSp, nullptr,
 #endif
    &g_MapEntsPool, &s_world, &g_GfxLightDefPool, nullptr,
 #endif
    &g_FontPool, &g_MenuListPool, &g_MenuPool, &g_LocalizeEntryPool,
    &g_WeaponDefPool, nullptr, &g_FxEffectDefPool, &g_FxImpactTablePool,
    nullptr, nullptr, nullptr, nullptr, &g_RawFilePool, &g_StringTablePool,
};

XAssetEntryPoolEntry g_assetEntryPool[32768];
XAssetEntryPoolEntry *g_freeAssetEntryHead = nullptr;
uint16_t db_hashTable[32768];

void DB_InitAssetPools()
{
    std::fill(std::begin(db_hashTable), std::end(db_hashTable), uint16_t{0});
    for (std::int32_t type = 0; type < ASSET_TYPE_COUNT; ++type)
    {
        if (!DB_XAssetPool[type]) continue;
        if (g_poolStride[type]) InitPool(DB_XAssetPool[type], g_poolStride[type], g_poolSize[type]);
        else iassert(g_poolSize[type] == 1);
    }

    g_freeAssetEntryHead = g_assetEntryPool + 16;
    for (std::int32_t index = 1; index < 0x7fff; ++index)
        g_assetEntryPool[index].next = &g_assetEntryPool[index + 1];
    g_assetEntryPool[0x7fff].next = nullptr;
    g_assetPoolsInitialized = true;
}

void DB_TrackAssetPools()
{
#define TRACK_POOL(pool) track_static_alloc_internal(&(pool), sizeof(pool), #pool, 10)
    TRACK_POOL(g_XModelPiecesPool); TRACK_POOL(g_PhysPresetPool);
    TRACK_POOL(g_XAnimPartsPool); TRACK_POOL(g_XModelPool);
    TRACK_POOL(g_MaterialPool); TRACK_POOL(g_MaterialTechniqueSetPool);
    TRACK_POOL(g_GfxImagePool); TRACK_POOL(g_SoundPool);
    TRACK_POOL(g_SndCurvePool); TRACK_POOL(g_LoadedSoundPool);
    TRACK_POOL(g_MapEntsPool); TRACK_POOL(g_GfxLightDefPool);
    TRACK_POOL(g_FontPool); TRACK_POOL(g_MenuListPool); TRACK_POOL(g_MenuPool);
    TRACK_POOL(g_LocalizeEntryPool); TRACK_POOL(g_WeaponDefPool);
    TRACK_POOL(g_FxEffectDefPool); TRACK_POOL(g_FxImpactTablePool);
    TRACK_POOL(g_RawFilePool); TRACK_POOL(g_StringTablePool);
#undef TRACK_POOL
    track_static_alloc_internal(g_assetEntryPool, sizeof(g_assetEntryPool), "g_assetEntryPool", 10);
}

bool DB_AreAssetPoolsInitialized() { return g_assetPoolsInitialized; }

std::size_t DB_GetInitializedAssetPoolCount()
{
    return static_cast<std::size_t>(std::count_if(
        std::begin(DB_XAssetPool), std::end(DB_XAssetPool),
        [](const void *pool) { return pool != nullptr; }));
}

std::size_t DB_GetFreeAssetEntryCount()
{
    std::size_t count = 0;
    for (XAssetEntryPoolEntry *entry = g_freeAssetEntryHead; entry; entry = entry->next) ++count;
    return count;
}
