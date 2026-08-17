#include <EffectsCore/fx_types.h>
#include <bgame/weapon_types.h>
#include <database/db_asset_types.h>
#include <database/db_semantic_trace.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/gfx_light_types.h>
#include <qcommon/com_world_types.h>
#include <xanim/xanim_types.h>
#include <xanim/xmodel_types.h>

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
    Require(std::is_standard_layout_v<XAnimParts>,
        "XAnimParts remains a standard-layout canonical engine type");
    Require(std::is_standard_layout_v<XModel> &&
            std::is_standard_layout_v<Material> &&
            std::is_standard_layout_v<MaterialTechniqueSet> &&
            std::is_standard_layout_v<MaterialTechnique> &&
            std::is_standard_layout_v<MaterialPass> &&
            std::is_standard_layout_v<FxEffectDef> &&
            std::is_standard_layout_v<WeaponDef> &&
            std::is_standard_layout_v<ComPrimaryLight> &&
            std::is_standard_layout_v<ComWorld> &&
            std::is_standard_layout_v<GfxImage> &&
            std::is_standard_layout_v<GfxLightDef>,
        "canonical weapon dependency types remain standard-layout records");
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
        Require(sizeof(XAnimParts) == 88u &&
                offsetof(XAnimParts, boneCount) == 18u &&
                offsetof(XAnimParts, randomDataShortCount) == 32u &&
                offsetof(XAnimParts, names) == 48u &&
                offsetof(XAnimParts, indices) == 76u &&
                offsetof(XAnimParts, notify) == 80u &&
                offsetof(XAnimParts, deltaPart) == 84u &&
                sizeof(XAnimPartTrans) == 36u &&
                sizeof(XAnimDeltaPartQuat) == 12u,
            "32-bit native/Wasm builds match the canonical XAnimParts graph ABI");
        Require(sizeof(XModel) == 220u &&
                offsetof(XModel, materialHandles) == 36u &&
                offsetof(XModel, lodInfo) == 40u &&
                offsetof(XModel, physPreset) == 212u &&
                sizeof(Material) == 80u &&
                offsetof(Material, stateBitsEntry) == 24u &&
                offsetof(Material, techniqueSet) == 64u &&
                sizeof(MaterialTechniqueSet) == 148u &&
                offsetof(MaterialTechniqueSet, remappedTechniqueSet) == 8u &&
                offsetof(MaterialTechniqueSet, techniques) == 12u &&
                sizeof(MaterialTechnique) == 28u &&
                offsetof(MaterialTechnique, passArray) == 8u &&
                sizeof(MaterialPass) == 20u &&
                offsetof(MaterialPass, args) == 16u &&
                sizeof(MaterialVertexDeclaration) == 100u &&
                sizeof(MaterialVertexShader) == 16u &&
                sizeof(MaterialPixelShader) == 16u &&
                sizeof(MaterialShaderArgument) == 8u &&
                sizeof(FxEffectDef) == 32u &&
                offsetof(FxEffectDef, elemDefs) == 28u &&
                sizeof(WeaponDef) == 2168u &&
                sizeof(ComPrimaryLight) == 68u &&
                offsetof(ComPrimaryLight, defName) == 64u &&
                sizeof(ComWorld) == 16u &&
                offsetof(ComWorld, primaryLights) == 12u &&
                sizeof(GfxImage) == 36u &&
                offsetof(GfxImage, name) == 32u &&
                sizeof(GfxLightImage) == 8u &&
                sizeof(GfxLightDef) == 16u &&
                offsetof(GfxLightDef, attenuation) == 4u &&
                offsetof(GfxLightDef, lmapLookupStart) == 12u,
            "32-bit native/Wasm builds match canonical weapon child ABIs");
    }

    Require(ASSET_TYPE_XANIMPARTS == static_cast<XAssetType>(2) &&
            ASSET_TYPE_XMODEL == static_cast<XAssetType>(3) &&
            ASSET_TYPE_MATERIAL == static_cast<XAssetType>(4) &&
            ASSET_TYPE_TECHNIQUE_SET == static_cast<XAssetType>(5) &&
            ASSET_TYPE_GFXWORLD == static_cast<XAssetType>(16) &&
            ASSET_TYPE_LIGHT_DEF == static_cast<XAssetType>(17) &&
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

    changed = trace;
    changed[1u].identity += 17u;
    changed[1u].inflatedOffset += 23u;
    Require(kisak::database::SemanticTraceContractHash(changed) ==
            kisak::database::SemanticTraceContractHash(trace),
        "portable contract hash excludes backend identity and inflate diagnostics");
    changed[1u].streamOffset += 1u;
    Require(kisak::database::SemanticTraceContractHash(changed) !=
            kisak::database::SemanticTraceContractHash(trace),
        "portable contract hash includes logical stream coordinates");
}

void CollectSemanticTrace(
    const kisak::database::SemanticTraceEntry &entry, void *userData)
{
    static_cast<std::vector<kisak::database::SemanticTraceEntry> *>(userData)
        ->push_back(entry);
}

void TestNativeSemanticTraceObserver()
{
    using namespace kisak::database;
    std::vector<SemanticTraceEntry> trace;

    Require(!HasSemanticTraceObserver(),
        "native semantic observer is disabled by default");
    SetSemanticTraceObserver(CollectSemanticTrace, &trace);
    ResetNativeSemanticTraceContext();
    EnterNativeSemanticTraceAsset(395u, ASSET_TYPE_RAWFILE);
    EmitNativeSemanticTrace(
        SemanticTraceEventKind::AssetBegin,
        0u, 0u, 0u, 220u, 4u, 999u);
    EmitNativeSemanticTrace(
        SemanticTraceEventKind::AssetPublish,
        0u, 0u, 0u, 220u, 4u, 999u,
        "maps/createart/killhouse_art.gsc");
    LeaveNativeSemanticTraceAsset();
    EmitNativeSemanticTrace(
        SemanticTraceEventKind::Failure,
        0u, 0u, 0u, 0u, 0u, 0u);
    ClearSemanticTraceObserver();

    Require(trace.size() == 2u &&
            trace[0u].assetType == ASSET_TYPE_RAWFILE &&
            trace[0u].assetIndex == 395u &&
            trace[0u].streamBlock == 0u &&
            trace[0u].streamOffset == 220u &&
            trace[0u].relatedBlock == 4u &&
            trace[0u].relatedOffset == 999u &&
            trace[1u].name == "maps/createart/killhouse_art.gsc",
        "native observer emits bounded logical RawFile events for active assets");
    Require(!HasSemanticTraceObserver(),
        "native semantic observer clears without retaining test state");
}

} // namespace

int main()
{
    TestCanonicalAssetTypes();
    TestSemanticTraceContract();
    TestNativeSemanticTraceObserver();
    std::cout << "canonical asset ABI tests passed\n";
    return 0;
}
