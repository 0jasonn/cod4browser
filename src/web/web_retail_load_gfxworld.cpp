#include <web/web_retail_load_gfxworld.h>
#include <web/web_retail_load_image.h>

#include <database/db_asset_types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace kisak::fastfile
{
namespace
{
constexpr std::uint32_t INLINE_POINTER = UINT32_MAX;
constexpr std::uint32_t SHARED_POINTER = UINT32_MAX - 1u;
constexpr std::uint32_t WORLD_BYTES = 732u;

std::uint16_t ReadU16(const std::uint8_t *p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | p[1] << 8u);
}
std::uint32_t ReadU32(const std::uint8_t *p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) |
        static_cast<std::uint32_t>(p[1]) << 8u |
        static_cast<std::uint32_t>(p[2]) << 16u |
        static_cast<std::uint32_t>(p[3]) << 24u;
}
std::int32_t ReadS32(const std::uint8_t *p) noexcept
{
    return std::bit_cast<std::int32_t>(ReadU32(p));
}
float ReadF32(const std::uint8_t *p) noexcept
{
    return std::bit_cast<float>(ReadU32(p));
}

RetailCensusError RegistryError(ZoneRegistryError error) noexcept
{
    if (error == ZoneRegistryError::None) return RetailCensusError::None;
    return error == ZoneRegistryError::AllocationFailed
        ? RetailCensusError::AllocationFailed
        : RetailCensusError::AssetRegistryInvalid;
}

bool CheckedTableAliasOffset(
    const RetailFastfileCensus &result,
    std::uint32_t assetIndex,
    std::uint32_t &offset) noexcept
{
    const std::uint64_t value = static_cast<std::uint64_t>(
        result.assetTableBlock4Offset) + static_cast<std::uint64_t>(assetIndex) * 8u + 4u;
    if (value > UINT32_MAX) return false;
    offset = static_cast<std::uint32_t>(value);
    return true;
}

bool CheckedBytes(
    std::uint64_t count,
    std::uint64_t stride,
    std::uint32_t elementLimit,
    std::uint32_t &bytes) noexcept
{
    if (count > elementLimit || count > UINT32_MAX ||
        (count != 0u && stride > UINT32_MAX / count))
        return false;
    bytes = static_cast<std::uint32_t>(count * stride);
    return true;
}

template <typename T>
T *AllocateOwned(
    struct CanonicalGfxWorldStorage &storage,
    std::uint32_t count) noexcept;
}

struct CanonicalGfxWorldStorage
{
    struct WireAllocation
    {
        ZoneSpan span{};
        std::uint32_t wireStride = 0u;
        std::uint32_t canonicalStride = 0u;
        std::uint8_t *canonical = nullptr;
    };
    std::shared_ptr<std::string> name;
    std::shared_ptr<std::string> baseName;
    std::vector<std::shared_ptr<void>> blocks;
    std::vector<WireAllocation> wireAllocations;
};

struct CanonicalMaterialTextureDef
{
    std::uint32_t nameHash = 0u;
    char nameStart = 0;
    char nameEnd = 0;
    std::uint8_t samplerState = 0u;
    std::uint8_t semantic = 0u;
    void *info = nullptr;
};

struct CanonicalWater
{
    float floatTime = 0.0f;
    float (*H0)[2] = nullptr;
    float *wTerm = nullptr;
    int M = 0, N = 0;
    float Lx = 0.0f, Lz = 0.0f, gravity = 0.0f, windvel = 0.0f;
    float winddir[2]{}, amplitude = 0.0f, codeConstant[4]{};
    GfxImage *image = nullptr;
};

struct CanonicalMaterialConstantDef
{
    std::uint32_t nameHash = 0u;
    char name[12]{};
    float literal[4]{};
};

struct CanonicalGfxStateBits
{
    std::uint32_t loadBits[2]{};
};

namespace
{
template <typename T>
T *AllocateOwned(CanonicalGfxWorldStorage &storage, std::uint32_t count) noexcept
{
    const std::size_t allocationCount = std::max<std::uint32_t>(count, 1u);
    std::shared_ptr<T[]> block(new (std::nothrow) T[allocationCount]{});
    if (!block) return nullptr;
    T *pointer = block.get();
    try { storage.blocks.emplace_back(block, pointer); }
    catch (...) { return nullptr; }
    return pointer;
}

bool RememberWireAllocation(
    CanonicalGfxWorldStorage &storage,
    const ZoneSpan &span,
    std::uint32_t wireStride,
    std::uint32_t canonicalStride,
    void *canonical) noexcept
{
    if (span.length == 0u || !canonical) return true;
    try
    {
        storage.wireAllocations.push_back({
            span, wireStride, canonicalStride,
            static_cast<std::uint8_t *>(canonical)});
    }
    catch (...) { return false; }
    return true;
}

void *ResolveWirePointer(
    RetailLoadContext &context,
    CanonicalGfxWorldStorage &storage,
    std::uint32_t token,
    std::uint32_t alignment) noexcept
{
    if (token == 0u || token == INLINE_POINTER || token == SHARED_POINTER)
        return nullptr;
    ZoneSpan target;
    if (!context.TranslatePointerToken(token, alignment, target)) return nullptr;
    const std::uint32_t block = target.block;
    const std::uint32_t offset = target.offset;
    for (const auto &allocation : storage.wireAllocations)
    {
        if (allocation.span.block != block ||
            offset < allocation.span.offset ||
            offset >= allocation.span.offset + allocation.span.length)
            continue;
        const std::uint32_t delta = offset - allocation.span.offset;
        if (allocation.wireStride == 0u ||
            delta % allocation.wireStride != 0u)
            return nullptr;
        return allocation.canonical +
            (delta / allocation.wireStride) * allocation.canonicalStride;
    }
    return nullptr;
}

template <typename T>
bool ResolveAsset(
    RetailLoadContext &context,
    std::uint32_t token,
    std::uint32_t type,
    T *&output,
    std::uint32_t *resolvedIdentity = nullptr) noexcept
{
    output = nullptr;
    if (resolvedIdentity) *resolvedIdentity = 0u;
    if (token == 0u) return true;
    if (token == INLINE_POINTER || token == SHARED_POINTER) return false;
    std::uint32_t identity = 0u;
    if (context.ResolveAssetAlias(token, type, identity) != ZoneRegistryError::None)
        return false;
    output = static_cast<T *>(context.FindCanonicalAsset(type, identity));
    if (output && resolvedIdentity) *resolvedIdentity = identity;
    return output != nullptr;
}

void DecodeVertex(const std::uint8_t *p, GfxWorldVertex &v) noexcept
{
    for (std::size_t i = 0; i < 3; ++i) v.xyz[i] = ReadF32(p + i * 4u);
    v.binormalSign = ReadF32(p + 12u);
    v.color.packed = ReadU32(p + 16u);
    v.texCoord[0] = ReadF32(p + 20u); v.texCoord[1] = ReadF32(p + 24u);
    v.lmapCoord[0] = ReadF32(p + 28u); v.lmapCoord[1] = ReadF32(p + 32u);
    v.normal.packed = ReadU32(p + 36u); v.tangent.packed = ReadU32(p + 40u);
}

void DecodeSurface(const std::uint8_t *p, GfxSurface &s) noexcept
{
    s = {};
    s.tris.vertexLayerData = ReadS32(p);
    s.tris.firstVertex = ReadS32(p + 4u);
    s.tris.vertexCount = ReadU16(p + 8u);
    s.tris.triCount = ReadU16(p + 10u);
    s.tris.baseIndex = ReadS32(p + 12u);
    s.lightmapIndex = p[20u]; s.reflectionProbeIndex = p[21u];
    s.primaryLightIndex = p[22u]; s.flags = p[23u];
    for (std::size_t i = 0; i < 6; ++i)
        s.bounds[i / 3u][i % 3u] = ReadF32(p + 24u + i * 4u);
}

void DecodeAabb(const std::uint8_t *p, GfxAabbTree &v) noexcept
{
    v = {};
    for (std::size_t n = 0; n < 3; ++n)
    {
        v.mins[n] = ReadF32(p + n * 4u);
        v.maxs[n] = ReadF32(p + 12u + n * 4u);
    }
    v.childCount = ReadU16(p + 24u);
    v.surfaceCount = ReadU16(p + 26u);
    v.startSurfIndex = ReadU16(p + 28u);
    v.surfaceCountNoDecal = ReadU16(p + 30u);
    v.startSurfIndexNoDecal = ReadU16(p + 32u);
    v.smodelIndexCount = ReadU16(p + 34u);
    v.childrenOffset = ReadS32(p + 40u);
}

void DecodePortal(const std::uint8_t *p, GfxPortal &v) noexcept
{
    v = {};
    v.writable.isQueued = p[0u] != 0u;
    v.writable.isAncestor = p[1u] != 0u;
    v.writable.recursionDepth = p[2u];
    v.writable.hullPointCount = p[3u];
    for (std::size_t n = 0; n < 4; ++n)
        v.plane.coeffs[n] = ReadF32(p + 12u + n * 4u);
    std::memcpy(v.plane.side, p + 28u, 3u);
    v.plane.pad = p[31u];
    v.vertexCount = p[40u];
    for (std::size_t a = 0; a < 2; ++a)
        for (std::size_t n = 0; n < 3; ++n)
            v.hullAxis[a][n] = ReadF32(p + 44u + (a * 3u + n) * 4u);
}

void DecodeBrushModel(const std::uint8_t *p, GfxBrushModel &v) noexcept
{
    v = {};
    for (std::size_t n = 0; n < 3; ++n)
    {
        v.writable.mins[n] = ReadF32(p + n * 4u);
        v.writable.maxs[n] = ReadF32(p + 12u + n * 4u);
        v.bounds[0][n] = ReadF32(p + 24u + n * 4u);
        v.bounds[1][n] = ReadF32(p + 36u + n * 4u);
    }
    v.surfaceCount = ReadU16(p + 48u);
    v.startSurfIndex = ReadU16(p + 50u);
    v.surfaceCountNoDecal = ReadU16(p + 52u);
}

void DecodeDrawInst(const std::uint8_t *p, GfxStaticModelDrawInst &v) noexcept
{
    v = {};
    v.cullDist = ReadF32(p);
    for (std::size_t n = 0; n < 3; ++n)
        v.placement.origin[n] = ReadF32(p + 4u + n * 4u);
    for (std::size_t n = 0; n < 9; ++n)
        v.placement.axis[n / 3u][n % 3u] = ReadF32(p + 16u + n * 4u);
    v.placement.scale = ReadF32(p + 52u);
    for (std::size_t n = 0; n < 4; ++n)
        v.smodelCacheIndex[n] = ReadU16(p + 60u + n * 2u);
    v.reflectionProbeIndex = p[68u];
    v.primaryLightIndex = p[69u];
    v.lightingHandle = ReadU16(p + 70u);
    v.flags = p[72u];
}
}

