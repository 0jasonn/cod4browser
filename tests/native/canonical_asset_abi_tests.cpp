#include <EffectsCore/fx_types.h>
#include <bgame/weapon_types.h>
#include <database/db_asset_types.h>
#include <database/db_semantic_trace.h>
#include <database/localize_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/gfx_light_types.h>
#include <qcommon/com_world_types.h>
#include <sound/snd_alias_types.h>
#include <xanim/xanim_types.h>
#include <xanim/xmodel_types.h>
#include <ui/ui_asset_types.h>

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
            std::is_standard_layout_v<FxElemDef> &&
            std::is_standard_layout_v<FxTrailDef> &&
            std::is_standard_layout_v<FxElemVelStateSample> &&
            std::is_standard_layout_v<FxElemVisStateSample> &&
            std::is_standard_layout_v<FxImpactEntry> &&
            std::is_standard_layout_v<FxImpactTable> &&
            std::is_standard_layout_v<WeaponDef> &&
            std::is_standard_layout_v<ComPrimaryLight> &&
            std::is_standard_layout_v<ComWorld> &&
            std::is_standard_layout_v<GfxImage> &&
            std::is_standard_layout_v<SndCurve> &&
            std::is_standard_layout_v<LoadedSound> &&
            std::is_standard_layout_v<SoundFile> &&
            std::is_standard_layout_v<SpeakerMap> &&
            std::is_standard_layout_v<snd_alias_t> &&
            std::is_standard_layout_v<snd_alias_list_t> &&
            std::is_standard_layout_v<Font_s> &&
            std::is_standard_layout_v<Glyph> &&
            std::is_standard_layout_v<LocalizeEntry> &&
            std::is_standard_layout_v<GfxLightDef> &&
            std::is_standard_layout_v<MenuList> &&
            std::is_standard_layout_v<menuDef_t> &&
            std::is_standard_layout_v<itemDef_s> &&
            std::is_standard_layout_v<statement_s>,
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
                sizeof(MaterialTextureDef) == 12u &&
                offsetof(MaterialTextureDef, u) == 8u &&
                sizeof(MaterialConstantDef) == 32u &&
                sizeof(GfxStateBits) == 8u &&
                sizeof(water_t) == 68u &&
                offsetof(water_t, image) == 64u &&
                sizeof(complex_s) == 8u &&
                sizeof(FxEffectDef) == 32u &&
                offsetof(FxEffectDef, elemDefs) == 28u &&
                sizeof(FxElemDef) == 252u &&
                offsetof(FxElemDef, elemType) == 176u &&
                offsetof(FxElemDef, velSamples) == 180u &&
                offsetof(FxElemDef, visuals) == 188u &&
                offsetof(FxElemDef, effectOnImpact) == 216u &&
                offsetof(FxElemDef, trailDef) == 244u &&
                sizeof(FxElemVelStateSample) == 96u &&
                sizeof(FxElemVisStateSample) == 48u &&
                sizeof(FxElemMarkVisuals) == 8u &&
                sizeof(FxTrailVertex) == 20u &&
                sizeof(FxTrailDef) == 28u &&
                offsetof(FxTrailDef, verts) == 16u &&
                offsetof(FxTrailDef, inds) == 24u &&
                sizeof(FxImpactEntry) == 132u &&
                offsetof(FxImpactEntry, flesh) == 116u &&
                sizeof(FxImpactTable) == 8u &&
                offsetof(FxImpactTable, table) == 4u &&
                sizeof(WeaponDef) == 2168u &&
                sizeof(ComPrimaryLight) == 68u &&
                offsetof(ComPrimaryLight, defName) == 64u &&
                sizeof(ComWorld) == 16u &&
                offsetof(ComWorld, primaryLights) == 12u &&
                sizeof(GfxImage) == 36u &&
                offsetof(GfxImage, name) == 32u &&
                sizeof(SndCurve) == 72u &&
                offsetof(SndCurve, filename) == 0u &&
                offsetof(SndCurve, knotCount) == 4u &&
                offsetof(SndCurve, knots) == 8u &&
                sizeof(LoadedSound) == 44u &&
                offsetof(LoadedSound, name) == 0u &&
                offsetof(LoadedSound, sound) == 4u &&
                sizeof(SoundFile) == 12u &&
                offsetof(SoundFile, u) == 4u &&
                sizeof(SpeakerMap) == 408u &&
                offsetof(SpeakerMap, name) == 4u &&
                sizeof(snd_alias_t) == 92u &&
                offsetof(snd_alias_t, soundFile) == 16u &&
                offsetof(snd_alias_t, volumeFalloffCurve) == 72u &&
                offsetof(snd_alias_t, speakerMap) == 88u &&
                sizeof(snd_alias_list_t) == 12u &&
                offsetof(snd_alias_list_t, head) == 4u &&
                offsetof(snd_alias_list_t, count) == 8u &&
                sizeof(Font_s) == 24u &&
                offsetof(Font_s, material) == 12u &&
                offsetof(Font_s, glyphs) == 20u &&
                sizeof(Glyph) == 24u &&
                sizeof(LocalizeEntry) == 8u &&
                offsetof(LocalizeEntry, value) == 0u &&
                offsetof(LocalizeEntry, name) == 4u &&
                sizeof(GfxLightImage) == 8u &&
                sizeof(GfxLightDef) == 16u &&
                offsetof(GfxLightDef, attenuation) == 4u &&
                offsetof(GfxLightDef, lmapLookupStart) == 12u &&
                sizeof(windowDef_t) == 156u &&
                sizeof(ItemKeyHandler) == 12u &&
                sizeof(Operand) == 8u &&
                sizeof(expressionEntry) == 12u &&
                sizeof(statement_s) == 8u &&
                sizeof(itemDef_s) == 372u &&
                offsetof(itemDef_s, typeData) == 300u &&
                sizeof(menuDef_t) == 284u &&
                offsetof(menuDef_t, items) == 280u &&
                sizeof(MenuList) == 12u &&
                offsetof(MenuList, menus) == 8u &&
                sizeof(listBoxDef_s) == 340u &&
                sizeof(editFieldDef_s) == 32u &&
                sizeof(multiDef_s) == 392u,
            "32-bit native/Wasm builds match canonical weapon child ABIs");
    }

    Require(ASSET_TYPE_XANIMPARTS == static_cast<XAssetType>(2) &&
            ASSET_TYPE_XMODEL == static_cast<XAssetType>(3) &&
            ASSET_TYPE_MATERIAL == static_cast<XAssetType>(4) &&
            ASSET_TYPE_TECHNIQUE_SET == static_cast<XAssetType>(5) &&
            ASSET_TYPE_IMAGE == static_cast<XAssetType>(6) &&
            ASSET_TYPE_SOUND == static_cast<XAssetType>(7) &&
            ASSET_TYPE_SOUND_CURVE == static_cast<XAssetType>(8) &&
            ASSET_TYPE_LOADED_SOUND == static_cast<XAssetType>(9) &&
            ASSET_TYPE_FONT == static_cast<XAssetType>(19) &&
            ASSET_TYPE_GFXWORLD == static_cast<XAssetType>(16) &&
            ASSET_TYPE_LIGHT_DEF == static_cast<XAssetType>(17) &&
            ASSET_TYPE_MENULIST == static_cast<XAssetType>(20) &&
            ASSET_TYPE_MENU == static_cast<XAssetType>(21) &&
            ASSET_TYPE_LOCALIZE_ENTRY == static_cast<XAssetType>(22) &&
            ASSET_TYPE_WEAPON == static_cast<XAssetType>(23) &&
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
