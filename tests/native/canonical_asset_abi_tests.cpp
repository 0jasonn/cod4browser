#include <database/db_asset_types.h>
#include <database/db_semantic_trace.h>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void TestCanonicalAssetTypes()
{
    Require(std::is_standard_layout_v<RawFile>,
        "RawFile remains a standard-layout canonical engine type");
    Require(std::is_standard_layout_v<XAsset>,
        "XAsset remains a standard-layout canonical engine type");
    Require(sizeof(XAssetHeader) == sizeof(void *),
        "XAssetHeader remains one native pointer wide");
    Require(offsetof(RawFile, name) == 0u &&
            offsetof(RawFile, len) == sizeof(void *) &&
            offsetof(RawFile, buffer) > offsetof(RawFile, len),
        "RawFile preserves canonical field ordering");
    Require(offsetof(XAsset, type) == 0u &&
            offsetof(XAsset, header) >= sizeof(XAssetType),
        "XAsset preserves canonical type/header ordering");

    if constexpr (sizeof(void *) == 4u)
    {
        Require(sizeof(RawFile) == 12u &&
                offsetof(RawFile, buffer) == 8u &&
                sizeof(XAssetHeader) == 4u &&
                sizeof(XAsset) == 8u &&
                offsetof(XAsset, header) == 4u,
            "32-bit native/Wasm builds match the IW3 asset ABI");
    }

    Require(ASSET_TYPE_XMODEL == static_cast<XAssetType>(3) &&
            ASSET_TYPE_MATERIAL == static_cast<XAssetType>(4) &&
            ASSET_TYPE_GFXWORLD == static_cast<XAssetType>(16) &&
            ASSET_TYPE_FX == static_cast<XAssetType>(25) &&
            ASSET_TYPE_RAWFILE == static_cast<XAssetType>(31) &&
            ASSET_TYPE_STRINGTABLE == static_cast<XAssetType>(32),
        "canonical PC XAssetType values remain stable");
}

void TestSemanticTraceContract()
{
    using kisak::database::SemanticTraceEntry;
    using kisak::database::SemanticTraceEventKind;

    const std::vector<SemanticTraceEntry> trace{
        {
            SemanticTraceEventKind::AssetBegin,
            ASSET_TYPE_RAWFILE,
            395u,
            0u,
            1234u,
            0u,
            220u,
            4u,
            999u,
            {},
        },
        {
            SemanticTraceEventKind::AssetPublish,
            ASSET_TYPE_RAWFILE,
            395u,
            1290u,
            1260u,
            0u,
            220u,
            4u,
            999u,
            "maps/createart/killhouse_art.gsc",
        },
        {
            SemanticTraceEventKind::Boundary,
            ASSET_TYPE_STRINGTABLE,
            396u,
            0u,
            1260u,
            0u,
            232u,
            4u,
            1007u,
            {},
        },
    };

    Require(std::string_view(kisak::database::SemanticTraceEventKindString(
                SemanticTraceEventKind::AssetBegin)) == "asset-begin" &&
            std::string_view(kisak::database::SemanticTraceEventKindString(
                SemanticTraceEventKind::AssetPublish)) == "asset-publish" &&
            std::string_view(kisak::database::SemanticTraceEventKindString(
                SemanticTraceEventKind::Boundary)) == "boundary",
        "semantic trace event names are stable");

    const std::uint32_t hash = kisak::database::SemanticTraceHash(trace);
    Require(hash == kisak::database::SemanticTraceHash(trace) &&
            hash != 2166136261u,
        "semantic trace hash is deterministic and includes event data");

    std::vector<SemanticTraceEntry> changed = trace;
    changed[1u].name.push_back('x');
    Require(kisak::database::SemanticTraceHash(changed) != hash,
        "semantic trace hash includes normalized asset names");
}

} // namespace

int main()
{
    TestCanonicalAssetTypes();
    TestSemanticTraceContract();
    std::cout << "canonical asset ABI tests passed\n";
    return 0;
}