struct RetailGfxWorldLoadFamily::State
{
    enum class MaterialPhase : std::uint8_t
    {
        Idle, Header, Name, Textures, TextureDependencies,
        Constants, StateBits, Publish, Complete,
    };
    enum class Phase : std::uint8_t
    {
        Root, Name, BaseName, Indices, SkySurfaces, SkyImage, SunLight,
        ReflectionProbes, ReflectionRuntime, Planes, Nodes, SceneEntBits,
        Cells, CellAabbs, CellAabbIndexes, CellPortals, CellPortalVertices,
        CellCullGroups, CellReflectionProbes, Lightmaps, LightGridRows,
        LightGridRaw, LightGridEntries, LightGridColors, LightmapPrimaryRuntime,
        LightmapSecondaryRuntime, Models, MaterialMemory, Vertices, VertexLayers,
        Sunflare, OutdoorImage, CellCasterBits, SceneDynModels, SceneDynBrushes,
        PrimaryEntityShadow, PrimaryDynModelShadow, PrimaryDynBrushShadow,
        NonSunPrimary, ShadowGeometry, ShadowSurfaceIndexes, ShadowModelIndexes,
        LightRegions, LightRegionHulls, LightRegionAxes, StaticSmodelVis,
        StaticSurfaceVis, StaticLod, StaticSortedSurfaces, StaticModelInsts,
        StaticSurfaces, StaticCullGroups, StaticDrawInsts, StaticSurfaceMaterials,
        StaticSunShadow, DynamicCellBits, DynamicVis, Publish,
    };

    RetailGfxWorldLoadProgress progress = RetailGfxWorldLoadProgress::Idle;
    Phase phase = Phase::Root;
    std::size_t resultIndex = 0u;
    ZoneSpan tableAlias{}, insertAlias{}, headerSpan{}, plannedSpan{};
    ZoneLoadKind plannedKind = ZoneLoadKind::Immediate;
    std::array<std::uint8_t, WORLD_BYTES> root{};
    std::uint32_t plannedBytes = 0u;
    bool planned = false, block4Pushed = false, hasInsertAlias = false;
    std::size_t i = 0u, j = 0u, k = 0u;
    std::vector<std::array<std::uint32_t, 4>> cellTokens;
    std::vector<std::vector<std::uint32_t>> aabbTokens;
    std::vector<std::vector<std::array<std::uint32_t, 2>>> portalTokens;
    std::vector<std::array<std::uint32_t, 2>> lightmapTokens;
    std::vector<std::uint32_t> reflectionTokens, materialTokens;
    std::vector<std::array<std::uint32_t, 2>> shadowTokens;
    std::vector<std::uint32_t> regionTokens;
    std::vector<std::vector<std::uint32_t>> hullTokens;
    std::vector<std::uint32_t> surfaceMaterialTokens, drawModelTokens;
    RetailImageLoadFamily imageLoader;
    bool dependencyStarted = false;
    bool recordsLoaded = false;
    ZoneSpan childSpan{};
    MaterialPhase materialPhase = MaterialPhase::Idle;
    std::size_t materialResultIndex = 0u;
    ZoneSpan materialPointerCell{}, materialInsertCell{}, materialHeaderSpan{}, materialTextureSpan{};
    std::array<std::uint8_t,80> materialHeader{};
    std::vector<std::uint32_t> materialImageTokens;
    std::uint32_t materialSerialized = 0u;
    std::uint32_t materialNameOffset = UINT32_MAX;
    std::size_t materialTextureIndex = 0u;
    bool materialHasInsert = false, materialBlock0 = false, materialBlock4 = false;
    std::uint8_t waterPhase = 0u;
    CanonicalWater *water = nullptr;
    std::array<std::uint8_t,68> waterHeader{};
    ZoneSpan waterSpan{};
    bool xmodelDependencyStarted = false;
};

