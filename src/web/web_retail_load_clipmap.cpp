#include <web/web_retail_load_clipmap.h>

#include <qcommon/cm_types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kisak::fastfile
{
namespace
{
constexpr std::uint32_t INLINE_POINTER = UINT32_MAX;
constexpr std::uint32_t SHARED_POINTER = UINT32_MAX - 1u;
constexpr std::uint32_t CLIPMAP_BYTES = 284u;
constexpr std::uint32_t MAP_ENTS_BYTES = 12u;

std::uint32_t ReadU32(const std::uint8_t *value) noexcept
{
    return static_cast<std::uint32_t>(value[0]) |
        static_cast<std::uint32_t>(value[1]) << 8u |
        static_cast<std::uint32_t>(value[2]) << 16u |
        static_cast<std::uint32_t>(value[3]) << 24u;
}

std::uint16_t ReadU16(const std::uint8_t *value) noexcept
{
    return static_cast<std::uint16_t>(value[0]) |
        static_cast<std::uint16_t>(value[1]) << 8u;
}

std::int16_t ReadS16(const std::uint8_t *value) noexcept
{
    return std::bit_cast<std::int16_t>(ReadU16(value));
}

std::int32_t ReadS32(const std::uint8_t *value) noexcept
{
    return std::bit_cast<std::int32_t>(ReadU32(value));
}

float ReadF32(const std::uint8_t *value) noexcept
{
    return std::bit_cast<float>(ReadU32(value));
}

RetailCensusError RegistryError(ZoneRegistryError error) noexcept
{
    if (error == ZoneRegistryError::None) return RetailCensusError::None;
    return error == ZoneRegistryError::AllocationFailed
        ? RetailCensusError::AllocationFailed
        : RetailCensusError::AssetRegistryInvalid;
}

bool ValidName(std::string_view name) noexcept
{
    if (name.empty() || name.front() == '/' || name.front() == '\\') return false;
    for (const unsigned char byte : name)
        if (byte < 0x21u || byte > 0x7eu || byte == '\\' || byte == ':')
            return false;
    return true;
}

bool CheckedBytes(
    std::uint32_t count,
    std::uint32_t stride,
    const RetailCensusLimits &limits,
    std::uint32_t &bytes) noexcept
{
    if (count > limits.maxClipMapArrayElements) return false;
    const std::uint64_t total = static_cast<std::uint64_t>(count) * stride;
    if (total > UINT32_MAX || total > limits.maxClipMapPayloadBytes) return false;
    bytes = static_cast<std::uint32_t>(total);
    return true;
}

template <typename T>
bool Resize(std::vector<T> &values, std::size_t count) noexcept
{
    try { values.resize(count); }
    catch (...) { return false; }
    return true;
}

std::shared_ptr<void> AllocateBlock(std::size_t bytes) noexcept
{
    void *memory = ::operator new(std::max<std::size_t>(bytes, 1u), std::nothrow);
    if (!memory) return {};
    std::memset(memory, 0, std::max<std::size_t>(bytes, 1u));
    return std::shared_ptr<void>(memory, [](void *value) noexcept {
        ::operator delete(value);
    });
}
} // namespace

struct CanonicalClipMapStorage
{
    std::shared_ptr<std::string> name;
    std::vector<cplane_s> planes;
    std::vector<cStaticModel_s> staticModels;
    std::vector<dmaterial_t> materials;
    std::vector<cbrushside_t> brushSides;
    std::vector<cplane_s> brushSidePlanes;
    std::vector<std::uint8_t> brushEdges;
    std::vector<cNode_t> nodes;
    std::vector<cplane_s> nodePlanes;
    std::vector<cLeaf_t> leafs;
    std::vector<std::uint16_t> leafBrushes;
    std::vector<cLeafBrushNode_s> leafBrushNodes;
    std::vector<std::vector<std::uint16_t>> leafBrushNodeBrushes;
    std::vector<std::uint32_t> leafSurfaces;
    std::vector<std::array<float, 3>> verts;
    std::vector<std::uint16_t> triIndices;
    std::vector<std::uint8_t> triEdgeIsWalkable;
    std::vector<CollisionBorder> borders;
    std::vector<CollisionPartition> partitions;
    std::vector<CollisionBorder> partitionBorders;
    std::vector<CollisionAabbTree> aabbTrees;
    std::vector<cmodel_t> cmodels;
    std::vector<cbrush_t> brushes;
    std::vector<cbrushside_t> inlineBrushSides;
    std::vector<std::uint8_t> inlineBrushAdjacent;
    std::vector<std::uint8_t> visibility;
    std::shared_ptr<MapEnts> mapEnts;
    std::shared_ptr<std::string> mapEntsName;
    std::shared_ptr<std::vector<char>> entityString;
    std::shared_ptr<cbrush_t> boxBrush;
    std::shared_ptr<cbrushside_t> boxBrushSide;
    std::shared_ptr<std::uint8_t> boxBrushAdjacent;
    std::array<std::shared_ptr<void>, 2> dynEntDefs;
    std::array<std::shared_ptr<void>, 2> dynEntPoses;
    std::array<std::shared_ptr<void>, 2> dynEntClients;
    std::array<std::shared_ptr<void>, 2> dynEntColls;
};

struct RetailClipMapLoadFamily::State
{
    enum class Phase : std::uint8_t
    {
        Header,
        Name,
        Planes,
        StaticModels,
        Materials,
        BrushSides,
        BrushEdges,
        Nodes,
        Leafs,
        LeafBrushes,
        LeafBrushNodes,
        LeafSurfaces,
        Verts,
        TriIndices,
        TriWalkable,
        Borders,
        Partitions,
        AabbTrees,
        CModels,
        Brushes,
        Visibility,
        MapEnts,
        BoxBrush,
        DynDefs0,
        DynDefs1,
        DynPose0,
        DynPose1,
        DynClient0,
        DynClient1,
        DynColl0,
        DynColl1,
        Publish,
    };

    RetailClipMapLoadProgress progress = RetailClipMapLoadProgress::Idle;
    Phase phase = Phase::Header;
    std::size_t resultIndex = 0u;
    ZoneSpan tableAlias{};
    ZoneSpan insertAlias{};
    ZoneSpan headerSpan{};
    ZoneSpan plannedSpan{};
    ZoneSpan planesSpan{};
    ZoneSpan brushSidesSpan{};
    ZoneSpan brushEdgesSpan{};
    ZoneSpan bordersSpan{};
    ZoneLoadKind plannedKind = ZoneLoadKind::Immediate;
    std::uint32_t plannedBytes = 0u;
    bool planned = false;
    bool hasInsertAlias = false;
    bool block4Pushed = false;
    std::array<std::uint8_t, CLIPMAP_BYTES> header{};
    std::array<std::uint32_t, 2> dynCounts{};
    std::array<std::uint32_t, 2> dynDefTokens{};
    std::array<std::uint32_t, 2> dynPoseTokens{};
    std::array<std::uint32_t, 2> dynClientTokens{};
    std::array<std::uint32_t, 2> dynCollTokens{};
    std::uint32_t nameToken = 0u;
    std::uint32_t planesToken = 0u;
    std::uint32_t staticModelsToken = 0u;
    std::uint32_t materialsToken = 0u;
    std::uint32_t brushSidesToken = 0u;
    std::uint32_t brushEdgesToken = 0u;
    std::uint32_t nodesToken = 0u;
    std::uint32_t leafsToken = 0u;
    std::uint32_t leafBrushNodesToken = 0u;
    std::uint32_t leafBrushesToken = 0u;
    std::uint32_t leafSurfacesToken = 0u;
    std::uint32_t vertsToken = 0u;
    std::uint32_t triIndicesToken = 0u;
    std::uint32_t triWalkableToken = 0u;
    std::uint32_t bordersToken = 0u;
    std::uint32_t partitionsToken = 0u;
    std::uint32_t aabbTreesToken = 0u;
    std::uint32_t cmodelsToken = 0u;
    std::uint32_t brushesToken = 0u;
    std::uint32_t visibilityToken = 0u;
    std::uint32_t mapEntsToken = 0u;
    std::uint32_t boxBrushToken = 0u;
    std::uint32_t mapEntsNameToken = 0u;
    std::uint32_t mapEntsStringToken = 0u;
    std::uint32_t mapEntsStringBytes = 0u;
    std::uint32_t mapEntsIdentity = 0u;
    ZoneSpan mapEntsSpan{};
    ZoneSpan mapEntsInsertAlias{};
    bool mapEntsHeaderLoaded = false;
    bool mapEntsHeaderPlanned = false;
    bool mapEntsBlock0Pushed = false;
    bool mapEntsBlock4Pushed = false;
    bool mapEntsNameLoaded = false;
    bool mapEntsStringLoaded = false;
    bool mapEntsHasInsertAlias = false;
    std::size_t nestedIndex = 0u;
    std::vector<std::uint8_t> nestedRecords;
};

namespace
{
RetailPublishedClipMap &Entry(
    RetailLoadContext &context,
    const RetailClipMapLoadFamily::State &state) noexcept
{
    return context.Ownership().worldClipMaps[state.resultIndex];
}

RetailCensusError AddPayload(
    RetailLoadContext &context,
    RetailPublishedClipMap &entry,
    std::uint32_t bytes) noexcept
{
    const std::uint64_t total = static_cast<std::uint64_t>(entry.payloadBytes) + bytes;
    if (total > context.LoaderLimits().maxClipMapPayloadBytes || total > UINT32_MAX)
        return RetailCensusError::ClipMapPayloadLimit;
    entry.payloadBytes = static_cast<std::uint32_t>(total);
    return RetailCensusError::None;
}

RetailCensusError PlanRecord(
    RetailLoadContext &context,
    RetailClipMapLoadFamily::State &state,
    std::uint32_t alignment,
    std::uint32_t bytes,
    bool retained = true) noexcept
{
    if (state.planned) return RetailCensusError::None;
    if (const RetailCensusError error = context.PlanStream(
            alignment, bytes, &state.plannedSpan, &state.plannedKind);
        error != RetailCensusError::None)
        return error;
    state.plannedBytes = bytes;
    state.planned = true;
    if (retained)
    {
        RetailPublishedClipMap &entry = Entry(context, state);
        if (const RetailCensusError error = AddPayload(context, entry, bytes);
            error != RetailCensusError::None)
            return error;
        if (entry.payloadBytes > context.LoaderLimits().maxRetainedClipMapBytes)
            return RetailCensusError::ClipMapPayloadLimit;
    }
    return RetailCensusError::None;
}

bool FinishRecord(
    RetailLoadContext &context,
    RetailClipMapLoadFamily::State &state,
    const std::uint8_t *&bytes) noexcept
{
    bytes = nullptr;
    if (state.plannedKind != ZoneLoadKind::Immediate)
    {
        state.planned = false;
        return true;
    }
    const RetailLoadVisit visit = context.VisitRecord(state.plannedBytes);
    if (visit != RetailLoadVisit::Complete) return false;
    const auto tail = context.InflatedTail();
    if (tail.size() < state.plannedBytes)
    {
        context.BlockForInflatedInput();
        return false;
    }
    bytes = tail.data();
    context.ConsumeRecord(state.plannedBytes);
    state.planned = false;
    return true;
}

template <typename T>
RetailCensusError LoadPlainArray(
    RetailLoadContext &context,
    RetailClipMapLoadFamily::State &state,
    std::uint32_t token,
    std::uint32_t count,
    std::uint32_t serializedStride,
    std::uint32_t alignment,
    std::vector<T> &output,
    RetailClipMapLoadFamily::State::Phase next) noexcept
{
    if (token == 0u)
    {
        output.clear();
        state.phase = next;
        return RetailCensusError::None;
    }
    std::uint32_t bytes = 0u;
    if (!CheckedBytes(count, serializedStride, context.LoaderLimits(), bytes))
        return RetailCensusError::ClipMapCountInvalid;
    if (const RetailCensusError error = PlanRecord(context, state, alignment, bytes);
        error != RetailCensusError::None)
        return error;
    const std::uint8_t *source = nullptr;
    if (!FinishRecord(context, state, source)) return RetailCensusError::None;
    if (!Resize(output, count)) return RetailCensusError::AllocationFailed;
    if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (sizeof(T) != serializedStride)
            return RetailCensusError::ClipMapLayoutUnsupported;
        if (bytes != 0u && source) std::memcpy(output.data(), source, bytes);
    }
    state.phase = next;
    return RetailCensusError::None;
}

void DecodeBrush(const std::uint8_t *source, cbrush_t &brush) noexcept
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        brush.mins[i] = ReadF32(source + i * 4u);
        brush.maxs[i] = ReadF32(source + 16u + i * 4u);
    }
    brush.contents = ReadS32(source + 12u);
    brush.numsides = ReadU32(source + 28u);
    for (std::size_t side = 0; side < 2; ++side)
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const std::size_t index = side * 3u + axis;
            brush.axialMaterialNum[side][axis] = ReadS16(source + 36u + index * 2u);
            brush.firstAdjacentSideOffsets[side][axis] =
                ReadS16(source + 52u + index * 2u);
            brush.edgeCount[side][axis] = source[64u + index];
        }
}

template <typename T>
T *ResolveArrayPointer(
    std::uint32_t token,
    const ZoneSpan &span,
    std::uint32_t stride,
    std::vector<T> &values) noexcept
{
    ZoneSpan target;
    if (!DecodeZoneAliasToken(token, target) || target.block != span.block ||
        target.offset < span.offset || stride == 0u)
        return nullptr;
    const std::uint32_t delta = target.offset - span.offset;
    if (delta % stride != 0u || delta / stride >= values.size()) return nullptr;
    return values.data() + delta / stride;
}

std::uint8_t *ResolveBytePointer(
    std::uint32_t token,
    const ZoneSpan &span,
    std::vector<std::uint8_t> &values) noexcept
{
    ZoneSpan target;
    if (!DecodeZoneAliasToken(token, target) || target.block != span.block ||
        target.offset < span.offset || target.offset - span.offset >= values.size())
        return nullptr;
    return values.data() + (target.offset - span.offset);
}
} // namespace

RetailClipMapLoadFamily::RetailClipMapLoadFamily() noexcept = default;
RetailClipMapLoadFamily::~RetailClipMapLoadFamily() = default;

RetailCensusError RetailClipMapLoadFamily::Begin(
    RetailLoadContext &context,
    std::uint32_t assetIndex,
    std::uint32_t assetType,
    std::uint32_t serializedReference) noexcept
{
    if (state_ && state_->progress == RetailClipMapLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    if (assetType != static_cast<std::uint32_t>(ASSET_TYPE_CLIPMAP) &&
        assetType != static_cast<std::uint32_t>(ASSET_TYPE_CLIPMAP_PVS))
        return RetailCensusError::ClipMapLayoutUnsupported;
    RetailFastfileCensus &result = context.Ownership();
    if (result.worldClipMaps.size() >= context.LoaderLimits().maxClipMaps)
        return RetailCensusError::ClipMapCollectionLimit;

    if (serializedReference != INLINE_POINTER &&
        serializedReference != SHARED_POINTER)
    {
        std::uint32_t identity = 0u;
        if (serializedReference == 0u ||
            context.ResolveAssetAlias(serializedReference, assetType, identity) !=
                ZoneRegistryError::None)
            return RetailCensusError::ClipMapAliasInvalid;
        void *canonical = context.FindCanonicalAsset(assetType, identity);
        if (!canonical) return RetailCensusError::ClipMapAliasInvalid;
        try { result.worldClipMaps.emplace_back(); }
        catch (...) { return RetailCensusError::AllocationFailed; }
        RetailPublishedClipMap &entry = result.worldClipMaps.back();
        entry.assetIndex = assetIndex;
        entry.assetType = assetType;
        entry.serializedReference = serializedReference;
        entry.identity = identity;
        entry.pointerAlias = true;
        entry.published = true;
        for (RetailPublishedClipMap &prior : result.worldClipMaps)
            if (&prior != &entry && prior.identity == identity && prior.asset)
            {
                entry.asset = prior.asset;
                entry.storage = prior.storage;
                break;
            }
        if (!entry.asset) return RetailCensusError::ClipMapAliasInvalid;
        std::unique_ptr<State> complete(new (std::nothrow) State());
        if (!complete) return RetailCensusError::AllocationFailed;
        complete->resultIndex = result.worldClipMaps.size() - 1u;
        complete->progress = RetailClipMapLoadProgress::Complete;
        state_ = std::move(complete);
        return RetailCensusError::None;
    }

    std::unique_ptr<State> next(new (std::nothrow) State());
    if (!next) return RetailCensusError::AllocationFailed;
    next->progress = RetailClipMapLoadProgress::Running;
    next->tableAlias = {
        4u, result.assetTableBlock4Offset + assetIndex * 8u + 4u, 4u};
    if (const RetailCensusError error = RegistryError(
            context.Assets().ReserveAlias(next->tableAlias, assetType));
        error != RetailCensusError::None)
        return error;
    if (const RetailCensusError error = context.PushStream(0u);
        error != RetailCensusError::None)
        return error;
    if (const RetailCensusError error = context.PlanStream(
            4u, CLIPMAP_BYTES, &next->headerSpan, nullptr);
        error != RetailCensusError::None)
        return error;
    next->hasInsertAlias = serializedReference == SHARED_POINTER;
    if (next->hasInsertAlias)
    {
        if (const RetailCensusError error = context.PushStream(4u);
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = context.PlanStream(
                4u, 4u, &next->insertAlias, nullptr);
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = RegistryError(
                context.Assets().ReserveAlias(next->insertAlias, assetType));
            error != RetailCensusError::None)
            return error;
    }
    try
    {
        result.worldClipMaps.emplace_back();
        RetailPublishedClipMap &entry = result.worldClipMaps.back();
        entry.storage = std::make_shared<CanonicalClipMapStorage>();
        entry.asset = std::make_shared<clipMap_t>();
    }
    catch (...) { return RetailCensusError::AllocationFailed; }
    next->resultIndex = result.worldClipMaps.size() - 1u;
    RetailPublishedClipMap &entry = result.worldClipMaps.back();
    *entry.asset = {};
    entry.assetIndex = assetIndex;
    entry.assetType = assetType;
    entry.serializedReference = serializedReference;
    entry.headerBlock0Offset = next->headerSpan.offset;
    if (next->hasInsertAlias)
        entry.insertPointerBlock4Offset = next->insertAlias.offset;
    if (const RetailCensusError error = context.Trace(
            kisak::database::SemanticTraceEventKind::AssetBegin,
            assetType, assetIndex, 0u,
            static_cast<std::uint32_t>(context.InflatedCursor()),
            next->headerSpan, {}, next->tableAlias);
        error != RetailCensusError::None)
        return error;
    state_ = std::move(next);
    return RetailCensusError::None;
}

RetailCensusError RetailClipMapLoadFamily::Step(RetailLoadContext &context) noexcept
{
    if (!state_ || state_->progress != RetailClipMapLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    State &state = *state_;
    RetailPublishedClipMap &entry = Entry(context, state);
    CanonicalClipMapStorage &storage = *entry.storage;
    clipMap_t &map = *entry.asset;
    const auto advance = [&](State::Phase phase) noexcept { state.phase = phase; };

    if (state.phase == State::Phase::Header)
    {
        const RetailLoadVisit visit = context.VisitRecord(CLIPMAP_BYTES);
        if (visit != RetailLoadVisit::Complete) return RetailCensusError::None;
        const auto tail = context.InflatedTail();
        if (tail.size() < CLIPMAP_BYTES)
        {
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        std::memcpy(state.header.data(), tail.data(), CLIPMAP_BYTES);
        context.ConsumeRecord(CLIPMAP_BYTES);
        const std::uint8_t *h = state.header.data();
        state.nameToken = ReadU32(h + 0u);
        map.isInUse = ReadS32(h + 4u);
        map.planeCount = ReadS32(h + 8u);
        state.planesToken = ReadU32(h + 12u);
        map.numStaticModels = ReadU32(h + 16u);
        state.staticModelsToken = ReadU32(h + 20u);
        map.numMaterials = ReadU32(h + 24u);
        state.materialsToken = ReadU32(h + 28u);
        map.numBrushSides = ReadU32(h + 32u);
        state.brushSidesToken = ReadU32(h + 36u);
        map.numBrushEdges = ReadU32(h + 40u);
        state.brushEdgesToken = ReadU32(h + 44u);
        map.numNodes = ReadU32(h + 48u);
        state.nodesToken = ReadU32(h + 52u);
        map.numLeafs = ReadU32(h + 56u);
        state.leafsToken = ReadU32(h + 60u);
        map.leafbrushNodesCount = ReadU32(h + 64u);
        state.leafBrushNodesToken = ReadU32(h + 68u);
        map.numLeafBrushes = ReadU32(h + 72u);
        state.leafBrushesToken = ReadU32(h + 76u);
        map.numLeafSurfaces = ReadU32(h + 80u);
        state.leafSurfacesToken = ReadU32(h + 84u);
        map.vertCount = ReadU32(h + 88u);
        state.vertsToken = ReadU32(h + 92u);
        map.triCount = ReadS32(h + 96u);
        state.triIndicesToken = ReadU32(h + 100u);
        state.triWalkableToken = ReadU32(h + 104u);
        map.borderCount = ReadS32(h + 108u);
        state.bordersToken = ReadU32(h + 112u);
        map.partitionCount = ReadS32(h + 116u);
        state.partitionsToken = ReadU32(h + 120u);
        map.aabbTreeCount = ReadS32(h + 124u);
        state.aabbTreesToken = ReadU32(h + 128u);
        map.numSubModels = ReadU32(h + 132u);
        state.cmodelsToken = ReadU32(h + 136u);
        map.numBrushes = ReadU16(h + 140u);
        state.brushesToken = ReadU32(h + 144u);
        map.numClusters = ReadS32(h + 148u);
        map.clusterBytes = ReadS32(h + 152u);
        state.visibilityToken = ReadU32(h + 156u);
        map.vised = ReadS32(h + 160u);
        state.mapEntsToken = ReadU32(h + 164u);
        state.boxBrushToken = ReadU32(h + 168u);
        for (std::size_t i = 0u; i < 3u; ++i)
        {
            map.box_model.mins[i] = ReadF32(h + 172u + i * 4u);
            map.box_model.maxs[i] = ReadF32(h + 184u + i * 4u);
        }
        map.box_model.radius = ReadF32(h + 196u);
        std::memcpy(&map.box_model.leaf, h + 200u, 44u);
        state.dynCounts = {ReadU16(h + 244u), ReadU16(h + 246u)};
        for (std::size_t i = 0u; i < 2u; ++i)
        {
            state.dynDefTokens[i] = ReadU32(h + 248u + i * 4u);
            state.dynPoseTokens[i] = ReadU32(h + 256u + i * 4u);
            state.dynClientTokens[i] = ReadU32(h + 264u + i * 4u);
            state.dynCollTokens[i] = ReadU32(h + 272u + i * 4u);
            map.dynEntCount[i] = static_cast<std::uint16_t>(state.dynCounts[i]);
        }
        map.checksum = ReadU32(h + 280u);
        if (map.planeCount < 0 || map.triCount < 0 || map.borderCount < 0 ||
            map.partitionCount < 0 || map.aabbTreeCount < 0 ||
            map.numClusters < 0 || map.clusterBytes < 0)
            return RetailCensusError::ClipMapCountInvalid;
        if (const RetailCensusError error = context.PushStream(4u);
            error != RetailCensusError::None)
            return error;
        state.block4Pushed = true;
        advance(State::Phase::Name);
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Name)
    {
        if (state.nameToken == 0u) return RetailCensusError::ClipMapNameInvalid;
        if (state.nameToken != INLINE_POINTER)
        {
            std::uint32_t offset = UINT32_MAX;
            if (!context.ResolveXString(state.nameToken, storage.name, offset) ||
                !storage.name || !ValidName(*storage.name))
                return RetailCensusError::ClipMapNameInvalid;
            entry.nameBlock4Offset = offset;
            map.name = storage.name->c_str();
            advance(State::Phase::Planes);
            return RetailCensusError::None;
        }
        const auto tail = context.InflatedTail();
        const auto end = std::find(tail.begin(), tail.end(), 0u);
        if (end == tail.end())
        {
            if (tail.size() > context.LoaderLimits().maxClipMapNameBytes)
                return RetailCensusError::ClipMapNameTooLong;
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::size_t bytes = static_cast<std::size_t>(end - tail.begin()) + 1u;
        if (bytes <= 1u || bytes - 1u > context.LoaderLimits().maxClipMapNameBytes)
            return bytes <= 1u ? RetailCensusError::ClipMapNameInvalid
                               : RetailCensusError::ClipMapNameTooLong;
        if (const RetailCensusError error = PlanRecord(
                context, state, 1u, static_cast<std::uint32_t>(bytes));
            error != RetailCensusError::None)
            return error;
        const std::uint8_t *source = nullptr;
        if (!FinishRecord(context, state, source)) return RetailCensusError::None;
        try
        {
            storage.name = std::make_shared<std::string>(
                reinterpret_cast<const char *>(source), bytes - 1u);
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        if (!ValidName(*storage.name)) return RetailCensusError::ClipMapNameInvalid;
        entry.nameBlock4Offset = state.plannedSpan.offset;
        map.name = storage.name->c_str();
        if (const RetailCensusError error = context.RememberXString(
                state.nameToken, state.plannedSpan, storage.name);
            error != RetailCensusError::None)
            return error;
        advance(State::Phase::Planes);
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Planes)
    {
        if (state.planesToken != 0u && state.planesToken != INLINE_POINTER)
            return RetailCensusError::ClipMapPointerInvalid;
        const RetailCensusError error = LoadPlainArray(context, state,
            state.planesToken, static_cast<std::uint32_t>(map.planeCount),
            20u, 4u, storage.planes, State::Phase::StaticModels);
        map.planes = storage.planes.empty() ? nullptr : storage.planes.data();
        if (error == RetailCensusError::None &&
            state.phase == State::Phase::StaticModels && state.planesToken != 0u)
            state.planesSpan = state.plannedSpan;
        return error;
    }

    if (state.phase == State::Phase::StaticModels)
    {
        if (state.staticModelsToken == 0u)
        {
            advance(State::Phase::Materials);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(map.numStaticModels, 80u, context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (const RetailCensusError error = PlanRecord(context, state, 4u, bytes);
            error != RetailCensusError::None)
            return error;
        const std::uint8_t *source = nullptr;
        if (!FinishRecord(context, state, source)) return RetailCensusError::None;
        if (!Resize(storage.staticModels, map.numStaticModels))
            return RetailCensusError::AllocationFailed;
        for (std::size_t i = 0u; i < storage.staticModels.size(); ++i)
        {
            const std::uint8_t *record = source + i * 80u;
            cStaticModel_s &model = storage.staticModels[i];
            model.writable.nextModelInWorldSector = ReadU16(record);
            const std::uint32_t token = ReadU32(record + 4u);
            if (token != 0u)
            {
                if (token == INLINE_POINTER || token == SHARED_POINTER)
                    return RetailCensusError::ClipMapDependencyUnsupported;
                std::uint32_t identity = 0u;
                if (context.ResolveAssetAlias(token, ASSET_TYPE_XMODEL, identity) !=
                        ZoneRegistryError::None ||
                    !(model.xmodel = static_cast<XModel *>(
                        context.FindCanonicalAsset(ASSET_TYPE_XMODEL, identity))))
                    return RetailCensusError::ClipMapDependencyUnsupported;
            }
            for (std::size_t value = 0u; value < 18u; ++value)
                reinterpret_cast<float *>(&model.origin[0])[value] =
                    ReadF32(record + 8u + value * 4u);
        }
        map.staticModelList = storage.staticModels.data();
        advance(State::Phase::Materials);
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Materials)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.materialsToken, map.numMaterials, 72u, 4u,
            storage.materials, State::Phase::BrushSides);
        map.materials = storage.materials.empty() ? nullptr : storage.materials.data();
        return error;
    }

    if (state.phase == State::Phase::BrushSides)
    {
        if (state.brushSidesToken == 0u)
        {
            advance(State::Phase::BrushEdges);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(map.numBrushSides, 12u, context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (state.nestedRecords.empty())
        {
            if (const RetailCensusError error = PlanRecord(context, state, 4u, bytes);
                error != RetailCensusError::None)
                return error;
            const std::uint8_t *source = nullptr;
            if (!FinishRecord(context, state, source)) return RetailCensusError::None;
            try
            {
                state.nestedRecords.assign(source, source + bytes);
                storage.brushSidePlanes.reserve(map.numBrushSides);
            }
            catch (...) { return RetailCensusError::AllocationFailed; }
            state.brushSidesSpan = state.plannedSpan;
            if (!Resize(storage.brushSides, map.numBrushSides))
                return RetailCensusError::AllocationFailed;
        }
        while (state.nestedIndex < storage.brushSides.size())
        {
            const std::uint8_t *record = state.nestedRecords.data() +
                state.nestedIndex * 12u;
            cbrushside_t &side = storage.brushSides[state.nestedIndex];
            const std::uint32_t plane = ReadU32(record);
            if (plane != 0u)
            {
                if (plane == INLINE_POINTER)
                {
                    if (const RetailCensusError error = PlanRecord(
                            context, state, 4u, 20u);
                        error != RetailCensusError::None)
                        return error;
                    const std::uint8_t *planeBytes = nullptr;
                    if (!FinishRecord(context, state, planeBytes))
                        return RetailCensusError::None;
                    try { storage.brushSidePlanes.emplace_back(); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    std::memcpy(&storage.brushSidePlanes.back(), planeBytes, 20u);
                    side.plane = &storage.brushSidePlanes.back();
                }
                else if (!(side.plane = ResolveArrayPointer(
                             plane, state.planesSpan, 20u, storage.planes)))
                    return RetailCensusError::ClipMapPointerInvalid;
            }
            side.materialNum = ReadU32(record + 4u);
            side.firstAdjacentSideOffset = ReadS16(record + 8u);
            side.edgeCount = record[10u];
            ++state.nestedIndex;
        }
        map.brushsides = storage.brushSides.data();
        state.nestedRecords.clear();
        state.nestedIndex = 0u;
        advance(State::Phase::BrushEdges);
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::BrushEdges)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.brushEdgesToken, map.numBrushEdges, 1u, 1u,
            storage.brushEdges, State::Phase::Nodes);
        map.brushEdges = storage.brushEdges.empty() ? nullptr : storage.brushEdges.data();
        if (error == RetailCensusError::None && state.phase == State::Phase::Nodes &&
            state.brushEdgesToken != 0u)
            state.brushEdgesSpan = state.plannedSpan;
        return error;
    }

    if (state.phase == State::Phase::Nodes)
    {
        if (state.nodesToken == 0u)
        {
            advance(State::Phase::Leafs);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(map.numNodes, 8u, context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (state.nestedRecords.empty())
        {
            if (const RetailCensusError error = PlanRecord(context, state, 4u, bytes);
                error != RetailCensusError::None)
                return error;
            const std::uint8_t *source = nullptr;
            if (!FinishRecord(context, state, source)) return RetailCensusError::None;
            try
            {
                state.nestedRecords.assign(source, source + bytes);
                storage.nodePlanes.reserve(map.numNodes);
            }
            catch (...) { return RetailCensusError::AllocationFailed; }
            if (!Resize(storage.nodes, map.numNodes))
                return RetailCensusError::AllocationFailed;
        }
        while (state.nestedIndex < storage.nodes.size())
        {
            const std::uint8_t *record = state.nestedRecords.data() +
                state.nestedIndex * 8u;
            const std::uint32_t plane = ReadU32(record);
            if (plane != 0u)
            {
                if (plane == INLINE_POINTER)
                {
                    if (const RetailCensusError error = PlanRecord(
                            context, state, 4u, 20u);
                        error != RetailCensusError::None)
                        return error;
                    const std::uint8_t *planeBytes = nullptr;
                    if (!FinishRecord(context, state, planeBytes))
                        return RetailCensusError::None;
                    try { storage.nodePlanes.emplace_back(); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    std::memcpy(&storage.nodePlanes.back(), planeBytes, 20u);
                    storage.nodes[state.nestedIndex].plane =
                        &storage.nodePlanes.back();
                }
                else if (!(storage.nodes[state.nestedIndex].plane =
                             ResolveArrayPointer(plane, state.planesSpan,
                                 20u, storage.planes)))
                    return RetailCensusError::ClipMapPointerInvalid;
            }
            storage.nodes[state.nestedIndex].children[0] = ReadS16(record + 4u);
            storage.nodes[state.nestedIndex].children[1] = ReadS16(record + 6u);
            ++state.nestedIndex;
        }
        map.nodes = storage.nodes.data();
        state.nestedRecords.clear();
        state.nestedIndex = 0u;
        advance(State::Phase::Leafs);
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Leafs)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.leafsToken, map.numLeafs, 44u, 4u,
            storage.leafs, State::Phase::LeafBrushes);
        map.leafs = storage.leafs.empty() ? nullptr : storage.leafs.data();
        return error;
    }
    if (state.phase == State::Phase::LeafBrushes)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.leafBrushesToken, map.numLeafBrushes, 2u, 2u,
            storage.leafBrushes, State::Phase::LeafBrushNodes);
        map.leafbrushes = storage.leafBrushes.empty() ? nullptr : storage.leafBrushes.data();
        return error;
    }

    if (state.phase == State::Phase::LeafBrushNodes)
    {
        if (state.leafBrushNodesToken == 0u)
        {
            advance(State::Phase::LeafSurfaces);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(map.leafbrushNodesCount, 20u,
                context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (state.nestedRecords.empty())
        {
            if (const RetailCensusError error = PlanRecord(context, state, 4u, bytes);
                error != RetailCensusError::None)
                return error;
            const std::uint8_t *source = nullptr;
            if (!FinishRecord(context, state, source)) return RetailCensusError::None;
            try { state.nestedRecords.assign(source, source + bytes); }
            catch (...) { return RetailCensusError::AllocationFailed; }
            if (!Resize(storage.leafBrushNodes, map.leafbrushNodesCount) ||
                !Resize(storage.leafBrushNodeBrushes, map.leafbrushNodesCount))
                return RetailCensusError::AllocationFailed;
        }
        while (state.nestedIndex < storage.leafBrushNodes.size())
        {
            const std::uint8_t *record = state.nestedRecords.data() +
                state.nestedIndex * 20u;
            cLeafBrushNode_s &node = storage.leafBrushNodes[state.nestedIndex];
            node.axis = record[0u];
            node.leafBrushCount = ReadS16(record + 2u);
            node.contents = ReadS32(record + 4u);
            if (node.leafBrushCount > 0)
            {
                const std::uint32_t token = ReadU32(record + 8u);
                if (token != 0u)
                {
                    if (token != INLINE_POINTER)
                        return RetailCensusError::ClipMapPointerInvalid;
                    std::uint32_t childBytes = 0u;
                    if (!CheckedBytes(static_cast<std::uint32_t>(node.leafBrushCount),
                            2u, context.LoaderLimits(), childBytes))
                        return RetailCensusError::ClipMapCountInvalid;
                    if (const RetailCensusError error = PlanRecord(
                            context, state, 2u, childBytes);
                        error != RetailCensusError::None)
                        return error;
                    const std::uint8_t *child = nullptr;
                    if (!FinishRecord(context, state, child))
                        return RetailCensusError::None;
                    auto &values = storage.leafBrushNodeBrushes[state.nestedIndex];
                    if (!Resize(values, node.leafBrushCount))
                        return RetailCensusError::AllocationFailed;
                    std::memcpy(values.data(), child, childBytes);
                    node.data.leaf.brushes = values.data();
                }
            }
            else
            {
                std::memcpy(&node.data.children, record + 8u, 12u);
            }
            ++state.nestedIndex;
        }
        map.leafbrushNodes = storage.leafBrushNodes.data();
        state.nestedRecords.clear();
        state.nestedIndex = 0u;
        advance(State::Phase::LeafSurfaces);
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::LeafSurfaces)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.leafSurfacesToken, map.numLeafSurfaces, 4u, 4u,
            storage.leafSurfaces, State::Phase::Verts);
        map.leafsurfaces = storage.leafSurfaces.empty() ? nullptr : storage.leafSurfaces.data();
        return error;
    }
    if (state.phase == State::Phase::Verts)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.vertsToken, map.vertCount, 12u, 4u,
            storage.verts, State::Phase::TriIndices);
        map.verts = storage.verts.empty() ? nullptr
            : reinterpret_cast<float (*)[3]>(storage.verts.data());
        return error;
    }
    if (state.phase == State::Phase::TriIndices)
    {
        const std::uint64_t count = static_cast<std::uint64_t>(map.triCount) * 3u;
        if (count > UINT32_MAX) return RetailCensusError::ClipMapCountInvalid;
        const RetailCensusError error = LoadPlainArray(context, state,
            state.triIndicesToken, static_cast<std::uint32_t>(count), 2u, 2u,
            storage.triIndices, State::Phase::TriWalkable);
        map.triIndices = storage.triIndices.empty() ? nullptr : storage.triIndices.data();
        return error;
    }
    if (state.phase == State::Phase::TriWalkable)
    {
        const std::uint64_t bits = static_cast<std::uint64_t>(map.triCount) * 3u;
        const std::uint64_t bytes64 = 4u * ((bits + 31u) >> 5u);
        if (bytes64 > UINT32_MAX) return RetailCensusError::ClipMapCountInvalid;
        const RetailCensusError error = LoadPlainArray(context, state,
            state.triWalkableToken, static_cast<std::uint32_t>(bytes64), 1u, 1u,
            storage.triEdgeIsWalkable, State::Phase::Borders);
        map.triEdgeIsWalkable = storage.triEdgeIsWalkable.empty() ? nullptr
            : storage.triEdgeIsWalkable.data();
        return error;
    }
    if (state.phase == State::Phase::Borders)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.bordersToken, static_cast<std::uint32_t>(map.borderCount),
            28u, 4u, storage.borders, State::Phase::Partitions);
        map.borders = storage.borders.empty() ? nullptr : storage.borders.data();
        if (error == RetailCensusError::None &&
            state.phase == State::Phase::Partitions && state.bordersToken != 0u)
            state.bordersSpan = state.plannedSpan;
        return error;
    }

    if (state.phase == State::Phase::Partitions)
    {
        if (state.partitionsToken == 0u)
        {
            advance(State::Phase::AabbTrees);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(static_cast<std::uint32_t>(map.partitionCount), 12u,
                context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (state.nestedRecords.empty())
        {
            if (const RetailCensusError error = PlanRecord(context, state, 4u, bytes);
                error != RetailCensusError::None)
                return error;
            const std::uint8_t *source = nullptr;
            if (!FinishRecord(context, state, source)) return RetailCensusError::None;
            try
            {
                state.nestedRecords.assign(source, source + bytes);
                storage.partitionBorders.reserve(map.partitionCount);
            }
            catch (...) { return RetailCensusError::AllocationFailed; }
            if (!Resize(storage.partitions, map.partitionCount))
                return RetailCensusError::AllocationFailed;
        }
        while (state.nestedIndex < storage.partitions.size())
        {
            const std::uint8_t *record = state.nestedRecords.data() +
                state.nestedIndex * 12u;
            CollisionPartition &partition = storage.partitions[state.nestedIndex];
            partition.triCount = record[0u];
            partition.borderCount = record[1u];
            partition.firstTri = ReadS32(record + 4u);
            const std::uint32_t token = ReadU32(record + 8u);
            if (token != 0u)
            {
                if (token == INLINE_POINTER)
                {
                    if (const RetailCensusError error = PlanRecord(
                            context, state, 4u, 28u);
                        error != RetailCensusError::None)
                        return error;
                    const std::uint8_t *border = nullptr;
                    if (!FinishRecord(context, state, border))
                        return RetailCensusError::None;
                    try { storage.partitionBorders.emplace_back(); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    std::memcpy(&storage.partitionBorders.back(), border, 28u);
                    partition.borders = &storage.partitionBorders.back();
                }
                else if (!(partition.borders = ResolveArrayPointer(
                             token, state.bordersSpan, 28u, storage.borders)))
                    return RetailCensusError::ClipMapPointerInvalid;
            }
            ++state.nestedIndex;
        }
        map.partitions = storage.partitions.data();
        state.nestedRecords.clear();
        state.nestedIndex = 0u;
        advance(State::Phase::AabbTrees);
        return RetailCensusError::None;
    }
    if (state.phase == State::Phase::AabbTrees)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.aabbTreesToken, static_cast<std::uint32_t>(map.aabbTreeCount),
            32u, 4u, storage.aabbTrees, State::Phase::CModels);
        map.aabbTrees = storage.aabbTrees.empty() ? nullptr : storage.aabbTrees.data();
        return error;
    }
    if (state.phase == State::Phase::CModels)
    {
        const RetailCensusError error = LoadPlainArray(context, state,
            state.cmodelsToken, map.numSubModels, 72u, 4u,
            storage.cmodels, State::Phase::Brushes);
        map.cmodels = storage.cmodels.empty() ? nullptr : storage.cmodels.data();
        return error;
    }

    if (state.phase == State::Phase::Brushes)
    {
        if (state.brushesToken == 0u)
        {
            advance(State::Phase::Visibility);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(map.numBrushes, 80u, context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (const RetailCensusError error = PlanRecord(context, state, 16u, bytes);
            error != RetailCensusError::None)
            return error;
        const std::uint8_t *source = nullptr;
        if (!FinishRecord(context, state, source)) return RetailCensusError::None;
        if (!Resize(storage.brushes, map.numBrushes))
            return RetailCensusError::AllocationFailed;
        for (std::size_t i = 0u; i < storage.brushes.size(); ++i)
        {
            const std::uint8_t *record = source + i * 80u;
            DecodeBrush(record, storage.brushes[i]);
            const std::uint32_t sides = ReadU32(record + 32u);
            const std::uint32_t adjacent = ReadU32(record + 48u);
            if (sides != 0u &&
                !(storage.brushes[i].sides = ResolveArrayPointer(
                    sides, state.brushSidesSpan, 12u, storage.brushSides)))
                return RetailCensusError::ClipMapPointerInvalid;
            if (adjacent != 0u &&
                !(storage.brushes[i].baseAdjacentSide = ResolveBytePointer(
                    adjacent, state.brushEdgesSpan, storage.brushEdges)))
                return RetailCensusError::ClipMapPointerInvalid;
        }
        map.brushes = storage.brushes.data();
        advance(State::Phase::Visibility);
        return RetailCensusError::None;
    }
    if (state.phase == State::Phase::Visibility)
    {
        const std::uint64_t bytes64 =
            static_cast<std::uint64_t>(map.numClusters) * map.clusterBytes;
        if (bytes64 > UINT32_MAX) return RetailCensusError::ClipMapCountInvalid;
        const RetailCensusError error = LoadPlainArray(context, state,
            state.visibilityToken, static_cast<std::uint32_t>(bytes64), 1u, 1u,
            storage.visibility, State::Phase::MapEnts);
        map.visibility = storage.visibility.empty() ? nullptr : storage.visibility.data();
        return error;
    }

    if (state.phase == State::Phase::MapEnts)
    {
        if (state.mapEntsToken == 0u)
        {
            advance(State::Phase::BoxBrush);
            return RetailCensusError::None;
        }
        if (state.mapEntsToken != INLINE_POINTER &&
            state.mapEntsToken != SHARED_POINTER)
        {
            std::uint32_t identity = 0u;
            if (context.ResolveAssetAlias(state.mapEntsToken, ASSET_TYPE_MAP_ENTS,
                    identity) != ZoneRegistryError::None ||
                !(map.mapEnts = static_cast<MapEnts *>(
                    context.FindCanonicalAsset(ASSET_TYPE_MAP_ENTS, identity))))
                return RetailCensusError::ClipMapDependencyUnsupported;
            advance(State::Phase::BoxBrush);
            return RetailCensusError::None;
        }
        if (!state.mapEntsHeaderLoaded)
        {
            if (!state.mapEntsHeaderPlanned)
            {
                if (const RetailCensusError error = context.PushStream(0u);
                    error != RetailCensusError::None)
                    return error;
                state.mapEntsBlock0Pushed = true;
                if (const RetailCensusError error = context.PlanStream(
                        4u, MAP_ENTS_BYTES, &state.mapEntsSpan, nullptr);
                    error != RetailCensusError::None)
                    return error;
                state.mapEntsHasInsertAlias = state.mapEntsToken == SHARED_POINTER;
                if (state.mapEntsHasInsertAlias)
                {
                    if (const RetailCensusError error = context.PushStream(4u);
                        error != RetailCensusError::None)
                        return error;
                    if (const RetailCensusError error = context.PlanStream(
                            4u, 4u, &state.mapEntsInsertAlias, nullptr);
                        error != RetailCensusError::None)
                        return error;
                    if (const RetailCensusError error = context.PopStream();
                        error != RetailCensusError::None)
                        return error;
                    if (const RetailCensusError error = RegistryError(
                            context.Assets().ReserveAlias(
                                state.mapEntsInsertAlias, ASSET_TYPE_MAP_ENTS));
                        error != RetailCensusError::None)
                        return error;
                }
                state.mapEntsHeaderPlanned = true;
            }
            if (context.VisitRecord(MAP_ENTS_BYTES) != RetailLoadVisit::Complete)
                return RetailCensusError::None;
            const auto tail = context.InflatedTail();
            if (tail.size() < MAP_ENTS_BYTES)
            {
                context.BlockForInflatedInput();
                return RetailCensusError::None;
            }
            const std::uint8_t *record = tail.data();
            try { storage.mapEnts = std::make_shared<MapEnts>(); }
            catch (...) { return RetailCensusError::AllocationFailed; }
            *storage.mapEnts = {};
            state.mapEntsNameToken = ReadU32(record);
            state.mapEntsStringToken = ReadU32(record + 4u);
            state.mapEntsStringBytes = ReadU32(record + 8u);
            context.ConsumeRecord(MAP_ENTS_BYTES);
            state.mapEntsHeaderLoaded = true;
            if (const RetailCensusError error = context.PushStream(4u);
                error != RetailCensusError::None)
                return error;
            state.mapEntsBlock4Pushed = true;
        }
        if (!state.mapEntsNameLoaded)
        {
            std::uint32_t nameOffset = UINT32_MAX;
            if (state.mapEntsNameToken == INLINE_POINTER)
            {
                const auto tail = context.InflatedTail();
                const auto end = std::find(tail.begin(), tail.end(), 0u);
                if (end == tail.end())
                {
                    if (tail.size() > context.LoaderLimits().maxClipMapNameBytes)
                        return RetailCensusError::ClipMapNameTooLong;
                    context.BlockForInflatedInput();
                    return RetailCensusError::None;
                }
                const std::size_t nameBytes =
                    static_cast<std::size_t>(end - tail.begin()) + 1u;
                if (nameBytes <= 1u ||
                    nameBytes - 1u > context.LoaderLimits().maxClipMapNameBytes)
                    return RetailCensusError::ClipMapNameInvalid;
                if (const RetailCensusError error = PlanRecord(context, state, 1u,
                        static_cast<std::uint32_t>(nameBytes));
                    error != RetailCensusError::None)
                    return error;
                const std::uint8_t *name = nullptr;
                if (!FinishRecord(context, state, name))
                    return RetailCensusError::None;
                try
                {
                    storage.mapEntsName = std::make_shared<std::string>(
                        reinterpret_cast<const char *>(name), nameBytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                nameOffset = state.plannedSpan.offset;
                if (const RetailCensusError error = context.RememberXString(
                        state.mapEntsNameToken, state.plannedSpan,
                        storage.mapEntsName);
                    error != RetailCensusError::None)
                    return error;
            }
            else if (!context.ResolveXString(state.mapEntsNameToken,
                         storage.mapEntsName, nameOffset))
                return RetailCensusError::ClipMapDependencyUnsupported;
            if (!storage.mapEntsName || !ValidName(*storage.mapEntsName))
                return RetailCensusError::ClipMapDependencyUnsupported;
            storage.mapEnts->name = storage.mapEntsName->c_str();
            state.mapEntsNameLoaded = true;
        }
        if (!state.mapEntsStringLoaded)
        {
            if (state.mapEntsStringToken != 0u)
            {
                if (state.mapEntsStringBytes >
                    context.LoaderLimits().maxClipMapPayloadBytes)
                    return RetailCensusError::ClipMapPayloadLimit;
                if (const RetailCensusError error = PlanRecord(context, state, 1u,
                        state.mapEntsStringBytes);
                    error != RetailCensusError::None)
                    return error;
                const std::uint8_t *text = nullptr;
                if (!FinishRecord(context, state, text))
                    return RetailCensusError::None;
                try
                {
                    storage.entityString = std::make_shared<std::vector<char>>(
                        text, text + state.mapEntsStringBytes);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                storage.mapEnts->entityString = storage.entityString->data();
            }
            storage.mapEnts->numEntityChars =
                static_cast<int>(state.mapEntsStringBytes);
            state.mapEntsStringLoaded = true;
        }
        if (state.mapEntsBlock4Pushed)
        {
            if (const RetailCensusError error = context.PopStream();
                error != RetailCensusError::None)
                return error;
            state.mapEntsBlock4Pushed = false;
        }
        if (state.mapEntsBlock0Pushed)
        {
            if (const RetailCensusError error = context.PopStream();
                error != RetailCensusError::None)
                return error;
            state.mapEntsBlock0Pushed = false;
        }
        if (const RetailCensusError error = RegistryError(
                context.Assets().RegisterAsset(ASSET_TYPE_MAP_ENTS,
                    entry.assetIndex, *storage.mapEntsName, state.mapEntsIdentity));
            error != RetailCensusError::None)
            return error;
        if (state.mapEntsHasInsertAlias)
            if (const RetailCensusError error = RegistryError(
                    context.Assets().PublishAlias(
                        state.mapEntsInsertAlias, state.mapEntsIdentity));
                error != RetailCensusError::None)
                return error;
        map.mapEnts = storage.mapEnts.get();
        advance(State::Phase::BoxBrush);
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::BoxBrush)
    {
        if (state.boxBrushToken == 0u)
        {
            advance(State::Phase::DynDefs0);
            return RetailCensusError::None;
        }
        if (state.boxBrushToken != INLINE_POINTER)
            return RetailCensusError::ClipMapPointerInvalid;
        if (const RetailCensusError error = PlanRecord(context, state, 16u, 80u);
            error != RetailCensusError::None)
            return error;
        const std::uint8_t *record = nullptr;
        if (!FinishRecord(context, state, record)) return RetailCensusError::None;
        try { storage.boxBrush = std::make_shared<cbrush_t>(); }
        catch (...) { return RetailCensusError::AllocationFailed; }
        *storage.boxBrush = {};
        DecodeBrush(record, *storage.boxBrush);
        const std::uint32_t sides = ReadU32(record + 32u);
        const std::uint32_t adjacent = ReadU32(record + 48u);
        if (sides != 0u &&
            !(storage.boxBrush->sides = ResolveArrayPointer(
                sides, state.brushSidesSpan, 12u, storage.brushSides)))
            return RetailCensusError::ClipMapPointerInvalid;
        if (adjacent != 0u &&
            !(storage.boxBrush->baseAdjacentSide = ResolveBytePointer(
                adjacent, state.brushEdgesSpan, storage.brushEdges)))
            return RetailCensusError::ClipMapPointerInvalid;
        map.box_brush = storage.boxBrush.get();
        advance(State::Phase::DynDefs0);
        return RetailCensusError::None;
    }

    auto loadDynDefs = [&](std::size_t list, State::Phase next) noexcept
        -> RetailCensusError {
        if (state.dynDefTokens[list] == 0u)
        {
            advance(next);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(state.dynCounts[list], 96u,
                context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (const RetailCensusError error = PlanRecord(context, state, 4u, bytes);
            error != RetailCensusError::None)
            return error;
        const std::uint8_t *source = nullptr;
        if (!FinishRecord(context, state, source)) return RetailCensusError::None;
        storage.dynEntDefs[list] = AllocateBlock(bytes);
        if (!storage.dynEntDefs[list]) return RetailCensusError::AllocationFailed;
        if (bytes != 0u) std::memcpy(storage.dynEntDefs[list].get(), source, bytes);
        for (std::size_t i = 0u; i < state.dynCounts[list]; ++i)
        {
            const std::uint8_t *record = source + i * 96u;
            const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> deps = {{
                {ReadU32(record + 32u), ASSET_TYPE_XMODEL},
                {ReadU32(record + 40u), ASSET_TYPE_FX},
                {ReadU32(record + 44u), ASSET_TYPE_XMODELPIECES},
                {ReadU32(record + 48u), ASSET_TYPE_PHYSPRESET},
            }};
            for (std::size_t dependency = 0u; dependency < deps.size(); ++dependency)
            {
                if (deps[dependency].first == 0u) continue;
                // The pointer-sized DynEntityDef ABI is compiled in the later
                // dynentity/runtime milestone. Preserve exact bytes and fail
                // closed rather than publishing a host-width imitation when a
                // canonical dependency pointer would need patching here.
                return RetailCensusError::ClipMapDependencyUnsupported;
            }
        }
        map.dynEntDefList[list] = reinterpret_cast<DynEntityDef *>(
            storage.dynEntDefs[list].get());
        advance(next);
        return RetailCensusError::None;
    };

    if (state.phase == State::Phase::DynDefs0)
        return loadDynDefs(0u, State::Phase::DynDefs1);
    if (state.phase == State::Phase::DynDefs1)
        return loadDynDefs(1u, State::Phase::DynPose0);

    auto loadZeroFill = [&](std::size_t list, std::uint32_t token,
                            std::uint32_t stride, std::shared_ptr<void> &output,
                            auto &target,
                            State::Phase next) noexcept -> RetailCensusError {
        if (token == 0u)
        {
            target = nullptr;
            advance(next);
            return RetailCensusError::None;
        }
        std::uint32_t bytes = 0u;
        if (!CheckedBytes(state.dynCounts[list], stride,
                context.LoaderLimits(), bytes))
            return RetailCensusError::ClipMapCountInvalid;
        if (const RetailCensusError error = context.PushStream(1u);
            error != RetailCensusError::None)
            return error;
        ZoneLoadKind kind = ZoneLoadKind::Immediate;
        if (const RetailCensusError error = context.PlanStream(
                4u, bytes, nullptr, &kind);
            error != RetailCensusError::None)
            return error;
        if (kind != ZoneLoadKind::ZeroFill)
            return RetailCensusError::ZoneStreamInvalid;
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        output = AllocateBlock(bytes);
        if (!output) return RetailCensusError::AllocationFailed;
        using Pointer = std::remove_reference_t<decltype(target)>;
        target = reinterpret_cast<Pointer>(output.get());
        advance(next);
        return RetailCensusError::None;
    };

    if (state.phase == State::Phase::DynPose0)
        return loadZeroFill(0u, state.dynPoseTokens[0], 32u,
            storage.dynEntPoses[0], map.dynEntPoseList[0], State::Phase::DynPose1);
    if (state.phase == State::Phase::DynPose1)
        return loadZeroFill(1u, state.dynPoseTokens[1], 32u,
            storage.dynEntPoses[1], map.dynEntPoseList[1], State::Phase::DynClient0);
    if (state.phase == State::Phase::DynClient0)
        return loadZeroFill(0u, state.dynClientTokens[0], 12u,
            storage.dynEntClients[0], map.dynEntClientList[0], State::Phase::DynClient1);
    if (state.phase == State::Phase::DynClient1)
        return loadZeroFill(1u, state.dynClientTokens[1], 12u,
            storage.dynEntClients[1], map.dynEntClientList[1], State::Phase::DynColl0);
    if (state.phase == State::Phase::DynColl0)
        return loadZeroFill(0u, state.dynCollTokens[0], 20u,
            storage.dynEntColls[0], map.dynEntCollList[0], State::Phase::DynColl1);
    if (state.phase == State::Phase::DynColl1)
        return loadZeroFill(1u, state.dynCollTokens[1], 20u,
            storage.dynEntColls[1], map.dynEntCollList[1], State::Phase::Publish);

    if (state.phase == State::Phase::Publish)
    {
        if (state.block4Pushed)
        {
            if (const RetailCensusError error = context.PopStream();
                error != RetailCensusError::None)
                return error;
            state.block4Pushed = false;
        }
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        if (!storage.name || !map.name) return RetailCensusError::ClipMapNameInvalid;
        if (const RetailCensusError error = RegistryError(
                context.Assets().RegisterAsset(entry.assetType, entry.assetIndex,
                    *storage.name, entry.identity));
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = RegistryError(
                context.Assets().PublishAlias(state.tableAlias, entry.identity));
            error != RetailCensusError::None)
            return error;
        if (state.hasInsertAlias)
            if (const RetailCensusError error = RegistryError(
                    context.Assets().PublishAlias(state.insertAlias, entry.identity));
                error != RetailCensusError::None)
                return error;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        entry.published = true;
        if (const RetailCensusError error = context.Trace(
                kisak::database::SemanticTraceEventKind::AssetPublish,
                entry.assetType, entry.assetIndex, entry.identity,
                entry.boundaryInflatedOffset, state.headerSpan,
                *storage.name, state.tableAlias);
            error != RetailCensusError::None)
            return error;
        state.progress = RetailClipMapLoadProgress::Complete;
        return RetailCensusError::None;
    }
    return RetailCensusError::ClipMapLayoutUnsupported;
}

RetailClipMapLoadProgress RetailClipMapLoadFamily::Progress() const noexcept
{
    return state_ ? state_->progress : RetailClipMapLoadProgress::Idle;
}

void RetailClipMapLoadFamily::Reset() noexcept
{
    state_.reset();
}

} // namespace kisak::fastfile