namespace
{
RetailPublishedGfxWorld &Entry(
    RetailLoadContext &context,
    const RetailGfxWorldLoadFamily::State &state) noexcept
{
    return context.Ownership().worldGfxWorlds[state.resultIndex];
}

RetailCensusError AddPayload(
    RetailLoadContext &context,
    RetailPublishedGfxWorld &entry,
    std::uint32_t bytes) noexcept
{
    const std::uint64_t total = static_cast<std::uint64_t>(entry.payloadBytes) + bytes;
    if (total > UINT32_MAX || total > context.LoaderLimits().maxGfxWorldPayloadBytes)
        return RetailCensusError::GfxWorldPayloadLimit;
    entry.payloadBytes = static_cast<std::uint32_t>(total);
    return RetailCensusError::None;
}

RetailCensusError PlanImmediate(
    RetailLoadContext &context,
    RetailGfxWorldLoadFamily::State &state,
    std::uint32_t alignment,
    std::uint32_t bytes) noexcept
{
    if (state.planned) return RetailCensusError::None;
    if (const RetailCensusError error = context.PlanStream(
            alignment, bytes, &state.plannedSpan, &state.plannedKind);
        error != RetailCensusError::None)
        return error;
    if (state.plannedKind != ZoneLoadKind::Immediate)
        return RetailCensusError::ZoneStreamInvalid;
    state.plannedBytes = bytes;
    state.planned = true;
    return RetailCensusError::None;
}

const std::uint8_t *FinishImmediate(
    RetailLoadContext &context,
    RetailGfxWorldLoadFamily::State &state) noexcept
{
    if (context.VisitRecord(state.plannedBytes) != RetailLoadVisit::Complete)
        return nullptr;
    const auto tail = context.InflatedTail();
    if (tail.size() < state.plannedBytes)
    {
        context.BlockForInflatedInput();
        return nullptr;
    }
    const std::uint8_t *source = tail.data();
    context.ConsumeRecord(state.plannedBytes);
    state.planned = false;
    return source;
}

RetailCensusError AllocateRuntime(
    RetailLoadContext &context,
    CanonicalGfxWorldStorage &storage,
    std::uint32_t alignment,
    std::uint32_t count,
    std::uint32_t stride,
    void *&output,
    std::uint32_t canonicalStride = 0u) noexcept
{
    output = nullptr;
    std::uint32_t bytes = 0u;
    if (!CheckedBytes(count, stride,
            context.LoaderLimits().maxGfxWorldArrayElements, bytes))
        return RetailCensusError::GfxWorldCountInvalid;
    std::uint32_t canonicalBytes = 0u;
    if (!CheckedBytes(count, canonicalStride == 0u ? stride : canonicalStride,
            context.LoaderLimits().maxGfxWorldArrayElements, canonicalBytes))
        return RetailCensusError::GfxWorldCountInvalid;
    if (const RetailCensusError error = context.PushStream(1u);
        error != RetailCensusError::None) return error;
    ZoneLoadKind kind = ZoneLoadKind::Immediate;
    const RetailCensusError plan = context.PlanStream(alignment, bytes, nullptr, &kind);
    const RetailCensusError pop = context.PopStream();
    if (plan != RetailCensusError::None) return plan;
    if (pop != RetailCensusError::None) return pop;
    if (kind != ZoneLoadKind::ZeroFill) return RetailCensusError::ZoneStreamInvalid;
    std::uint8_t *memory = AllocateOwned<std::uint8_t>(
        storage, std::max(canonicalBytes, 1u));
    if (!memory) return RetailCensusError::AllocationFailed;
    output = memory;
    return RetailCensusError::None;
}

RetailCensusError Checkpoint(
    RetailLoadContext &context,
    const RetailGfxWorldLoadFamily::State &state,
    std::string name,
    const ZoneSpan &span = {}) noexcept
{
    return context.Trace(kisak::database::SemanticTraceEventKind::Dependency,
        ASSET_TYPE_GFXWORLD, Entry(context, state).assetIndex, 0u,
        static_cast<std::uint32_t>(context.InflatedCursor()), span,
        name, state.headerSpan);
}

RetailCensusError LoadXString(
    RetailLoadContext &context,
    RetailGfxWorldLoadFamily::State &state,
    std::uint32_t token,
    std::shared_ptr<std::string> &output,
    std::uint32_t &offset) noexcept
{
    output.reset(); offset = UINT32_MAX;
    if (token == 0u) return RetailCensusError::None;
    if (token != INLINE_POINTER)
    {
        if (!context.ResolveXString(token, output, offset) || !output)
            return RetailCensusError::GfxWorldNameInvalid;
        return output->size() < context.LoaderLimits().maxGfxWorldNameBytes
            ? RetailCensusError::None : RetailCensusError::GfxWorldNameTooLong;
    }
    if (!state.planned)
    {
        const auto tail = context.InflatedTail();
        const std::size_t limit = context.LoaderLimits().maxGfxWorldNameBytes;
        const std::size_t scan = std::min(tail.size(), limit + 1u);
        const auto end = std::find(tail.begin(), tail.begin() + scan, 0u);
        if (end == tail.begin() + scan)
        {
            if (tail.size() > limit) return RetailCensusError::GfxWorldNameTooLong;
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::uint32_t bytes = static_cast<std::uint32_t>(end - tail.begin() + 1u);
        if (const RetailCensusError error = PlanImmediate(context, state, 1u, bytes);
            error != RetailCensusError::None) return error;
    }
    const std::uint32_t bytes = state.plannedBytes;
    const ZoneSpan span = state.plannedSpan;
    const std::uint8_t *source = FinishImmediate(context, state);
    if (!source) return RetailCensusError::None;
    try { output = std::make_shared<std::string>(
        reinterpret_cast<const char *>(source), bytes - 1u); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    offset = span.offset;
    return context.RememberXString(token, span, output);
}

void DecodeRoot(const std::uint8_t *p, GfxWorld &w) noexcept
{
    w = {};
    w.planeCount = ReadS32(p + 0x08); w.nodeCount = ReadS32(p + 0x0c);
    w.indexCount = ReadS32(p + 0x10); w.surfaceCount = ReadS32(p + 0x18);
    w.skySurfCount = ReadS32(p + 0x20); w.skySamplerState = p[0x2c];
    w.vertexCount = ReadU32(p + 0x30); w.vertexLayerDataSize = ReadU32(p + 0x3c);
    std::memcpy(&w.sunParse, p + 0x48, sizeof(w.sunParse));
    for (std::size_t n = 0; n < 3; ++n) w.sunColorFromBsp[n] = ReadF32(p + 0xcc + n * 4u);
    w.sunPrimaryLightIndex = ReadU32(p + 0xd8); w.primaryLightCount = ReadU32(p + 0xdc);
    w.cullGroupCount = ReadS32(p + 0xe0); w.reflectionProbeCount = ReadU32(p + 0xe4);
    w.dpvsPlanes.cellCount = ReadS32(p + 0xf0); w.cellBitsCount = ReadS32(p + 0x100);
    w.lightmapCount = ReadS32(p + 0x108);
    GfxLightGrid &g = w.lightGrid; g.hasLightRegions = p[0x110] != 0;
    g.sunPrimaryLightIndex = ReadU32(p + 0x114);
    for (std::size_t n = 0; n < 3; ++n) { g.mins[n] = ReadU16(p + 0x118 + n*2u); g.maxs[n] = ReadU16(p + 0x11e + n*2u); }
    g.rowAxis = ReadU32(p + 0x124); g.colAxis = ReadU32(p + 0x128);
    g.rawRowDataSize = ReadU32(p + 0x130); g.entryCount = ReadU32(p + 0x138); g.colorCount = ReadU32(p + 0x140);
    w.modelCount = ReadS32(p + 0x150);
    for (std::size_t n=0;n<3;++n) { w.mins[n]=ReadF32(p+0x158+n*4u); w.maxs[n]=ReadF32(p+0x164+n*4u); }
    w.checksum = ReadU32(p + 0x170); w.materialMemoryCount = ReadS32(p + 0x174);
    w.dpvs.smodelCount=ReadU32(p+0x244); w.dpvs.staticSurfaceCount=ReadU32(p+0x248);
    w.dpvs.staticSurfaceCountNoDecal=ReadU32(p+0x24c); w.dpvs.litSurfsBegin=ReadU32(p+0x250);
    w.dpvs.litSurfsEnd=ReadU32(p+0x254); w.dpvs.decalSurfsBegin=ReadU32(p+0x258);
    w.dpvs.decalSurfsEnd=ReadU32(p+0x25c); w.dpvs.emissiveSurfsBegin=ReadU32(p+0x260);
    w.dpvs.emissiveSurfsEnd=ReadU32(p+0x264); w.dpvs.smodelVisDataCount=ReadU32(p+0x268);
    w.dpvs.surfaceVisDataCount=ReadU32(p+0x26c); w.dpvs.usageCount=ReadS32(p+0x2a8);
    w.dpvsDyn.dynEntClientWordCount[0]=ReadU32(p+0x2ac); w.dpvsDyn.dynEntClientWordCount[1]=ReadU32(p+0x2b0);
    w.dpvsDyn.dynEntClientCount[0]=ReadU32(p+0x2b4); w.dpvsDyn.dynEntClientCount[1]=ReadU32(p+0x2b8);
}
}

RetailGfxWorldLoadFamily::RetailGfxWorldLoadFamily() noexcept = default;
RetailGfxWorldLoadFamily::~RetailGfxWorldLoadFamily() = default;

RetailCensusError RetailGfxWorldLoadFamily::Begin(
    RetailLoadContext &context, std::uint32_t assetIndex,
    std::uint32_t serializedReference) noexcept
{
    if (state_ && state_->progress == RetailGfxWorldLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    RetailFastfileCensus &result = context.Ownership();
    if (result.worldGfxWorlds.size() >= context.LoaderLimits().maxGfxWorlds)
        return RetailCensusError::GfxWorldCollectionLimit;
    std::unique_ptr<State> next(new (std::nothrow) State());
    if (!next) return RetailCensusError::AllocationFailed;
    try { result.worldGfxWorlds.emplace_back(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    next->resultIndex = result.worldGfxWorlds.size() - 1u;
    RetailPublishedGfxWorld &entry = result.worldGfxWorlds.back();
    entry.assetIndex = assetIndex; entry.serializedReference = serializedReference;
    if (serializedReference == 0u)
    {
        entry.nullRoot = true; entry.published = true;
        entry.boundaryInflatedOffset = static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress = RetailGfxWorldLoadProgress::Complete;
        state_ = std::move(next); return RetailCensusError::None;
    }
    if (serializedReference != INLINE_POINTER && serializedReference != SHARED_POINTER)
    {
        if (context.ResolveAssetAlias(serializedReference, ASSET_TYPE_GFXWORLD,
                entry.identity) != ZoneRegistryError::None)
            return RetailCensusError::GfxWorldAliasInvalid;
        const auto found = std::find_if(result.worldGfxWorlds.begin(), result.worldGfxWorlds.end()-1,
            [&](const RetailPublishedGfxWorld &v){return v.published && v.identity==entry.identity && v.asset;});
        if (found == result.worldGfxWorlds.end()-1) return RetailCensusError::GfxWorldAliasInvalid;
        entry = *found; entry.assetIndex=assetIndex; entry.serializedReference=serializedReference;
        entry.pointerAlias=true; entry.boundaryInflatedOffset=static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress=RetailGfxWorldLoadProgress::Complete; state_=std::move(next);
        return RetailCensusError::None;
    }
    std::uint32_t tableOffset=0u;
    if (!CheckedTableAliasOffset(result, assetIndex, tableOffset)) return RetailCensusError::GfxWorldAliasInvalid;
    next->tableAlias={4u,tableOffset,4u};
    if (const auto e=RegistryError(context.Assets().ReserveAlias(next->tableAlias,ASSET_TYPE_GFXWORLD)); e!=RetailCensusError::None) return e;
    if (const auto e=context.PushStream(0u); e!=RetailCensusError::None) return e;
    if (const auto e=context.PlanStream(4u,WORLD_BYTES,&next->headerSpan,nullptr); e!=RetailCensusError::None) return e;
    entry.headerBlock0Offset=next->headerSpan.offset;
    next->hasInsertAlias=serializedReference==SHARED_POINTER;
    if (next->hasInsertAlias)
    {
        if (const auto e=context.PushStream(4u); e!=RetailCensusError::None) return e;
        if (const auto e=context.PlanStream(4u,4u,&next->insertAlias,nullptr); e!=RetailCensusError::None) return e;
        if (const auto e=context.PopStream(); e!=RetailCensusError::None) return e;
        if (const auto e=RegistryError(context.Assets().ReserveAlias(next->insertAlias,ASSET_TYPE_GFXWORLD)); e!=RetailCensusError::None) return e;
        entry.insertPointerBlock4Offset=next->insertAlias.offset;
    }
    try { entry.storage=std::make_shared<CanonicalGfxWorldStorage>(); entry.asset=std::make_shared<GfxWorld>(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    *entry.asset={};
    if (const auto e=context.Trace(kisak::database::SemanticTraceEventKind::AssetBegin,
            ASSET_TYPE_GFXWORLD,assetIndex,0u,static_cast<std::uint32_t>(context.InflatedCursor()),
            next->headerSpan,{},next->tableAlias); e!=RetailCensusError::None) return e;
    next->progress=RetailGfxWorldLoadProgress::Running; state_=std::move(next);
    return RetailCensusError::None;
}

// The complete phase implementation is intentionally kept in a separate
// continuation below so each generated child remains independently bounded.
RetailCensusError RetailGfxWorldLoadFamily::Step(RetailLoadContext &context) noexcept
{
    if (!state_ || state_->progress != RetailGfxWorldLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    State &s=*state_; RetailPublishedGfxWorld &entry=Entry(context,s);
    CanonicalGfxWorldStorage &own=*entry.storage; GfxWorld &w=*entry.asset;
    const auto token=[&](std::size_t off){return ReadU32(s.root.data()+off);};
    const auto countBytes=[&](std::uint64_t count,std::uint64_t stride,std::uint32_t limit,std::uint32_t &bytes){
        return CheckedBytes(count,stride,limit,bytes);};
    const auto loadImage=[&](std::uint32_t serialized,const ZoneSpan &cell,GfxImage *&target)->RetailCensusError
    {
        if(!s.dependencyStarted)
        {
            s.imageLoader.Reset();
            const auto e=s.imageLoader.Begin(context,entry.assetIndex,serialized,cell);
            if(e!=RetailCensusError::None)return e;
            s.dependencyStarted=true;
        }
        if(s.imageLoader.Progress()==RetailImageLoadProgress::Running)
        {
            const auto e=s.imageLoader.Step(context);if(e!=RetailCensusError::None)return e;
            if(s.imageLoader.Progress()==RetailImageLoadProgress::Running)return RetailCensusError::None;
        }
        target=s.imageLoader.Asset(context);
        if(serialized!=0u&&!target)return RetailCensusError::GfxWorldImageInvalid;
        s.dependencyStarted=false;s.imageLoader.Reset();return RetailCensusError::None;
    };
    const auto loadWater=[&](std::uint32_t serialized,void *&target)->RetailCensusError
    {
        if(s.waterPhase==0u)
        {
            target=nullptr;if(serialized==0u)return RetailCensusError::None;
            if(serialized!=INLINE_POINTER)
            {
                target=ResolveWirePointer(context,own,serialized,4u);return target?RetailCensusError::None:RetailCensusError::GfxWorldPointerInvalid;
            }
            if(const auto e=PlanImmediate(context,s,4u,68u);e!=RetailCensusError::None)return e;s.waterSpan=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;std::memcpy(s.waterHeader.data(),p,68u);s.water=AllocateOwned<CanonicalWater>(own,1u);if(!s.water)return RetailCensusError::AllocationFailed;CanonicalWater &v=*s.water;v.floatTime=ReadF32(p);v.M=ReadS32(p+12u);v.N=ReadS32(p+16u);v.Lx=ReadF32(p+20u);v.Lz=ReadF32(p+24u);v.gravity=ReadF32(p+28u);v.windvel=ReadF32(p+32u);v.winddir[0]=ReadF32(p+36u);v.winddir[1]=ReadF32(p+40u);v.amplitude=ReadF32(p+44u);for(std::size_t n=0;n<4;++n)v.codeConstant[n]=ReadF32(p+48u+n*4u);if(v.M<0||v.N<0||static_cast<std::uint64_t>(v.M)*v.N>context.LoaderLimits().maxGfxWorldArrayElements)return RetailCensusError::GfxWorldCountInvalid;s.waterPhase=1u;
        }
        const std::uint32_t count=static_cast<std::uint32_t>(s.water->M)*static_cast<std::uint32_t>(s.water->N);
        if(s.waterPhase==1u)
        {
            if(ReadU32(s.waterHeader.data()+4u)==0u)s.water->H0=nullptr;else{std::uint32_t bytes=0u;if(!CheckedBytes(count,8u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;s.water->H0=AllocateOwned<float[2]>(own,count);if(!s.water->H0)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<count;++n){s.water->H0[n][0]=ReadF32(p+n*8u);s.water->H0[n][1]=ReadF32(p+n*8u+4u);}}s.waterPhase=2u;
        }
        if(s.waterPhase==2u)
        {
            if(ReadU32(s.waterHeader.data()+8u)==0u)s.water->wTerm=nullptr;else{std::uint32_t bytes=0u;if(!CheckedBytes(count,4u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;s.water->wTerm=AllocateOwned<float>(own,count);if(!s.water->wTerm)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<count;++n)s.water->wTerm[n]=ReadF32(p+n*4u);}s.waterPhase=3u;
        }
        if(s.waterPhase==3u)
        {
            const auto e=loadImage(ReadU32(s.waterHeader.data()+64u),{s.waterSpan.block,s.waterSpan.offset+64u,4u},s.water->image);if(e!=RetailCensusError::None)return e;if(s.dependencyStarted)return RetailCensusError::None;s.waterPhase=4u;
        }
        if(s.waterPhase==4u){target=s.water;s.water=nullptr;s.waterPhase=0u;return RetailCensusError::None;}return RetailCensusError::None;
    };
    const auto loadMaterial=[&](std::uint32_t serialized,const ZoneSpan &cell,Material *&target)->RetailCensusError
    {
        if(s.materialPhase==State::MaterialPhase::Idle)
        {
            target=nullptr;
            if(serialized==0u)return RetailCensusError::None;
            if(serialized!=INLINE_POINTER&&serialized!=SHARED_POINTER)
            {
                if(!ResolveAsset(context,serialized,ASSET_TYPE_MATERIAL,target))return RetailCensusError::GfxWorldMaterialInvalid;
                return RetailCensusError::None;
            }
            s.materialPointerCell=cell;s.materialSerialized=serialized;s.materialHasInsert=serialized==SHARED_POINTER;s.materialInsertCell={};
            if(const auto e=RegistryError(context.Assets().ReserveAlias(cell,ASSET_TYPE_MATERIAL));e!=RetailCensusError::None)return e;
            if(s.materialHasInsert)
            {
                if(const auto e=context.PlanStream(4u,4u,&s.materialInsertCell,nullptr);e!=RetailCensusError::None)return e;
                if(const auto e=RegistryError(context.Assets().ReserveAlias(s.materialInsertCell,ASSET_TYPE_MATERIAL));e!=RetailCensusError::None)return e;
            }
            if(const auto e=context.PushStream(0u);e!=RetailCensusError::None)return e;s.materialBlock0=true;
            if(const auto e=context.PlanStream(4u,80u,&s.materialHeaderSpan,nullptr);e!=RetailCensusError::None)return e;
            try{context.Ownership().worldMaterials.emplace_back();context.Ownership().worldMaterials.back().asset=std::make_shared<Material>();}catch(...){return RetailCensusError::AllocationFailed;}
            s.materialResultIndex=context.Ownership().worldMaterials.size()-1u;auto &m=context.Ownership().worldMaterials.back();*m.asset={};m.handleIndex=entry.assetIndex;m.headerBlock0Offset=s.materialHeaderSpan.offset;s.materialPhase=State::MaterialPhase::Header;
        }
        RetailXModelMaterial &meta=context.Ownership().worldMaterials[s.materialResultIndex];Material &m=*meta.asset;
        if(s.materialPhase==State::MaterialPhase::Header)
        {
            if(context.VisitRecord(80u)!=RetailLoadVisit::Complete)return RetailCensusError::None;const auto tail=context.InflatedTail();if(tail.size()<80u){context.BlockForInflatedInput();return RetailCensusError::None;}std::memcpy(s.materialHeader.data(),tail.data(),80u);context.ConsumeRecord(80u);const std::uint8_t *p=s.materialHeader.data();
            meta.textureCount=p[58u];meta.constantCount=p[59u];meta.stateBitsCount=p[60u];meta.techniqueSetReference=ReadU32(p+64u);
            if(meta.textureCount>context.LoaderLimits().maxMaterialTextures||meta.constantCount>context.LoaderLimits().maxMaterialConstants||meta.stateBitsCount>context.LoaderLimits().maxMaterialStateBits||ReadU32(p)==0u||ReadU32(p)==SHARED_POINTER||(meta.textureCount==0u)!=(ReadU32(p+68u)==0u)||(meta.constantCount==0u)!=(ReadU32(p+72u)==0u)||(meta.stateBitsCount==0u)!=(ReadU32(p+76u)==0u))return RetailCensusError::MaterialLayoutUnsupported;
            m.info.gameFlags=p[4u];m.info.sortKey=p[5u];m.info.textureAtlasRowCount=p[6u];m.info.textureAtlasColumnCount=p[7u];std::memcpy(&m.info.drawSurf.packed,p+8u,8u);m.info.surfaceTypeBits=ReadU32(p+16u);m.info.hashIndex=ReadU16(p+20u);m.info.padding=ReadU16(p+22u);std::memcpy(m.stateBitsEntry,p+24u,34u);m.textureCount=meta.textureCount;m.constantCount=meta.constantCount;m.stateBitsCount=meta.stateBitsCount;m.stateFlags=p[61u];m.cameraRegion=p[62u];m.padding=p[63u];
            if(meta.techniqueSetReference==INLINE_POINTER||meta.techniqueSetReference==SHARED_POINTER)return RetailCensusError::MaterialTechniqueSetInvalid;
            if(meta.techniqueSetReference!=0u&&context.ResolveAssetAlias(meta.techniqueSetReference,ASSET_TYPE_TECHNIQUE_SET,meta.techniqueSetIdentity)!=ZoneRegistryError::None)return RetailCensusError::MaterialTechniqueSetInvalid;m.techniqueSet=nullptr;
            if(const auto e=context.PushStream(4u);e!=RetailCensusError::None)return e;s.materialBlock4=true;s.materialPhase=State::MaterialPhase::Name;
        }
        if(s.materialPhase==State::MaterialPhase::Name)
        {
            const auto e=LoadXString(context,s,ReadU32(s.materialHeader.data()),meta.canonicalName,s.materialNameOffset);if(e!=RetailCensusError::None)return e;if(ReadU32(s.materialHeader.data())==INLINE_POINTER&&!meta.canonicalName)return RetailCensusError::None;if(!meta.canonicalName||meta.canonicalName->empty())return RetailCensusError::MaterialNameInvalid;meta.name=*meta.canonicalName;meta.nameBlock4Offset=s.materialNameOffset;m.info.name=meta.canonicalName->c_str();s.materialPhase=State::MaterialPhase::Textures;
        }
        if(s.materialPhase==State::MaterialPhase::Textures)
        {
            if(meta.textureCount==0u){m.textureTable=nullptr;s.materialPhase=State::MaterialPhase::Constants;}
            else
            {
                std::uint32_t bytes=static_cast<std::uint32_t>(meta.textureCount)*12u;if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;s.materialTextureSpan=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
                auto *defs=AllocateOwned<CanonicalMaterialTextureDef>(own,meta.textureCount);if(!defs)return RetailCensusError::AllocationFailed;m.textureTable=reinterpret_cast<MaterialTextureDef*>(defs);try{s.materialImageTokens.resize(meta.textureCount);meta.textures.resize(meta.textureCount);}catch(...){return RetailCensusError::AllocationFailed;}
                for(std::uint32_t n=0;n<meta.textureCount;++n){defs[n].nameHash=ReadU32(p+n*12u);defs[n].nameStart=static_cast<char>(p[n*12u+4u]);defs[n].nameEnd=static_cast<char>(p[n*12u+5u]);defs[n].samplerState=p[n*12u+6u];defs[n].semantic=p[n*12u+7u];s.materialImageTokens[n]=ReadU32(p+n*12u+8u);auto &t=meta.textures[n];t.nameHash=defs[n].nameHash;t.nameStart=p[n*12u+4u];t.nameEnd=p[n*12u+5u];t.samplerState=defs[n].samplerState;t.semantic=defs[n].semantic;t.imageReference=s.materialImageTokens[n];if(s.materialImageTokens[n]==0u)return RetailCensusError::MaterialTextureLayoutUnsupported;}
                s.materialTextureIndex=0u;s.materialPhase=State::MaterialPhase::TextureDependencies;
            }
        }
        if(s.materialPhase==State::MaterialPhase::TextureDependencies)
        {
            if(s.materialTextureIndex<meta.textureCount)
            {
                auto *defs=reinterpret_cast<CanonicalMaterialTextureDef*>(m.textureTable);const std::size_t n=s.materialTextureIndex;RetailCensusError e=RetailCensusError::None;if(defs[n].semantic==11u)e=loadWater(s.materialImageTokens[n],defs[n].info);else{GfxImage *image=static_cast<GfxImage*>(defs[n].info);const ZoneSpan imageCell{s.materialTextureSpan.block,s.materialTextureSpan.offset+static_cast<std::uint32_t>(n)*12u+8u,4u};e=loadImage(s.materialImageTokens[n],imageCell,image);defs[n].info=image;}if(e!=RetailCensusError::None)return e;if(s.dependencyStarted||s.waterPhase!=0u)return RetailCensusError::None;meta.textures[n].resolved=defs[n].info!=nullptr;++s.materialTextureIndex;return RetailCensusError::None;
            }
            s.materialPhase=State::MaterialPhase::Constants;
        }
        if(s.materialPhase==State::MaterialPhase::Constants)
        {
            if(meta.constantCount==0u)m.constantTable=nullptr;else{const std::uint32_t bytes=static_cast<std::uint32_t>(meta.constantCount)*32u;if(const auto e=PlanImmediate(context,s,16u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;auto *defs=AllocateOwned<CanonicalMaterialConstantDef>(own,meta.constantCount);if(!defs)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<meta.constantCount;++n){defs[n].nameHash=ReadU32(p+n*32u);std::memcpy(defs[n].name,p+n*32u+4u,12u);for(std::size_t a=0;a<4;++a)defs[n].literal[a]=ReadF32(p+n*32u+16u+a*4u);}m.constantTable=reinterpret_cast<MaterialConstantDef*>(defs);}s.materialPhase=State::MaterialPhase::StateBits;
        }
        if(s.materialPhase==State::MaterialPhase::StateBits)
        {
            if(meta.stateBitsCount==0u)m.stateBitsTable=nullptr;else{const std::uint32_t bytes=static_cast<std::uint32_t>(meta.stateBitsCount)*8u;if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;auto *bits=AllocateOwned<CanonicalGfxStateBits>(own,meta.stateBitsCount);if(!bits)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<meta.stateBitsCount;++n){bits[n].loadBits[0]=ReadU32(p+n*8u);bits[n].loadBits[1]=ReadU32(p+n*8u+4u);}m.stateBitsTable=reinterpret_cast<GfxStateBits*>(bits);}s.materialPhase=State::MaterialPhase::Publish;
        }
        if(s.materialPhase==State::MaterialPhase::Publish)
        {
            if(s.materialBlock4){if(const auto e=context.PopStream();e!=RetailCensusError::None)return e;s.materialBlock4=false;}if(s.materialBlock0){if(const auto e=context.PopStream();e!=RetailCensusError::None)return e;s.materialBlock0=false;}
            const std::uint32_t source=(s.materialPointerCell.block<<28u)|s.materialPointerCell.offset;if(const auto e=RegistryError(context.Assets().RegisterAsset(ASSET_TYPE_MATERIAL,source,*meta.canonicalName,meta.identity));e!=RetailCensusError::None)return e;if(const auto e=RegistryError(context.Assets().PublishAlias(s.materialPointerCell,meta.identity));e!=RetailCensusError::None)return e;if(s.materialHasInsert)if(const auto e=RegistryError(context.Assets().PublishAlias(s.materialInsertCell,meta.identity));e!=RetailCensusError::None)return e;meta.published=true;s.materialPhase=State::MaterialPhase::Complete;
        }
        if(s.materialPhase==State::MaterialPhase::Complete){target=meta.asset.get();s.materialPhase=State::MaterialPhase::Idle;return RetailCensusError::None;}
        return RetailCensusError::None;
    };
    const auto loadXModel=[&](std::uint32_t serialized,const ZoneSpan &cell,XModel *&target)->RetailCensusError
    {
        target=nullptr;if(serialized==0u)return RetailCensusError::None;
        if(serialized!=INLINE_POINTER&&serialized!=SHARED_POINTER)return ResolveAsset(context,serialized,ASSET_TYPE_XMODEL,target)?RetailCensusError::None:RetailCensusError::GfxWorldModelInvalid;
        if(!s.xmodelDependencyStarted){const auto e=context.BeginXModelDependency(entry.assetIndex,serialized,cell);if(e!=RetailCensusError::None)return e;s.xmodelDependencyStarted=true;return RetailCensusError::None;}
        target=context.TakeXModelDependency();if(!target)return RetailCensusError::None;s.xmodelDependencyStarted=false;return RetailCensusError::None;
    };

    if (s.phase==State::Phase::Root)
    {
        if (context.VisitRecord(WORLD_BYTES)!=RetailLoadVisit::Complete) return RetailCensusError::None;
        const auto tail=context.InflatedTail(); if(tail.size()<WORLD_BYTES){context.BlockForInflatedInput();return RetailCensusError::None;}
        std::memcpy(s.root.data(),tail.data(),WORLD_BYTES); context.ConsumeRecord(WORLD_BYTES);
        DecodeRoot(s.root.data(),w);
        const auto nonnegative=[](int v){return v>=0;};
        if(!nonnegative(w.planeCount)||!nonnegative(w.nodeCount)||!nonnegative(w.indexCount)||
           !nonnegative(w.surfaceCount)||!nonnegative(w.skySurfCount)||!nonnegative(w.dpvsPlanes.cellCount)||
           !nonnegative(w.lightmapCount)||!nonnegative(w.modelCount)||!nonnegative(w.materialMemoryCount)||
           !nonnegative(w.cullGroupCount)||w.vertexCount>context.LoaderLimits().maxGfxWorldVertices||
           static_cast<std::uint32_t>(w.indexCount)>context.LoaderLimits().maxGfxWorldIndices||
           static_cast<std::uint32_t>(w.surfaceCount)>context.LoaderLimits().maxGfxWorldSurfaces||
           static_cast<std::uint32_t>(w.dpvsPlanes.cellCount)>context.LoaderLimits().maxGfxWorldCells||
           static_cast<std::uint32_t>(w.lightmapCount)>context.LoaderLimits().maxGfxWorldLightmaps||
           w.dpvs.smodelCount>context.LoaderLimits().maxGfxWorldStaticModels||
           static_cast<std::uint32_t>(w.materialMemoryCount)>context.LoaderLimits().maxGfxWorldMaterialMemory)
            return RetailCensusError::GfxWorldCountInvalid;
        if (const auto e=context.PushStream(4u);e!=RetailCensusError::None)return e;
        s.block4Pushed=true;s.phase=State::Phase::Name;return Checkpoint(context,s,"world.root",s.headerSpan);
    }
    if(s.phase==State::Phase::Name)
    {
        const auto e=LoadXString(context,s,token(0),own.name,entry.nameBlock4Offset);if(e!=RetailCensusError::None)return e;
        if(token(0)==INLINE_POINTER&&!own.name)return RetailCensusError::None;
        if(!own.name||own.name->empty())return RetailCensusError::GfxWorldNameInvalid;
        w.name=own.name->c_str();s.phase=State::Phase::BaseName;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::BaseName)
    {
        const auto e=LoadXString(context,s,token(4),own.baseName,entry.baseNameBlock4Offset);if(e!=RetailCensusError::None)return e;
        if(token(4)==INLINE_POINTER&&!own.baseName)return RetailCensusError::None;
        w.baseName=own.baseName?own.baseName->c_str():nullptr;s.phase=State::Phase::Indices;return RetailCensusError::None;
    }
    auto simpleArray=[&](State::Phase phase,std::uint32_t presence,std::uint32_t count,std::uint32_t stride,std::uint32_t alignment,
                         void *&target,State::Phase next,auto decode,const char *checkpoint)->RetailCensusError{
        if(s.phase!=phase)return RetailCensusError::InvalidArgument;
        if(presence==0u){target=nullptr;s.phase=next;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(count,stride,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,alignment,bytes);e!=RetailCensusError::None)return e;
        const ZoneSpan span=s.plannedSpan;const std::uint8_t *src=FinishImmediate(context,s);if(!src)return RetailCensusError::None;
        std::uint8_t *block=AllocateOwned<std::uint8_t>(own,std::max(bytes,1u));if(!block)return RetailCensusError::AllocationFailed;
        target=block;for(std::uint32_t n=0;n<count;++n)decode(src+n*stride,block+n*stride,n);
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;
        s.phase=next;return Checkpoint(context,s,std::string(checkpoint)+" count="+std::to_string(count),span);
    };
    if(s.phase==State::Phase::Indices)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x14),w.indexCount,2u,2u,out,State::Phase::SkySurfaces,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){const auto v=ReadU16(p);std::memcpy(d,&v,2u);},"world.indices");
        w.indices=static_cast<std::uint16_t*>(out);return e;
    }
    if(s.phase==State::Phase::SkySurfaces)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x24),w.skySurfCount,4u,4u,out,State::Phase::SkyImage,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){const auto v=ReadS32(p);std::memcpy(d,&v,4u);},"world.sky-surfaces");
        w.skyStartSurfs=static_cast<int*>(out);return e;
    }
    if(s.phase==State::Phase::SkyImage)
    {
        const auto e=loadImage(token(0x28),{s.headerSpan.block,s.headerSpan.offset+0x28u,4u},w.skyImage);if(e!=RetailCensusError::None)return e;if(s.dependencyStarted)return RetailCensusError::None;
        s.phase=State::Phase::SunLight;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::SunLight)
    {
        const std::uint32_t t=token(0xc8);if(t==0u){w.sunLight=nullptr;s.phase=State::Phase::ReflectionProbes;return RetailCensusError::None;}
        if(t!=INLINE_POINTER)return RetailCensusError::GfxWorldPointerInvalid;
        if(const auto e=PlanImmediate(context,s,4u,64u);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        w.sunLight=AllocateOwned<GfxLight>(own,1u);if(!w.sunLight)return RetailCensusError::AllocationFailed;GfxLight &l=*w.sunLight;l={};
        l.type=p[0];l.canUseShadowMap=p[1];for(int n=0;n<3;++n){l.color[n]=ReadF32(p+4+n*4);l.dir[n]=ReadF32(p+16+n*4);l.origin[n]=ReadF32(p+28+n*4);}l.radius=ReadF32(p+40);l.cosHalfFovOuter=ReadF32(p+44);l.cosHalfFovInner=ReadF32(p+48);l.exponent=ReadS32(p+52);l.spotShadowIndex=ReadU32(p+56);
        if(!ResolveAsset(context,ReadU32(p+60),ASSET_TYPE_LIGHT_DEF,l.def))return RetailCensusError::GfxWorldLightDefInvalid;
        s.phase=State::Phase::ReflectionProbes;return AddPayload(context,entry,64u);
    }
    if(s.phase==State::Phase::ReflectionProbes)
    {
        const std::uint32_t count=w.reflectionProbeCount;if(token(0xe8)==0u){w.reflectionProbes=nullptr;s.phase=State::Phase::ReflectionRuntime;return RetailCensusError::None;}
        if(!s.recordsLoaded)
        {
            std::uint32_t bytes=0;if(!countBytes(count,16u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
            if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;s.childSpan=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
            w.reflectionProbes=AllocateOwned<GfxReflectionProbe>(own,count);if(!w.reflectionProbes)return RetailCensusError::AllocationFailed;try{s.reflectionTokens.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}
            for(std::uint32_t n=0;n<count;++n){auto &r=w.reflectionProbes[n];for(int a=0;a<3;++a)r.origin[a]=ReadF32(p+n*16+a*4);s.reflectionTokens[n]=ReadU32(p+n*16+12);}
            if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.i=0u;s.recordsLoaded=true;
        }
        if(s.i<count){const ZoneSpan cell{s.childSpan.block,s.childSpan.offset+static_cast<std::uint32_t>(s.i)*16u+12u,4u};const auto e=loadImage(s.reflectionTokens[s.i],cell,w.reflectionProbes[s.i].reflectionImage);if(e!=RetailCensusError::None)return e;if(s.dependencyStarted)return RetailCensusError::None;++s.i;return RetailCensusError::None;}
        s.recordsLoaded=false;s.i=0u;s.phase=State::Phase::ReflectionRuntime;return Checkpoint(context,s,"world.reflection-probes",s.childSpan);
    }
    if(s.phase==State::Phase::ReflectionRuntime)
    {
        if(token(0xec)==0u)w.reflectionProbeTextures=nullptr;else{void *out=nullptr;const auto e=AllocateRuntime(context,own,4u,w.reflectionProbeCount,4u,out,sizeof(GfxTexture));if(e!=RetailCensusError::None)return e;w.reflectionProbeTextures=static_cast<GfxTexture*>(out);}
        s.phase=State::Phase::Planes;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::Planes)
    {
        const auto t=token(0xf4);if(t==0u){w.dpvsPlanes.planes=nullptr;s.phase=State::Phase::Nodes;return RetailCensusError::None;}
        if(t!=INLINE_POINTER)return RetailCensusError::GfxWorldPointerInvalid;std::uint32_t bytes=0;if(!countBytes(w.planeCount,20u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;w.dpvsPlanes.planes=AllocateOwned<cplane_s>(own,w.planeCount);if(!w.dpvsPlanes.planes)return RetailCensusError::AllocationFailed;std::memcpy(w.dpvsPlanes.planes,p,bytes);s.phase=State::Phase::Nodes;return AddPayload(context,entry,bytes);
    }
    if(s.phase==State::Phase::Nodes)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0xf8),w.nodeCount,2u,2u,out,State::Phase::SceneEntBits,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){const auto v=ReadU16(p);std::memcpy(d,&v,2);},"world.dpvs-planes");w.dpvsPlanes.nodes=static_cast<std::uint16_t*>(out);return e;
    }
    if(s.phase==State::Phase::SceneEntBits)
    {
        if(token(0xfc)==0u)w.dpvsPlanes.sceneEntCellBits=nullptr;
        else
        {
            void *out=nullptr;
            const auto e=AllocateRuntime(context,own,4u,
                static_cast<std::uint32_t>(w.dpvsPlanes.cellCount),1024u,out);
            if(e!=RetailCensusError::None)return e;
            w.dpvsPlanes.sceneEntCellBits=static_cast<std::uint32_t*>(out);
        }
        s.phase=State::Phase::Cells;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::Cells)
    {
        const std::uint32_t count=static_cast<std::uint32_t>(w.dpvsPlanes.cellCount);
        if(token(0x104)==0u){w.cells=nullptr;s.phase=State::Phase::Lightmaps;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(count,56u,context.LoaderLimits().maxGfxWorldCells,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;
        const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        w.cells=AllocateOwned<GfxCell>(own,count);if(!w.cells)return RetailCensusError::AllocationFailed;
        try{s.cellTokens.resize(count);s.aabbTokens.resize(count);s.portalTokens.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}
        for(std::uint32_t n=0;n<count;++n)
        {
            const std::uint8_t *q=p+n*56u;GfxCell &c=w.cells[n];c={};
            for(std::size_t a=0;a<3;++a){c.mins[a]=ReadF32(q+a*4u);c.maxs[a]=ReadF32(q+12u+a*4u);}
            c.aabbTreeCount=ReadS32(q+24u);c.portalCount=ReadS32(q+32u);c.cullGroupCount=ReadS32(q+40u);c.reflectionProbeCount=q[48u];
            if(c.aabbTreeCount<0||c.portalCount<0||c.cullGroupCount<0)return RetailCensusError::GfxWorldCellInvalid;
            s.cellTokens[n]={ReadU32(q+28u),ReadU32(q+36u),ReadU32(q+44u),ReadU32(q+52u)};
        }
        if(!RememberWireAllocation(own,span,56u,sizeof(GfxCell),w.cells))return RetailCensusError::AllocationFailed;
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;
        s.i=0u;s.phase=State::Phase::CellAabbs;return Checkpoint(context,s,"world.cells",span);
    }
    if(s.phase==State::Phase::CellAabbs)
    {
        const std::size_t count=static_cast<std::size_t>(w.dpvsPlanes.cellCount);
        if(s.i>=count){s.i=0u;s.phase=State::Phase::Lightmaps;return RetailCensusError::None;}
        GfxCell &c=w.cells[s.i];const std::uint32_t t=s.cellTokens[s.i][0];
        if(t==0u){c.aabbTree=nullptr;s.j=0u;s.phase=State::Phase::CellPortals;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(c.aabbTreeCount,44u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        c.aabbTree=AllocateOwned<GfxAabbTree>(own,c.aabbTreeCount);if(!c.aabbTree)return RetailCensusError::AllocationFailed;
        try{s.aabbTokens[s.i].resize(c.aabbTreeCount);}catch(...){return RetailCensusError::AllocationFailed;}
        for(int n=0;n<c.aabbTreeCount;++n){DecodeAabb(p+n*44u,c.aabbTree[n]);s.aabbTokens[s.i][n]=ReadU32(p+n*44u+36u);}
        if(!RememberWireAllocation(own,span,44u,sizeof(GfxAabbTree),c.aabbTree))return RetailCensusError::AllocationFailed;
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.j=0u;s.phase=State::Phase::CellAabbIndexes;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::CellAabbIndexes)
    {
        GfxCell &c=w.cells[s.i];if(s.j>=static_cast<std::size_t>(c.aabbTreeCount)){s.j=0u;s.phase=State::Phase::CellPortals;return RetailCensusError::None;}
        GfxAabbTree &a=c.aabbTree[s.j];const std::uint32_t t=s.aabbTokens[s.i][s.j];
        if(t==0u){a.smodelIndexes=nullptr;++s.j;return RetailCensusError::None;}
        if(t!=INLINE_POINTER)
        {
            a.smodelIndexes=static_cast<std::uint16_t*>(ResolveWirePointer(context,own,t,2u));
            if(!a.smodelIndexes)return RetailCensusError::GfxWorldPointerInvalid;++s.j;return RetailCensusError::None;
        }
        std::uint32_t bytes=0u;if(!countBytes(a.smodelIndexCount,2u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,2u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        a.smodelIndexes=AllocateOwned<std::uint16_t>(own,a.smodelIndexCount);if(!a.smodelIndexes)return RetailCensusError::AllocationFailed;
        for(std::uint32_t n=0;n<a.smodelIndexCount;++n)a.smodelIndexes[n]=ReadU16(p+n*2u);
        if(!RememberWireAllocation(own,span,2u,2u,a.smodelIndexes))return RetailCensusError::AllocationFailed;
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;++s.j;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::CellPortals)
    {
        GfxCell &c=w.cells[s.i];const std::uint32_t t=s.cellTokens[s.i][1];
        if(t==0u){c.portals=nullptr;s.j=0u;s.phase=State::Phase::CellCullGroups;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(c.portalCount,68u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        c.portals=AllocateOwned<GfxPortal>(own,c.portalCount);if(!c.portals)return RetailCensusError::AllocationFailed;
        try{s.portalTokens[s.i].resize(c.portalCount);}catch(...){return RetailCensusError::AllocationFailed;}
        for(int n=0;n<c.portalCount;++n){DecodePortal(p+n*68u,c.portals[n]);s.portalTokens[s.i][n]={ReadU32(p+n*68u+32u),ReadU32(p+n*68u+36u)};}
        if(!RememberWireAllocation(own,span,68u,sizeof(GfxPortal),c.portals))return RetailCensusError::AllocationFailed;
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.j=0u;s.phase=State::Phase::CellPortalVertices;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::CellPortalVertices)
    {
        GfxCell &c=w.cells[s.i];if(s.j>=static_cast<std::size_t>(c.portalCount)){s.j=0u;s.phase=State::Phase::CellCullGroups;return RetailCensusError::None;}
        GfxPortal &v=c.portals[s.j];const auto tokens=s.portalTokens[s.i][s.j];
        if(tokens[0]==INLINE_POINTER||tokens[0]==SHARED_POINTER)return RetailCensusError::GfxWorldCellInvalid;
        v.cell=static_cast<GfxCell*>(ResolveWirePointer(context,own,tokens[0],4u));if(tokens[0]!=0u&&!v.cell)return RetailCensusError::GfxWorldPointerInvalid;
        if(tokens[1]==0u){v.vertices=nullptr;++s.j;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(v.vertexCount,12u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        v.vertices=AllocateOwned<float[3]>(own,v.vertexCount);if(!v.vertices)return RetailCensusError::AllocationFailed;
        for(std::uint32_t n=0;n<v.vertexCount;++n)for(std::size_t a=0;a<3;++a)v.vertices[n][a]=ReadF32(p+n*12u+a*4u);
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;++s.j;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::CellCullGroups)
    {
        GfxCell &c=w.cells[s.i];const std::uint32_t t=s.cellTokens[s.i][2];if(t==0u){c.cullGroups=nullptr;s.phase=State::Phase::CellReflectionProbes;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(c.cullGroupCount,4u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        c.cullGroups=AllocateOwned<int>(own,c.cullGroupCount);if(!c.cullGroups)return RetailCensusError::AllocationFailed;for(int n=0;n<c.cullGroupCount;++n)c.cullGroups[n]=ReadS32(p+n*4u);
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.phase=State::Phase::CellReflectionProbes;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::CellReflectionProbes)
    {
        GfxCell &c=w.cells[s.i];const std::uint32_t t=s.cellTokens[s.i][3];if(t==0u){c.reflectionProbes=nullptr;++s.i;s.phase=State::Phase::CellAabbs;return RetailCensusError::None;}
        std::uint32_t bytes=c.reflectionProbeCount;if(const auto e=PlanImmediate(context,s,1u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        c.reflectionProbes=AllocateOwned<std::uint8_t>(own,bytes);if(!c.reflectionProbes)return RetailCensusError::AllocationFailed;std::memcpy(c.reflectionProbes,p,bytes);
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;++s.i;s.phase=State::Phase::CellAabbs;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::Lightmaps)
    {
        const std::uint32_t count=static_cast<std::uint32_t>(w.lightmapCount);if(token(0x10c)==0u){w.lightmaps=nullptr;s.phase=State::Phase::LightGridRows;return RetailCensusError::None;}
        if(!s.recordsLoaded)
        {
            std::uint32_t bytes=0u;if(!countBytes(count,8u,context.LoaderLimits().maxGfxWorldLightmaps,bytes))return RetailCensusError::GfxWorldCountInvalid;
            if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;s.childSpan=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
            w.lightmaps=AllocateOwned<GfxLightmapArray>(own,count);if(!w.lightmaps)return RetailCensusError::AllocationFailed;try{s.lightmapTokens.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}
            for(std::uint32_t n=0;n<count;++n)s.lightmapTokens[n]={ReadU32(p+n*8u),ReadU32(p+n*8u+4u)};
            if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.i=0u;s.j=0u;s.recordsLoaded=true;
        }
        if(s.i<count)
        {
            GfxImage *&target=s.j==0u?w.lightmaps[s.i].primary:w.lightmaps[s.i].secondary;const ZoneSpan cell{s.childSpan.block,s.childSpan.offset+static_cast<std::uint32_t>(s.i)*8u+static_cast<std::uint32_t>(s.j)*4u,4u};const auto e=loadImage(s.lightmapTokens[s.i][s.j],cell,target);if(e!=RetailCensusError::None)return e;if(s.dependencyStarted)return RetailCensusError::None;if(++s.j>=2u){s.j=0u;++s.i;}return RetailCensusError::None;
        }
        s.recordsLoaded=false;s.i=0u;s.j=0u;s.phase=State::Phase::LightGridRows;return Checkpoint(context,s,"world.lightmaps",s.childSpan);
    }
    if(s.phase==State::Phase::LightGridRows)
    {
        GfxLightGrid &g=w.lightGrid;if(g.rowAxis>2u||g.maxs[g.rowAxis]<g.mins[g.rowAxis])return RetailCensusError::GfxWorldCountInvalid;
        const std::uint32_t count=static_cast<std::uint32_t>(g.maxs[g.rowAxis]-g.mins[g.rowAxis])+1u;
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x12c),count,2u,2u,out,State::Phase::LightGridRaw,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){const auto v=ReadU16(p);std::memcpy(d,&v,2u);},"world.lightgrid-rows");g.rowDataStart=static_cast<std::uint16_t*>(out);return e;
    }
    if(s.phase==State::Phase::LightGridRaw)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x134),w.lightGrid.rawRowDataSize,1u,1u,out,State::Phase::LightGridEntries,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){*d=*p;},"world.lightgrid-raw");w.lightGrid.rawRowData=static_cast<std::uint8_t*>(out);return e;
    }
    if(s.phase==State::Phase::LightGridEntries)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x13c),w.lightGrid.entryCount,4u,4u,out,State::Phase::LightGridColors,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){GfxLightGridEntry v{};v.colorsIndex=ReadU16(p);v.primaryLightIndex=p[2];v.needsTrace=p[3];std::memcpy(d,&v,4u);},"world.lightgrid-entries");w.lightGrid.entries=static_cast<GfxLightGridEntry*>(out);return e;
    }
    if(s.phase==State::Phase::LightGridColors)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x144),w.lightGrid.colorCount,168u,1u,out,State::Phase::LightmapPrimaryRuntime,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){std::memcpy(d,p,168u);},"world.lightgrid-colors");w.lightGrid.colors=static_cast<GfxLightGridColors*>(out);return e;
    }
    if(s.phase==State::Phase::LightmapPrimaryRuntime)
    {
        if(token(0x148)==0u)w.lightmapPrimaryTextures=nullptr;else{void *out=nullptr;const auto e=AllocateRuntime(context,own,4u,w.lightmapCount,4u,out,sizeof(GfxTexture));if(e!=RetailCensusError::None)return e;w.lightmapPrimaryTextures=static_cast<GfxTexture*>(out);}s.phase=State::Phase::LightmapSecondaryRuntime;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::LightmapSecondaryRuntime)
    {
        if(token(0x14c)==0u)w.lightmapSecondaryTextures=nullptr;else{void *out=nullptr;const auto e=AllocateRuntime(context,own,4u,w.lightmapCount,4u,out,sizeof(GfxTexture));if(e!=RetailCensusError::None)return e;w.lightmapSecondaryTextures=static_cast<GfxTexture*>(out);}s.phase=State::Phase::Models;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::Models)
    {
        const std::uint32_t count=static_cast<std::uint32_t>(w.modelCount);if(token(0x154)==0u){w.models=nullptr;s.phase=State::Phase::MaterialMemory;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(count,56u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        w.models=AllocateOwned<GfxBrushModel>(own,count);if(!w.models)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<count;++n)DecodeBrushModel(p+n*56u,w.models[n]);
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.phase=State::Phase::MaterialMemory;return Checkpoint(context,s,"world.models",span);
    }
    if(s.phase==State::Phase::MaterialMemory)
    {
        const std::uint32_t count=static_cast<std::uint32_t>(w.materialMemoryCount);if(token(0x178)==0u){w.materialMemory=nullptr;s.phase=State::Phase::Vertices;return RetailCensusError::None;}
        if(!s.recordsLoaded)
        {
            std::uint32_t bytes=0u;if(!countBytes(count,8u,context.LoaderLimits().maxGfxWorldMaterialMemory,bytes))return RetailCensusError::GfxWorldCountInvalid;
            if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;s.childSpan=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
            w.materialMemory=AllocateOwned<MaterialMemory>(own,count);if(!w.materialMemory)return RetailCensusError::AllocationFailed;try{s.materialTokens.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}
            for(std::uint32_t n=0;n<count;++n){s.materialTokens[n]=ReadU32(p+n*8u);w.materialMemory[n].memory=ReadS32(p+n*8u+4u);}if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.i=0u;s.recordsLoaded=true;
        }
        if(s.i<count){const ZoneSpan cell{s.childSpan.block,s.childSpan.offset+static_cast<std::uint32_t>(s.i)*8u,4u};const auto e=loadMaterial(s.materialTokens[s.i],cell,w.materialMemory[s.i].material);if(e!=RetailCensusError::None)return e;if(s.materialPhase!=State::MaterialPhase::Idle)return RetailCensusError::None;++s.i;return RetailCensusError::None;}
        s.recordsLoaded=false;s.i=0u;s.phase=State::Phase::Vertices;return Checkpoint(context,s,"world.material-memory",s.childSpan);
    }
    if(s.phase==State::Phase::Vertices)
    {
        if(token(0x34)==0u){w.vd.vertices=nullptr;w.vd.worldVb=nullptr;s.phase=State::Phase::VertexLayers;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(w.vertexCount,44u,context.LoaderLimits().maxGfxWorldVertices,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        w.vd.vertices=AllocateOwned<GfxWorldVertex>(own,w.vertexCount);if(!w.vd.vertices)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<w.vertexCount;++n)DecodeVertex(p+n*44u,w.vd.vertices[n]);w.vd.worldVb=nullptr;
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.phase=State::Phase::VertexLayers;return Checkpoint(context,s,"world.vertices",span);
    }
    if(s.phase==State::Phase::VertexLayers)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x40),w.vertexLayerDataSize,1u,1u,out,State::Phase::Sunflare,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){*d=*p;},"world.vertex-layers");w.vld.data=static_cast<std::uint8_t*>(out);w.vld.layerVb=nullptr;return e;
    }
    if(s.phase==State::Phase::Sunflare)
    {
        const std::uint8_t *p=s.root.data()+0x17c;sunflare_t &v=w.sun;
        if(!s.recordsLoaded){v={};v.hasValidData=p[0u]!=0u;v.spriteSize=ReadF32(p+12u);v.flareMinSize=ReadF32(p+16u);v.flareMinDot=ReadF32(p+20u);v.flareMaxSize=ReadF32(p+24u);v.flareMaxDot=ReadF32(p+28u);v.flareMaxAlpha=ReadF32(p+32u);v.flareFadeInTime=ReadS32(p+36u);v.flareFadeOutTime=ReadS32(p+40u);v.blindMinDot=ReadF32(p+44u);v.blindMaxDot=ReadF32(p+48u);v.blindMaxDarken=ReadF32(p+52u);v.blindFadeInTime=ReadS32(p+56u);v.blindFadeOutTime=ReadS32(p+60u);v.glareMinDot=ReadF32(p+64u);v.glareMaxDot=ReadF32(p+68u);v.glareMaxLighten=ReadF32(p+72u);v.glareFadeInTime=ReadS32(p+76u);v.glareFadeOutTime=ReadS32(p+80u);for(std::size_t n=0;n<3;++n)v.sunFxPosition[n]=ReadF32(p+84u+n*4u);for(std::size_t n=0;n<16;++n)w.outdoorLookupMatrix[n/4u][n%4u]=ReadF32(s.root.data()+0x1dc+n*4u);s.i=0u;s.recordsLoaded=true;}
        if(s.i<2u){Material *&target=s.i==0u?v.spriteMaterial:v.flareMaterial;const std::uint32_t off=0x180u+static_cast<std::uint32_t>(s.i)*4u;const auto e=loadMaterial(token(off),{s.headerSpan.block,s.headerSpan.offset+off,4u},target);if(e!=RetailCensusError::None)return e;if(s.materialPhase!=State::MaterialPhase::Idle)return RetailCensusError::None;++s.i;return RetailCensusError::None;}
        s.i=0u;s.recordsLoaded=false;s.phase=State::Phase::OutdoorImage;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::OutdoorImage)
    {
        const auto e=loadImage(token(0x21c),{s.headerSpan.block,s.headerSpan.offset+0x21cu,4u},w.outdoorImage);if(e!=RetailCensusError::None)return e;if(s.dependencyStarted)return RetailCensusError::None;s.phase=State::Phase::CellCasterBits;return RetailCensusError::None;
    }
    auto runtimeArray=[&](State::Phase phase,std::uint32_t presence,std::uint64_t count,std::uint32_t stride,std::uint32_t alignment,void *&target,State::Phase next)->RetailCensusError{
        if(s.phase!=phase)return RetailCensusError::InvalidArgument;if(presence==0u){target=nullptr;s.phase=next;return RetailCensusError::None;}
        if(count>UINT32_MAX)return RetailCensusError::GfxWorldCountInvalid;const auto e=AllocateRuntime(context,own,alignment,static_cast<std::uint32_t>(count),stride,target);if(e!=RetailCensusError::None)return e;s.phase=next;return RetailCensusError::None;
    };
    if(s.phase==State::Phase::CellCasterBits)
    {
        const std::uint64_t cells=static_cast<std::uint32_t>(w.dpvsPlanes.cellCount);void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x220),cells*((cells+31u)>>5u),4u,4u,out,State::Phase::SceneDynModels);w.cellCasterBits=static_cast<std::uint32_t*>(out);return e;
    }
    if(s.phase==State::Phase::SceneDynModels)
    {
        void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x224),w.dpvsDyn.dynEntClientCount[0],6u,4u,out,State::Phase::SceneDynBrushes);w.sceneDynModel=static_cast<GfxSceneDynModel*>(out);return e;
    }
    if(s.phase==State::Phase::SceneDynBrushes)
    {
        void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x228),w.dpvsDyn.dynEntClientCount[1],4u,4u,out,State::Phase::PrimaryEntityShadow);w.sceneDynBrush=static_cast<GfxSceneDynBrush*>(out);return e;
    }
    if(s.phase==State::Phase::PrimaryEntityShadow)
    {
        if(w.primaryLightCount<=w.sunPrimaryLightIndex)return RetailCensusError::GfxWorldCountInvalid;const std::uint64_t nonsun=w.primaryLightCount-(w.sunPrimaryLightIndex+1u);void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x22c),nonsun<<12u,4u,4u,out,State::Phase::PrimaryDynModelShadow);w.primaryLightEntityShadowVis=static_cast<std::uint32_t*>(out);return e;
    }
    if(s.phase==State::Phase::PrimaryDynModelShadow)
    {
        const std::uint64_t nonsun=w.primaryLightCount-(w.sunPrimaryLightIndex+1u);void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x230),nonsun*w.dpvsDyn.dynEntClientCount[0],4u,4u,out,State::Phase::PrimaryDynBrushShadow);w.primaryLightDynEntShadowVis[0]=static_cast<std::uint32_t*>(out);return e;
    }
    if(s.phase==State::Phase::PrimaryDynBrushShadow)
    {
        const std::uint64_t nonsun=w.primaryLightCount-(w.sunPrimaryLightIndex+1u);void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x234),nonsun*w.dpvsDyn.dynEntClientCount[1],4u,4u,out,State::Phase::NonSunPrimary);w.primaryLightDynEntShadowVis[1]=static_cast<std::uint32_t*>(out);return e;
    }
    if(s.phase==State::Phase::NonSunPrimary)
    {
        void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x238),w.dpvsDyn.dynEntClientCount[0],1u,1u,out,State::Phase::ShadowGeometry);w.nonSunPrimaryLightForModelDynEnt=static_cast<std::uint8_t*>(out);return e;
    }
    if(s.phase==State::Phase::ShadowGeometry)
    {
        const std::uint32_t count=w.primaryLightCount;if(token(0x23c)==0u){w.shadowGeom=nullptr;s.phase=State::Phase::LightRegions;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(count,12u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        w.shadowGeom=AllocateOwned<GfxShadowGeometry>(own,count);if(!w.shadowGeom)return RetailCensusError::AllocationFailed;try{s.shadowTokens.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}
        for(std::uint32_t n=0;n<count;++n){w.shadowGeom[n].surfaceCount=ReadU16(p+n*12u);w.shadowGeom[n].smodelCount=ReadU16(p+n*12u+2u);s.shadowTokens[n]={ReadU32(p+n*12u+4u),ReadU32(p+n*12u+8u)};}
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.i=0u;s.j=0u;s.phase=State::Phase::ShadowSurfaceIndexes;return Checkpoint(context,s,"world.shadow-geometry",span);
    }
    if(s.phase==State::Phase::ShadowSurfaceIndexes||s.phase==State::Phase::ShadowModelIndexes)
    {
        if(s.i>=w.primaryLightCount){s.i=0u;s.phase=State::Phase::LightRegions;return RetailCensusError::None;}
        GfxShadowGeometry &g=w.shadowGeom[s.i];const bool model=s.phase==State::Phase::ShadowModelIndexes;const std::uint32_t t=s.shadowTokens[s.i][model?1u:0u];const std::uint32_t count=model?g.smodelCount:g.surfaceCount;std::uint16_t *&target=model?g.smodelIndex:g.sortedSurfIndex;
        if(t==0u){target=nullptr;if(model){++s.i;s.phase=State::Phase::ShadowSurfaceIndexes;}else s.phase=State::Phase::ShadowModelIndexes;return RetailCensusError::None;}std::uint32_t bytes=0u;if(!countBytes(count,2u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,2u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;target=AllocateOwned<std::uint16_t>(own,count);if(!target)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<count;++n)target[n]=ReadU16(p+n*2u);if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;if(model){++s.i;s.phase=State::Phase::ShadowSurfaceIndexes;}else s.phase=State::Phase::ShadowModelIndexes;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::LightRegions)
    {
        const std::uint32_t count=w.primaryLightCount;if(token(0x240)==0u){w.lightRegion=nullptr;s.phase=State::Phase::StaticSmodelVis;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(count,8u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        w.lightRegion=AllocateOwned<GfxLightRegion>(own,count);if(!w.lightRegion)return RetailCensusError::AllocationFailed;try{s.regionTokens.resize(count);s.hullTokens.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}
        for(std::uint32_t n=0;n<count;++n){w.lightRegion[n].hullCount=ReadU32(p+n*8u);s.regionTokens[n]=ReadU32(p+n*8u+4u);}
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.i=0u;s.phase=State::Phase::LightRegionHulls;return Checkpoint(context,s,"world.light-regions",span);
    }
    if(s.phase==State::Phase::LightRegionHulls)
    {
        if(s.i>=w.primaryLightCount){s.i=0u;s.phase=State::Phase::StaticSmodelVis;return RetailCensusError::None;}
        GfxLightRegion &r=w.lightRegion[s.i];if(s.regionTokens[s.i]==0u){r.hulls=nullptr;++s.i;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(r.hullCount,80u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;
        r.hulls=AllocateOwned<GfxLightRegionHull>(own,r.hullCount);if(!r.hulls)return RetailCensusError::AllocationFailed;try{s.hullTokens[s.i].resize(r.hullCount);}catch(...){return RetailCensusError::AllocationFailed;}
        for(std::uint32_t n=0;n<r.hullCount;++n){GfxLightRegionHull &h=r.hulls[n];for(std::size_t a=0;a<9;++a){h.kdopMidPoint[a]=ReadF32(p+n*80u+a*4u);h.kdopHalfSize[a]=ReadF32(p+n*80u+36u+a*4u);}h.axisCount=ReadU32(p+n*80u+72u);s.hullTokens[s.i][n]=ReadU32(p+n*80u+76u);}
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.j=0u;s.phase=State::Phase::LightRegionAxes;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::LightRegionAxes)
    {
        GfxLightRegion &r=w.lightRegion[s.i];if(s.j>=r.hullCount){++s.i;s.j=0u;s.phase=State::Phase::LightRegionHulls;return RetailCensusError::None;}
        GfxLightRegionHull &h=r.hulls[s.j];if(s.hullTokens[s.i][s.j]==0u){h.axis=nullptr;++s.j;return RetailCensusError::None;}
        std::uint32_t bytes=0u;if(!countBytes(h.axisCount,20u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;h.axis=AllocateOwned<GfxLightRegionAxis>(own,h.axisCount);if(!h.axis)return RetailCensusError::AllocationFailed;
        for(std::uint32_t n=0;n<h.axisCount;++n){for(std::size_t a=0;a<3;++a)h.axis[n].dir[a]=ReadF32(p+n*20u+a*4u);h.axis[n].midPoint=ReadF32(p+n*20u+12u);h.axis[n].halfSize=ReadF32(p+n*20u+16u);}if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;++s.j;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::StaticSmodelVis)
    {
        if(s.i>=3u){s.i=0u;s.phase=State::Phase::StaticSurfaceVis;return RetailCensusError::None;}void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x270+s.i*4u),w.dpvs.smodelCount,1u,1u,out,State::Phase::StaticSmodelVis);w.dpvs.smodelVisData[s.i]=static_cast<std::uint8_t*>(out);if(e==RetailCensusError::None)++s.i;return e;
    }
    if(s.phase==State::Phase::StaticSurfaceVis)
    {
        if(s.i>=3u){s.i=0u;s.phase=State::Phase::StaticLod;return RetailCensusError::None;}void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x27c+s.i*4u),w.dpvs.staticSurfaceCount,1u,1u,out,State::Phase::StaticSurfaceVis);w.dpvs.surfaceVisData[s.i]=static_cast<std::uint8_t*>(out);if(e==RetailCensusError::None)++s.i;return e;
    }
    if(s.phase==State::Phase::StaticLod)
    {
        void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x288),static_cast<std::uint64_t>(w.dpvs.smodelVisDataCount)*2u,4u,16u,out,State::Phase::StaticSortedSurfaces);w.dpvs.lodData=static_cast<std::uint32_t*>(out);return e;
    }
    if(s.phase==State::Phase::StaticSortedSurfaces)
    {
        void *out=nullptr;auto e=simpleArray(s.phase,token(0x28c),static_cast<std::uint64_t>(w.dpvs.staticSurfaceCountNoDecal)+w.dpvs.staticSurfaceCount,2u,2u,out,State::Phase::StaticModelInsts,
            [](const std::uint8_t *p,std::uint8_t *d,std::uint32_t){const auto v=ReadU16(p);std::memcpy(d,&v,2u);},"world.sorted-surfaces");w.dpvs.sortedSurfIndex=static_cast<std::uint16_t*>(out);return e;
    }
    if(s.phase==State::Phase::StaticModelInsts)
    {
        const std::uint32_t count=w.dpvs.smodelCount;if(token(0x290)==0u){w.dpvs.smodelInsts=nullptr;s.phase=State::Phase::StaticSurfaces;return RetailCensusError::None;}std::uint32_t bytes=0u;if(!countBytes(count,28u,context.LoaderLimits().maxGfxWorldStaticModels,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;w.dpvs.smodelInsts=AllocateOwned<GfxStaticModelInst>(own,count);if(!w.dpvs.smodelInsts)return RetailCensusError::AllocationFailed;
        for(std::uint32_t n=0;n<count;++n){for(std::size_t a=0;a<3;++a){w.dpvs.smodelInsts[n].mins[a]=ReadF32(p+n*28u+a*4u);w.dpvs.smodelInsts[n].maxs[a]=ReadF32(p+n*28u+12u+a*4u);}w.dpvs.smodelInsts[n].groundLighting.packed=ReadU32(p+n*28u+24u);}if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.phase=State::Phase::StaticSurfaces;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::StaticSurfaces)
    {
        const std::uint32_t count=static_cast<std::uint32_t>(w.surfaceCount);if(token(0x294)==0u){w.dpvs.surfaces=nullptr;s.phase=State::Phase::StaticCullGroups;return RetailCensusError::None;}std::uint32_t bytes=0u;if(!countBytes(count,48u,context.LoaderLimits().maxGfxWorldSurfaces,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const ZoneSpan span=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;w.dpvs.surfaces=AllocateOwned<GfxSurface>(own,count);if(!w.dpvs.surfaces)return RetailCensusError::AllocationFailed;try{s.surfaceMaterialTokens.resize(count);entry.surfaceMaterialIdentities.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}
        for(std::uint32_t n=0;n<count;++n){DecodeSurface(p+n*48u,w.dpvs.surfaces[n]);s.surfaceMaterialTokens[n]=ReadU32(p+n*48u+16u);if(!ResolveAsset(context,s.surfaceMaterialTokens[n],ASSET_TYPE_MATERIAL,w.dpvs.surfaces[n].material,&entry.surfaceMaterialIdentities[n]))return RetailCensusError::GfxWorldMaterialInvalid;if(w.dpvs.surfaces[n].tris.firstVertex<0||w.dpvs.surfaces[n].tris.baseIndex<0||static_cast<std::uint64_t>(w.dpvs.surfaces[n].tris.firstVertex)+w.dpvs.surfaces[n].tris.vertexCount>w.vertexCount||static_cast<std::uint64_t>(w.dpvs.surfaces[n].tris.baseIndex)+static_cast<std::uint64_t>(w.dpvs.surfaces[n].tris.triCount)*3u>static_cast<std::uint32_t>(w.indexCount))return RetailCensusError::GfxWorldSurfaceInvalid;}
        if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.phase=State::Phase::StaticCullGroups;return Checkpoint(context,s,"world.surfaces",span);
    }
    if(s.phase==State::Phase::StaticCullGroups)
    {
        const std::uint32_t count=static_cast<std::uint32_t>(w.cullGroupCount);if(token(0x298)==0u){w.dpvs.cullGroups=nullptr;s.phase=State::Phase::StaticDrawInsts;return RetailCensusError::None;}std::uint32_t bytes=0u;if(!countBytes(count,32u,context.LoaderLimits().maxGfxWorldArrayElements,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;w.dpvs.cullGroups=AllocateOwned<GfxCullGroup>(own,count);if(!w.dpvs.cullGroups)return RetailCensusError::AllocationFailed;for(std::uint32_t n=0;n<count;++n){for(std::size_t a=0;a<3;++a){w.dpvs.cullGroups[n].mins[a]=ReadF32(p+n*32u+a*4u);w.dpvs.cullGroups[n].maxs[a]=ReadF32(p+n*32u+12u+a*4u);}w.dpvs.cullGroups[n].surfaceCount=ReadS32(p+n*32u+24u);w.dpvs.cullGroups[n].startSurfIndex=ReadS32(p+n*32u+28u);}if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.phase=State::Phase::StaticDrawInsts;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::StaticDrawInsts)
    {
        const std::uint32_t count=w.dpvs.smodelCount;if(token(0x29c)==0u){w.dpvs.smodelDrawInsts=nullptr;s.phase=State::Phase::StaticSurfaceMaterials;return RetailCensusError::None;}std::uint32_t bytes=0u;if(!countBytes(count,76u,context.LoaderLimits().maxGfxWorldStaticModels,bytes))return RetailCensusError::GfxWorldCountInvalid;
        if(!s.recordsLoaded){if(const auto e=PlanImmediate(context,s,4u,bytes);e!=RetailCensusError::None)return e;s.childSpan=s.plannedSpan;const std::uint8_t *p=FinishImmediate(context,s);if(!p)return RetailCensusError::None;w.dpvs.smodelDrawInsts=AllocateOwned<GfxStaticModelDrawInst>(own,count);if(!w.dpvs.smodelDrawInsts)return RetailCensusError::AllocationFailed;try{s.drawModelTokens.resize(count);}catch(...){return RetailCensusError::AllocationFailed;}for(std::uint32_t n=0;n<count;++n){DecodeDrawInst(p+n*76u,w.dpvs.smodelDrawInsts[n]);s.drawModelTokens[n]=ReadU32(p+n*76u+56u);}if(const auto e=AddPayload(context,entry,bytes);e!=RetailCensusError::None)return e;s.i=0u;s.recordsLoaded=true;}
        if(s.i<count){const ZoneSpan cell{s.childSpan.block,s.childSpan.offset+static_cast<std::uint32_t>(s.i)*76u+56u,4u};const auto e=loadXModel(s.drawModelTokens[s.i],cell,w.dpvs.smodelDrawInsts[s.i].model);if(e!=RetailCensusError::None)return e;if(s.xmodelDependencyStarted)return RetailCensusError::None;++s.i;return RetailCensusError::None;}
        s.i=0u;s.recordsLoaded=false;s.phase=State::Phase::StaticSurfaceMaterials;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::StaticSurfaceMaterials)
    {
        if(token(0x2a0)==0u)w.dpvs.surfaceMaterials=nullptr;else{void *out=nullptr;const auto e=AllocateRuntime(context,own,4u,w.dpvs.staticSurfaceCount,8u,out);if(e!=RetailCensusError::None)return e;w.dpvs.surfaceMaterials=static_cast<GfxDrawSurf*>(out);}s.phase=State::Phase::StaticSunShadow;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::StaticSunShadow)
    {
        if(token(0x2a4)==0u)w.dpvs.surfaceCastsSunShadow=nullptr;else{void *out=nullptr;const auto e=AllocateRuntime(context,own,16u,w.dpvs.surfaceVisDataCount,4u,out);if(e!=RetailCensusError::None)return e;w.dpvs.surfaceCastsSunShadow=static_cast<std::uint32_t*>(out);}s.i=0u;s.phase=State::Phase::DynamicCellBits;return RetailCensusError::None;
    }
    if(s.phase==State::Phase::DynamicCellBits)
    {
        if(s.i>=2u){s.i=0u;s.j=0u;s.phase=State::Phase::DynamicVis;return RetailCensusError::None;}void *out=nullptr;const std::uint64_t count=static_cast<std::uint32_t>(w.dpvsPlanes.cellCount)*static_cast<std::uint64_t>(w.dpvsDyn.dynEntClientWordCount[s.i]);const auto e=runtimeArray(s.phase,token(0x2bc+s.i*4u),count,4u,4u,out,State::Phase::DynamicCellBits);w.dpvsDyn.dynEntCellBits[s.i]=static_cast<std::uint32_t*>(out);if(e==RetailCensusError::None)++s.i;return e;
    }
    if(s.phase==State::Phase::DynamicVis)
    {
        if(s.j>=3u){s.phase=State::Phase::Publish;return RetailCensusError::None;}const std::size_t type=s.i;const std::size_t vis=s.j;const std::size_t wireIndex=vis*2u+type;void *out=nullptr;const auto e=runtimeArray(s.phase,token(0x2c4+wireIndex*4u),static_cast<std::uint64_t>(w.dpvsDyn.dynEntClientWordCount[type])*32u,1u,16u,out,State::Phase::DynamicVis);w.dpvsDyn.dynEntVisData[type][vis]=static_cast<std::uint8_t*>(out);if(e!=RetailCensusError::None)return e;if(++s.i>=2u){s.i=0u;++s.j;}return RetailCensusError::None;
    }
    if(s.phase==State::Phase::Publish)
    {
        if(s.block4Pushed){if(const auto e=context.PopStream();e!=RetailCensusError::None)return e;s.block4Pushed=false;}if(const auto e=context.PopStream();e!=RetailCensusError::None)return e;
        if(const auto e=RegistryError(context.Assets().RegisterAsset(ASSET_TYPE_GFXWORLD,entry.assetIndex,*own.name,entry.identity));e!=RetailCensusError::None)return e;
        if(const auto e=RegistryError(context.Assets().PublishAlias(s.tableAlias,entry.identity));e!=RetailCensusError::None)return e;if(s.hasInsertAlias)if(const auto e=RegistryError(context.Assets().PublishAlias(s.insertAlias,entry.identity));e!=RetailCensusError::None)return e;
        entry.boundaryInflatedOffset=static_cast<std::uint32_t>(context.InflatedCursor());
        entry.block0HighWaterAtPublication=context.Streams().HighWater(0u);
        entry.block1HighWaterAtPublication=context.Streams().HighWater(1u);
        entry.block4CursorAtPublication=context.Streams().Cursor(4u);
        entry.registryAssetCountAtPublication=context.Assets().AssetCount();
        entry.registryAliasCountAtPublication=context.Assets().AliasCount();
        entry.registryDefinedAliasCountAtPublication=context.Assets().DefinedAliasCount();
        entry.published=true;
        if(const auto e=context.Trace(kisak::database::SemanticTraceEventKind::AssetPublish,ASSET_TYPE_GFXWORLD,entry.assetIndex,entry.identity,entry.boundaryInflatedOffset,s.headerSpan,*own.name,s.tableAlias);e!=RetailCensusError::None)return e;s.progress=RetailGfxWorldLoadProgress::Complete;return RetailCensusError::None;
    }
    return RetailCensusError::GfxWorldLayoutUnsupported;
}

RetailGfxWorldLoadProgress RetailGfxWorldLoadFamily::Progress() const noexcept
{ return state_ ? state_->progress : RetailGfxWorldLoadProgress::Idle; }
void RetailGfxWorldLoadFamily::Reset() noexcept { state_.reset(); }

} // namespace kisak::fastfile
