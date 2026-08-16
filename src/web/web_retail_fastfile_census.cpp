#include <web/web_retail_fastfile_census.h>
#include <web/web_retail_load_context.h>
#include <web/web_retail_load_clipmap.h>
#include <web/web_retail_load_comworld.h>
#include <web/web_retail_load_lightdef.h>
#include <web/web_retail_load_weapon.h>

#include <web/web_fastfile_source_stream.h>
#include <web/web_fastfile_zone_registry.h>
#include <web/web_fastfile_zone_stream.h>
#include <web/web_shader_compatibility.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>

namespace kisak::fastfile
{
namespace
{
constexpr std::array<std::uint8_t, 8> UNSIGNED_MAGIC = {'I','W','f','f','u','1','0','0'};
constexpr std::array<std::uint8_t, 8> AUTHENTICATED_MAGIC = {'I','W','f','f','0','1','0','0'};
constexpr std::uint32_t PREFIX_BYTES = 12u;
constexpr std::uint32_t XFILE_BYTES = 44u;
constexpr std::uint32_t ASSET_LIST_BYTES = 16u;
constexpr std::uint32_t ASSET_BYTES = 8u;
constexpr std::uint32_t INLINE_POINTER = 0xffffffffu;
constexpr std::uint32_t SHARED_POINTER = 0xfffffffeu;
constexpr std::uint32_t TECHNIQUE_SET_BYTES = 148u;
constexpr std::uint32_t TECHNIQUE_HEADER_BYTES = 8u;
constexpr std::uint32_t MATERIAL_PASS_BYTES = 20u;
constexpr std::uint32_t VERTEX_DECLARATION_BYTES = 100u;
constexpr std::uint32_t VERTEX_SHADER_BYTES = 16u;
constexpr std::uint32_t PIXEL_SHADER_BYTES = 16u;
constexpr std::uint32_t MATERIAL_ARGUMENT_BYTES = 8u;
constexpr std::uint32_t MATERIAL_LITERAL_CONSTANT_BYTES = 16u;
constexpr std::uint32_t MATERIAL_BYTES = 80u;
constexpr std::uint32_t MATERIAL_TEXTURE_BYTES = 12u;
constexpr std::uint32_t GFX_IMAGE_BYTES = 36u;
constexpr std::uint32_t GFX_IMAGE_LOAD_DEF_BYTES = 16u;
constexpr std::uint32_t GFX_STATE_BITS_BYTES = 8u;
constexpr std::uint32_t XMODEL_BYTES = 220u;
constexpr std::uint32_t DOBJ_ANIM_MAT_BYTES = 32u;
constexpr std::uint32_t XSURFACE_BYTES = 56u;
constexpr std::uint32_t GFX_PACKED_VERTEX_BYTES = 32u;
constexpr std::uint32_t MAX_RETAINED_RENDER_VERTICES = 4096u;
constexpr std::uint32_t MAX_RETAINED_RENDER_TRIANGLES = 4096u;
constexpr std::uint32_t MAX_RETAINED_LOD_VERTICES = 16384u;
constexpr std::uint32_t MAX_RETAINED_LOD_TRIANGLES = 16384u;
constexpr std::uint32_t RIGID_VERT_LIST_BYTES = 12u;
constexpr std::uint32_t SURFACE_COLLISION_TREE_BYTES = 40u;
constexpr std::uint32_t SURFACE_COLLISION_NODE_BYTES = 16u;
constexpr std::uint32_t SURFACE_COLLISION_LEAF_BYTES = 2u;
constexpr std::uint32_t MATERIAL_CONSTANT_BYTES = 32u;
constexpr std::uint32_t XMODEL_COLLISION_SURFACE_BYTES = 44u;
constexpr std::uint32_t XMODEL_COLLISION_TRIANGLE_BYTES = 48u;
constexpr std::uint32_t XBONE_INFO_BYTES = 40u;
constexpr std::uint32_t PHYS_PRESET_BYTES = 44u;
constexpr std::uint32_t PHYS_GEOM_LIST_BYTES = 44u;
constexpr std::uint32_t PHYS_GEOM_INFO_BYTES = 68u;
constexpr std::uint32_t BRUSH_WRAPPER_BYTES = 80u;
constexpr std::uint32_t BRUSH_SIDE_BYTES = 12u;
constexpr std::uint32_t COLLISION_PLANE_BYTES = 20u;
constexpr std::uint32_t FX_EFFECT_DEF_BYTES = 32u;
constexpr std::uint32_t FX_ELEM_DEF_BYTES = 252u;
constexpr std::uint32_t FX_VELOCITY_SAMPLE_BYTES = 96u;
constexpr std::uint32_t FX_VISUAL_SAMPLE_BYTES = 48u;
constexpr std::uint32_t FX_TRAIL_DEF_BYTES = 28u;
constexpr std::uint32_t FX_TRAIL_VERTEX_BYTES = 20u;
constexpr std::uint32_t RAWFILE_BYTES = 12u;
constexpr std::uint32_t XANIM_PARTS_BYTES = 88u;
constexpr std::uint32_t XANIM_NOTIFY_BYTES = 8u;
constexpr std::uint32_t XANIM_DELTA_PART_BYTES = 8u;
constexpr std::uint32_t XANIM_DELTA_TRANS_HEADER_BYTES = 4u;
constexpr std::uint32_t XANIM_DELTA_TRANS_FRAMES_BYTES = 28u;
constexpr std::uint32_t XANIM_DELTA_QUAT_HEADER_BYTES = 4u;
constexpr std::uint32_t XANIM_DELTA_QUAT_FRAMES_BYTES = 4u;
constexpr std::uint32_t WEAPON_DEF_BYTES = 2168u;
constexpr std::uint32_t LOCALIZE_ENTRY_BYTES = 8u;
constexpr std::uint32_t SOUND_ALIAS_LIST_BYTES = 12u;
constexpr std::uint32_t SOUND_ALIAS_BYTES = 92u;
constexpr std::uint32_t SOUND_FILE_BYTES = 12u;
constexpr std::uint32_t SOUND_CURVE_BYTES = 72u;
constexpr std::uint32_t SPEAKER_MAP_BYTES = 408u;
constexpr std::uint32_t FX_IMPACT_TABLE_BYTES = 8u;
constexpr std::uint32_t FX_IMPACT_ENTRY_COUNT = 12u;
constexpr std::uint32_t FX_IMPACT_ENTRY_BYTES = 132u;
constexpr std::uint32_t ASSET_TYPE_PHYS_PRESET = 1u;
constexpr std::uint32_t ASSET_TYPE_XANIM_PARTS = 2u;
constexpr std::uint32_t ASSET_TYPE_XMODEL = 3u;
constexpr std::uint32_t ASSET_TYPE_MATERIAL = 4u;
constexpr std::uint32_t ASSET_TYPE_TECHNIQUE_SET = 5u;
constexpr std::uint32_t ASSET_TYPE_IMAGE = 6u;
constexpr std::uint32_t ASSET_TYPE_SOUND = 7u;
constexpr std::uint32_t ASSET_TYPE_CLIPMAP = 10u;
constexpr std::uint32_t ASSET_TYPE_CLIPMAP_PVS = 11u;
constexpr std::uint32_t ASSET_TYPE_GFX_WORLD = 16u;
constexpr std::uint32_t ASSET_TYPE_LIGHT_DEF = 17u;
constexpr std::uint32_t ASSET_TYPE_MENU_LIST = 20u;
constexpr std::uint32_t ASSET_TYPE_LOCALIZE = 22u;
constexpr std::uint32_t ASSET_TYPE_WEAPON = 23u;
constexpr std::uint32_t ASSET_TYPE_FX = 25u;
constexpr std::uint32_t ASSET_TYPE_IMPACT_FX = 26u;
constexpr std::uint32_t ASSET_TYPE_RAW_FILE = 31u;
constexpr std::uint32_t ASSET_TYPE_STRING_TABLE = 32u;

static_assert(ASSET_TYPE_PHYS_PRESET ==
    static_cast<std::uint32_t>(::ASSET_TYPE_PHYSPRESET));
static_assert(ASSET_TYPE_XANIM_PARTS ==
    static_cast<std::uint32_t>(::ASSET_TYPE_XANIMPARTS));
static_assert(ASSET_TYPE_XMODEL ==
    static_cast<std::uint32_t>(::ASSET_TYPE_XMODEL));
static_assert(ASSET_TYPE_MATERIAL ==
    static_cast<std::uint32_t>(::ASSET_TYPE_MATERIAL));
static_assert(ASSET_TYPE_TECHNIQUE_SET ==
    static_cast<std::uint32_t>(::ASSET_TYPE_TECHNIQUE_SET));
static_assert(ASSET_TYPE_IMAGE ==
    static_cast<std::uint32_t>(::ASSET_TYPE_IMAGE));
static_assert(ASSET_TYPE_GFX_WORLD ==
    static_cast<std::uint32_t>(::ASSET_TYPE_GFXWORLD));
static_assert(ASSET_TYPE_LIGHT_DEF ==
    static_cast<std::uint32_t>(::ASSET_TYPE_LIGHT_DEF));
static_assert(ASSET_TYPE_SOUND ==
    static_cast<std::uint32_t>(::ASSET_TYPE_SOUND));
static_assert(ASSET_TYPE_CLIPMAP ==
    static_cast<std::uint32_t>(::ASSET_TYPE_CLIPMAP));
static_assert(ASSET_TYPE_CLIPMAP_PVS ==
    static_cast<std::uint32_t>(::ASSET_TYPE_CLIPMAP_PVS));
static_assert(ASSET_TYPE_MENU_LIST ==
    static_cast<std::uint32_t>(::ASSET_TYPE_MENULIST));
static_assert(ASSET_TYPE_LOCALIZE ==
    static_cast<std::uint32_t>(::ASSET_TYPE_LOCALIZE_ENTRY));
static_assert(ASSET_TYPE_WEAPON ==
    static_cast<std::uint32_t>(::ASSET_TYPE_WEAPON));
static_assert(ASSET_TYPE_FX ==
    static_cast<std::uint32_t>(::ASSET_TYPE_FX));
static_assert(ASSET_TYPE_IMPACT_FX ==
    static_cast<std::uint32_t>(::ASSET_TYPE_IMPACT_FX));
static_assert(ASSET_TYPE_RAW_FILE ==
    static_cast<std::uint32_t>(::ASSET_TYPE_RAWFILE));
static_assert(ASSET_TYPE_STRING_TABLE ==
    static_cast<std::uint32_t>(::ASSET_TYPE_STRINGTABLE));

struct WorldMaterialPassState
{
    std::uint32_t vertexDeclarationToken = 0u;
    std::uint32_t vertexShaderToken = 0u;
    std::uint32_t pixelShaderToken = 0u;
    std::uint32_t argumentToken = 0u;
    std::uint32_t argumentCount = 0u;
};

struct WorldPhysGeomInfoState
{
    std::uint32_t brushReference = 0u;
    std::int32_t type = 0;
};

enum class WorldMaterialPassPhase : std::uint8_t
{
    VertexDeclaration = 0,
    VertexShader,
    PixelShader,
    Arguments,
    Complete,
};

enum class WorldFxElemPhase : std::uint8_t
{
    VisualString = 0,
    VelocitySamples,
    VisualSamples,
    VisualArray,
    Visuals,
    EffectOnImpact,
    EffectOnDeath,
    EffectEmitted,
    Trail,
};

enum class WorldXAnimPhase : std::uint8_t
{
    BoneNames = 0,
    Notify,
    DeltaPart,
    DeltaTransHeader,
    DeltaTransFrames,
    DeltaTransIndices,
    DeltaTransData,
    DeltaQuatHeader,
    DeltaQuatFrames,
    DeltaQuatIndices,
    DeltaQuatData,
    DataByte,
    DataShort,
    DataInt,
    RandomDataShort,
    RandomDataByte,
    RandomDataInt,
    Indices,
    Complete,
};

struct WorldSoundAliasState
{
    std::array<std::uint32_t, 4> stringReferences{};
    std::uint32_t soundFileReference = 0u;
    std::uint32_t curveReference = 0u;
    std::uint32_t speakerMapReference = 0u;
};

enum class WorldSoundStringTarget : std::uint8_t
{
    ListName = 0,
    AliasName,
    Subtitle,
    SecondaryAliasName,
    ChainAliasName,
    FileDirectory,
    FileName,
    CurveName,
    SpeakerMapName,
};

enum class WorldMenuTaskKind : std::uint8_t
{
    MenuList,
    MenuPointerArray,
    MenuDefPointer,
    MenuDef,
    ItemPointerArray,
    ItemDef,
    KeyHandler,
    StatementPointerArray,
    Expression,
    ListBox,
    EditField,
    Multi,
    StringTable,
    StringPointerArray,
    String,
    Material,
    Sound,
    Push0,
    Push4,
    Pop,
    InsertPointer,
    Finish,
};

struct WorldMenuTask
{
    WorldMenuTaskKind kind{};
    std::uint32_t value = 0u;
    std::uint32_t extra = 0u;
};

using WorldWeaponOperationKind = weapon_loader::OperationKind;
using WorldWeaponOperation = weapon_loader::Operation;
const auto &WEAPON_STRING_OFFSETS = weapon_loader::StringFieldOffsets();
const auto &WEAPON_XMODEL_OFFSETS = weapon_loader::XModelFieldOffsets();
const auto &WEAPON_FX_OFFSETS = weapon_loader::FxFieldOffsets();
const auto &WEAPON_MATERIAL_OFFSETS = weapon_loader::MaterialFieldOffsets();
const auto &WEAPON_SOUND_OFFSETS = weapon_loader::SoundFieldOffsets();
const auto &WEAPON_OPERATIONS = weapon_loader::Operations();
const auto &WEAPON_ACCURACY_KNOT_OFFSETS =
    weapon_loader::AccuracyKnotFieldOffsets();

constexpr std::array<const char *, RETAIL_CENSUS_ASSET_TYPE_COUNT> ASSET_NAMES = {{
    "xmodelpieces", "physpreset", "xanim", "xmodel", "material", "techset",
    "image", "sound", "sndcurve", "loaded_sound", "col_map_sp", "col_map_mp",
    "com_map", "game_map_sp", "game_map_mp", "map_ents", "gfx_map", "lightdef",
    "ui_map", "font", "menufile", "menu", "localize", "weapon",
    "snddriverglobals", "fx", "impactfx", "aitype", "mptype", "character",
    "xmodelalias", "rawfile", "stringtable",
}};

std::uint32_t ReadU32(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
        static_cast<std::uint32_t>(bytes[1]) << 8u |
        static_cast<std::uint32_t>(bytes[2]) << 16u |
        static_cast<std::uint32_t>(bytes[3]) << 24u;
}

std::uint16_t ReadU16(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::int32_t ReadS32(const std::uint8_t *bytes) noexcept
{
    return std::bit_cast<std::int32_t>(ReadU32(bytes));
}

std::int16_t ReadS16(const std::uint8_t *bytes) noexcept
{
    return std::bit_cast<std::int16_t>(ReadU16(bytes));
}

float ReadF32(const std::uint8_t *bytes) noexcept
{
    return std::bit_cast<float>(ReadU32(bytes));
}

bool SupportedImageFormat(std::uint32_t format) noexcept
{
    return format == 0x00000015u || // D3DFMT_A8R8G8B8
        format == 0x00000016u || // D3DFMT_X8R8G8B8
        format == 0x0000001cu || // D3DFMT_A8
        format == 0x00000032u || // D3DFMT_L8
        format == 0x00000033u || // D3DFMT_A8L8
        format == 0x0000004bu || // D3DFMT_D24S8
        format == 0x00000050u || // D3DFMT_D16
        format == 0x00000072u || // D3DFMT_R32F
        format == 0x31545844u || // DXT1
        format == 0x33545844u ||    // DXT3
        format == 0x35545844u;      // DXT5
}

RetailCensusError MapSourceError(SourceStreamError error) noexcept
{
    switch (error)
    {
    case SourceStreamError::None: return RetailCensusError::None;
    case SourceStreamError::ChunkTooLarge: return RetailCensusError::SourceChunkTooLarge;
    case SourceStreamError::TotalSizeLimit: return RetailCensusError::FileTooLarge;
    case SourceStreamError::Backpressure: return RetailCensusError::SourceBackpressure;
    case SourceStreamError::AlreadyFinal: return RetailCensusError::SourceAlreadyFinal;
    case SourceStreamError::AllocationFailed: return RetailCensusError::AllocationFailed;
    default: return RetailCensusError::InvalidArgument;
    }
}

RetailCensusError MapZoneError(ZoneStreamError error) noexcept
{
    switch (error)
    {
    case ZoneStreamError::None: return RetailCensusError::None;
    case ZoneStreamError::BlockOverflow: return RetailCensusError::ZoneBlockOverflow;
    case ZoneStreamError::AllocationFailed: return RetailCensusError::AllocationFailed;
    default: return RetailCensusError::ZoneStreamInvalid;
    }
}

RetailCensusError MapRegistryError(ZoneRegistryError error) noexcept
{
    return error == ZoneRegistryError::None
        ? RetailCensusError::None
        : error == ZoneRegistryError::AllocationFailed
            ? RetailCensusError::AllocationFailed
            : RetailCensusError::AssetRegistryInvalid;
}

bool ValidPublishedName(std::string_view name) noexcept
{
    if (name.empty() || name.front() == '/' || name.front() == '\\') return false;
    bool segmentStart = true;
    std::uint32_t dotCount = 0u;
    for (const unsigned char byte : name)
    {
        if (byte < 0x21u || byte > 0x7eu || byte == '\\' || byte == ':') return false;
        if (byte == '/')
        {
            if (segmentStart || dotCount == 2u) return false;
            segmentStart = true;
            dotCount = 0u;
            continue;
        }
        if (segmentStart && byte == '.') ++dotCount;
        else if (segmentStart) segmentStart = false;
    }
    return !segmentStart && dotCount != 2u;
}

bool DatabaseNamesEqual(
    std::string_view left,
    std::string_view right) noexcept
{
    const auto foldAscii = [](unsigned char value) noexcept {
        return value >= 'A' && value <= 'Z'
            ? static_cast<unsigned char>(value + ('a' - 'A'))
            : value;
    };
    return left.size() == right.size() &&
        std::equal(
            left.begin(), left.end(), right.begin(),
            [foldAscii](char lhs, char rhs) noexcept {
                return foldAscii(static_cast<unsigned char>(lhs)) ==
                    foldAscii(static_cast<unsigned char>(rhs));
            });
}

bool DatabaseNameLess(
    std::string_view left,
    std::string_view right) noexcept
{
    const auto foldAscii = [](unsigned char value) noexcept {
        return value >= 'A' && value <= 'Z'
            ? static_cast<unsigned char>(value + ('a' - 'A'))
            : value;
    };
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [foldAscii](char lhs, char rhs) noexcept {
            return foldAscii(static_cast<unsigned char>(lhs)) <
                foldAscii(static_cast<unsigned char>(rhs));
        });
}

std::uint32_t Fnv1a32(
    std::span<const std::uint8_t> bytes,
    std::uint32_t value = 2166136261u) noexcept
{
    for (const std::uint8_t byte : bytes)
    {
        value ^= byte;
        value *= 16777619u;
    }
    return value;
}

bool ValidLimits(const RetailCensusLimits &limits) noexcept
{
    return limits.maxFileBytes >= PREFIX_BYTES &&
        limits.maxSourceChunkBytes != 0u &&
        limits.maxSourceChunkBytes <= RETAIL_CENSUS_MAX_STEP_BYTES &&
        limits.maxInflatedPrefixBytes >= XFILE_BYTES + ASSET_LIST_BYTES &&
        limits.maxBlockBytes != 0u && limits.maxTotalBlockBytes != 0u &&
        limits.maxScriptStrings != 0u && limits.maxScriptStringBytes != 0u &&
        limits.maxTotalScriptStringBytes != 0u && limits.maxAssets != 0u &&
        limits.maxRegistryAssets != 0u && limits.maxRegistryAliases != 0u &&
        limits.maxRegistryNameBytes != 0u &&
        limits.maxTechniqueNameBytes != 0u && limits.maxTechniquePasses != 0u &&
        limits.maxShaderNameBytes != 0u && limits.maxShaderProgramDwords != 0u &&
        limits.maxMaterialNameBytes != 0u && limits.maxImageNameBytes != 0u &&
        limits.maxMaterialTextures != 0u && limits.maxImageResourceBytes != 0u &&
        limits.maxMaterialConstants != 0u && limits.maxMaterialStateBits != 0u &&
        limits.maxXModelNameBytes != 0u && limits.maxWorldXModels != 0u &&
        limits.maxXModelCollisionSurfaces != 0u &&
        limits.maxXModelSurfaceVertices != 0u &&
        limits.maxXModelSurfaceTriangles != 0u &&
        limits.maxXModelRigidVertLists != 0u &&
        limits.maxXModelCollisionNodes != 0u &&
        limits.maxXModelCollisionLeaves != 0u &&
        limits.maxXModelSurfacePayloadBytes != 0u &&
        limits.maxRetainedXModelRendererBytes != 0u &&
        limits.maxXModelCollisionTriangles != 0u &&
        limits.maxXModelCollisionPayloadBytes != 0u &&
        limits.maxFxEffects != 0u && limits.maxFxElemDefs != 0u &&
        limits.maxFxVisuals != 0u && limits.maxFxSampleBytes != 0u &&
        limits.maxFxTrailVertices != 0u && limits.maxFxTrailIndices != 0u &&
        limits.maxRawFiles != 0u && limits.maxRawFileNameBytes != 0u &&
        limits.maxRawFileBytes != 0u && limits.maxRetainedRawFileBytes != 0u &&
        limits.maxXAnimParts != 0u && limits.maxXAnimNameBytes != 0u &&
        limits.maxXAnimIndices != 0u && limits.maxXAnimPayloadBytes != 0u &&
        limits.maxRetainedXAnimBytes != 0u &&
        limits.maxWeapons != 0u && limits.maxWeaponStringBytes != 0u &&
        limits.maxWeaponAccuracyKnots != 0u &&
        limits.maxWeaponPayloadBytes != 0u &&
        limits.maxRetainedWeaponBytes != 0u &&
        limits.maxLocalizeEntries != 0u &&
        limits.maxLocalizeStringBytes != 0u &&
        limits.maxRetainedLocalizeBytes != 0u &&
        limits.maxSoundAliasLists != 0u &&
        limits.maxSoundAliasesPerList != 0u &&
        limits.maxSoundStringBytes != 0u &&
        limits.maxRetainedSoundBytes != 0u &&
        limits.maxClipMaps != 0u && limits.maxClipMapNameBytes != 0u &&
        limits.maxClipMapArrayElements != 0u &&
        limits.maxClipMapPayloadBytes != 0u &&
        limits.maxRetainedClipMapBytes != 0u &&
        limits.maxLightDefs != 0u && limits.maxLightDefNameBytes != 0u &&
        limits.maxSemanticTraceEntries != 0u;
}

template <typename T>
std::shared_ptr<T> AllocateFlexibleObject(std::size_t bytes) noexcept
{
    bytes = std::max(bytes, sizeof(T));
    void *memory = ::operator new(bytes, std::nothrow);
    if (!memory) return {};
    std::memset(memory, 0, bytes);
    return std::shared_ptr<T>(
        static_cast<T *>(memory),
        [](T *value) noexcept { ::operator delete(value); });
}
} // namespace

const char *RetailAssetTypeName(std::uint32_t type) noexcept
{
    return type < ASSET_NAMES.size() ? ASSET_NAMES[type] : "invalid";
}

const char *RetailCensusErrorString(RetailCensusError error) noexcept
{
    switch (error)
    {
    case RetailCensusError::None: return "none";
    case RetailCensusError::InvalidArgument: return "invalid argument";
    case RetailCensusError::InvalidStepBudget: return "invalid step budget";
    case RetailCensusError::SourceChunkTooLarge: return "source chunk exceeds limit";
    case RetailCensusError::SourceBackpressure: return "source backpressure";
    case RetailCensusError::SourceAlreadyFinal: return "source already final";
    case RetailCensusError::FileTooLarge: return "fastfile exceeds source limit";
    case RetailCensusError::PrefixTruncated: return "fastfile prefix truncated";
    case RetailCensusError::InvalidMagic: return "invalid fastfile magic";
    case RetailCensusError::AuthenticatedUnsupported: return "authenticated fastfile unsupported";
    case RetailCensusError::UnsupportedVersion: return "unsupported fastfile version";
    case RetailCensusError::InflateInit: return "zlib initialization failed";
    case RetailCensusError::InflateData: return "invalid zlib stream";
    case RetailCensusError::InflateTruncated: return "zlib prefix truncated";
    case RetailCensusError::InflatedPrefixLimit: return "inflated prefix exceeds limit";
    case RetailCensusError::RecordTruncated: return "fastfile prefix record truncated";
    case RetailCensusError::BlockSizeLimit: return "zone block exceeds limit";
    case RetailCensusError::TotalBlockSizeLimit: return "zone block total exceeds limit";
    case RetailCensusError::ScriptStringCountInvalid: return "invalid script string count";
    case RetailCensusError::ScriptStringCountLimit: return "script string count exceeds limit";
    case RetailCensusError::ScriptStringArrayInvalid: return "invalid script string array token";
    case RetailCensusError::ScriptStringReferenceUnsupported: return "unsupported script string reference";
    case RetailCensusError::ScriptStringTooLong: return "script string exceeds limit";
    case RetailCensusError::ScriptStringBytesLimit: return "script string bytes exceed limit";
    case RetailCensusError::AssetCountInvalid: return "invalid asset count";
    case RetailCensusError::AssetCountLimit: return "asset count exceeds limit";
    case RetailCensusError::AssetArrayInvalid: return "invalid asset array token";
    case RetailCensusError::AssetTypeInvalid: return "invalid asset type";
    case RetailCensusError::ZoneStreamInvalid: return "invalid logical zone stream state";
    case RetailCensusError::ZoneBlockOverflow: return "logical zone block overflow";
    case RetailCensusError::FirstAssetUnsupported: return "first asset is not an inline technique set";
    case RetailCensusError::AssetPrefixUnsupported: return "fastfile does not have the supported two-techset/material prefix";
    case RetailCensusError::AssetRegistryInvalid: return "retail asset registry state is invalid";
    case RetailCensusError::TechniqueSetLayoutUnsupported: return "unsupported technique-set layout";
    case RetailCensusError::TechniqueSetNameInvalid: return "invalid technique-set name";
    case RetailCensusError::TechniqueSetNameTooLong: return "technique-set name exceeds limit";
    case RetailCensusError::TechniqueReferenceUnsupported: return "unsupported technique reference";
    case RetailCensusError::TechniqueLayoutUnsupported: return "unsupported technique layout";
    case RetailCensusError::TechniquePassCountLimit: return "technique pass count exceeds limit";
    case RetailCensusError::MaterialPassUnsupported: return "unsupported material pass layout";
    case RetailCensusError::VertexDeclarationUnsupported: return "unsupported vertex declaration";
    case RetailCensusError::VertexShaderLayoutUnsupported: return "unsupported vertex shader layout";
    case RetailCensusError::VertexShaderNameInvalid: return "invalid vertex shader name";
    case RetailCensusError::VertexShaderNameTooLong: return "vertex shader name exceeds limit";
    case RetailCensusError::ShaderProgramSizeInvalid: return "invalid vertex shader program size";
    case RetailCensusError::ShaderProgramSizeLimit: return "vertex shader program exceeds limit";
    case RetailCensusError::ShaderProgramSignatureInvalid: return "invalid Direct3D vertex shader signature";
    case RetailCensusError::PixelShaderLayoutUnsupported: return "unsupported pixel shader layout";
    case RetailCensusError::ShaderContractInvalid: return "invalid Direct3D shader contract";
    case RetailCensusError::ShaderSubstitutionUnsupported: return "no WebGL2 shader substitution matches";
    case RetailCensusError::ShaderArgumentLayoutUnsupported: return "unsupported shader argument layout";
    case RetailCensusError::TechniqueNameInvalid: return "invalid technique name";
    case RetailCensusError::TechniqueAliasInvalid: return "material technique-set dependency alias is invalid";
    case RetailCensusError::MaterialLayoutUnsupported: return "unsupported material layout";
    case RetailCensusError::MaterialNameInvalid: return "invalid material name";
    case RetailCensusError::MaterialNameTooLong: return "material name exceeds limit";
    case RetailCensusError::MaterialTechniqueSetInvalid: return "material technique-set reference is invalid";
    case RetailCensusError::MaterialTextureCountLimit: return "material texture count exceeds limit";
    case RetailCensusError::MaterialTextureLayoutUnsupported: return "unsupported material texture layout";
    case RetailCensusError::ImageLayoutUnsupported: return "unsupported GfxImage layout";
    case RetailCensusError::ImageNameInvalid: return "invalid GfxImage name";
    case RetailCensusError::ImageNameTooLong: return "GfxImage name exceeds limit";
    case RetailCensusError::ImageResourceSizeInvalid: return "invalid GfxImage resource size";
    case RetailCensusError::ImageResourceSizeLimit: return "GfxImage resource exceeds limit";
    case RetailCensusError::MaterialStateBitsUnsupported: return "unsupported material state-bits layout";
    case RetailCensusError::GfxWorldMissing: return "fastfile contains no GfxWorld asset";
    case RetailCensusError::XModelLayoutUnsupported: return "unsupported XModel layout";
    case RetailCensusError::XModelNameInvalid: return "invalid XModel name";
    case RetailCensusError::XModelNameTooLong: return "XModel name exceeds limit";
    case RetailCensusError::XModelCountInvalid: return "invalid XModel count";
    case RetailCensusError::XModelCollectionLimit: return "XModel collection limit exceeded";
    case RetailCensusError::XModelBoundsInvalid: return "invalid XModel bounds";
    case RetailCensusError::XModelScriptStringInvalid: return "invalid XModel bone script string";
    case RetailCensusError::XModelScriptStringAliasInvalid: return "invalid XModel bone script-string array alias";
    case RetailCensusError::XModelArrayAliasInvalid: return "invalid XModel typed-array alias";
    case RetailCensusError::XModelDependencyUnsupported: return "unsupported XModel dependency";
    case RetailCensusError::XSurfaceLayoutUnsupported: return "unsupported XSurface layout";
    case RetailCensusError::XSurfaceCountInvalid: return "invalid XSurface count";
    case RetailCensusError::XSurfacePayloadLimit: return "XSurface payload exceeds limit";
    case RetailCensusError::XSurfaceCollisionInvalid: return "invalid XSurface collision tree";
    case RetailCensusError::XModelMaterialAliasInvalid: return "invalid XModel material dependency alias";
    case RetailCensusError::XModelImageAliasInvalid: return "invalid XModel GfxImage dependency alias";
    case RetailCensusError::XModelCollisionInvalid: return "invalid XModel collision surface";
    case RetailCensusError::XModelCollisionPayloadLimit: return "XModel collision payload exceeds limit";
    case RetailCensusError::XModelBoneInfoInvalid: return "invalid XModel bone info";
    case RetailCensusError::XModelPhysicsUnsupported: return "unsupported XModel physics dependency";
    case RetailCensusError::PhysPresetLayoutUnsupported: return "unsupported physics preset layout";
    case RetailCensusError::PhysPresetNameInvalid: return "invalid physics preset name";
    case RetailCensusError::PhysPresetNameTooLong: return "physics preset name exceeds limit";
    case RetailCensusError::PhysPresetSoundAliasInvalid: return "invalid physics preset sound alias prefix";
    case RetailCensusError::PhysPresetSoundAliasTooLong: return "physics preset sound alias prefix exceeds limit";
    case RetailCensusError::PhysPresetValuesInvalid: return "invalid physics preset values";
    case RetailCensusError::PhysPresetAliasInvalid: return "invalid physics preset dependency alias";
    case RetailCensusError::PhysGeomLayoutUnsupported: return "unsupported physics geometry layout";
    case RetailCensusError::PhysGeomCountLimit: return "physics geometry count exceeds limit";
    case RetailCensusError::PhysGeomValuesInvalid: return "invalid physics geometry values";
    case RetailCensusError::PhysGeomBrushInvalid: return "invalid physics geometry brush";
    case RetailCensusError::PhysGeomPayloadLimit: return "physics geometry payload exceeds limit";
    case RetailCensusError::FxEffectLayoutUnsupported: return "unsupported FX effect layout";
    case RetailCensusError::FxEffectNameInvalid: return "invalid FX effect name";
    case RetailCensusError::FxEffectNameTooLong: return "FX effect name exceeds limit";
    case RetailCensusError::FxEffectCountLimit: return "FX effect element count exceeds limit";
    case RetailCensusError::FxElemLayoutUnsupported: return "unsupported FX element layout";
    case RetailCensusError::FxElemSampleLimit: return "FX element samples exceed limit";
    case RetailCensusError::FxElemVisualInvalid: return "invalid FX element visual dependency";
    case RetailCensusError::FxStringReferenceInvalid: return "invalid FX string reference";
    case RetailCensusError::FxTrailInvalid: return "invalid FX trail definition";
    case RetailCensusError::FxMaterialUnsupported: return "unsupported FX material dependency";
    case RetailCensusError::RawFileLayoutUnsupported: return "unsupported RawFile layout";
    case RetailCensusError::RawFileNameInvalid: return "invalid RawFile name";
    case RetailCensusError::RawFileNameTooLong: return "RawFile name exceeds limit";
    case RetailCensusError::RawFileSizeInvalid: return "invalid RawFile size";
    case RetailCensusError::RawFilePayloadLimit: return "RawFile payload exceeds limit";
    case RetailCensusError::RawFileCollectionLimit: return "RawFile collection limit exceeded";
    case RetailCensusError::XAnimLayoutUnsupported: return "unsupported XAnimParts layout";
    case RetailCensusError::XAnimNameInvalid: return "invalid XAnimParts name";
    case RetailCensusError::XAnimNameTooLong: return "XAnimParts name exceeds limit";
    case RetailCensusError::XAnimCollectionLimit: return "XAnimParts collection limit exceeded";
    case RetailCensusError::XAnimScriptStringInvalid: return "invalid XAnimParts script string";
    case RetailCensusError::XAnimPayloadLimit: return "XAnimParts payload exceeds limit";
    case RetailCensusError::XAnimDeltaInvalid: return "invalid XAnimParts delta payload";
    case RetailCensusError::XAnimAliasInvalid: return "invalid XAnimParts pointer alias";
    case RetailCensusError::WeaponLayoutUnsupported: return "unsupported WeaponDef layout";
    case RetailCensusError::WeaponNameInvalid: return "invalid WeaponDef internal name";
    case RetailCensusError::WeaponStringInvalid: return "invalid WeaponDef string pointer";
    case RetailCensusError::WeaponStringTooLong: return "WeaponDef string exceeds limit";
    case RetailCensusError::WeaponCollectionLimit: return "WeaponDef collection limit exceeded";
    case RetailCensusError::WeaponScriptStringInvalid: return "invalid WeaponDef script string";
    case RetailCensusError::WeaponDependencyUnsupported: return "WeaponDef canonical dependency is not published";
    case RetailCensusError::WeaponSoundNameInvalid: return "invalid WeaponDef sound-name indirection";
    case RetailCensusError::WeaponSoundNameTooLong: return "WeaponDef sound name exceeds limit";
    case RetailCensusError::WeaponSoundLookupFailed: return "WeaponDef sound name was not found";
    case RetailCensusError::WeaponAccuracyInvalid: return "invalid WeaponDef accuracy graph";
    case RetailCensusError::WeaponPayloadLimit: return "WeaponDef payload exceeds limit";
    case RetailCensusError::WeaponAliasInvalid: return "invalid WeaponDef pointer alias";
    case RetailCensusError::LocalizeLayoutUnsupported: return "unsupported LocalizeEntry layout";
    case RetailCensusError::LocalizeCollectionLimit: return "LocalizeEntry collection limit exceeded";
    case RetailCensusError::LocalizeStringInvalid: return "invalid LocalizeEntry string";
    case RetailCensusError::LocalizeStringTooLong: return "LocalizeEntry string exceeds limit";
    case RetailCensusError::LocalizePayloadLimit: return "LocalizeEntry payload exceeds limit";
    case RetailCensusError::LocalizeAliasInvalid: return "invalid LocalizeEntry pointer alias";
    case RetailCensusError::SoundAliasLayoutUnsupported: return "unsupported sound alias layout";
    case RetailCensusError::SoundAliasCollectionLimit: return "sound alias list count exceeds limit";
    case RetailCensusError::SoundAliasCountLimit: return "sound aliases per list exceed limit";
    case RetailCensusError::SoundAliasStringInvalid: return "invalid sound alias string";
    case RetailCensusError::SoundAliasStringTooLong: return "sound alias string exceeds limit";
    case RetailCensusError::SoundAliasDependencyUnsupported: return "unsupported sound alias dependency";
    case RetailCensusError::SoundAliasPayloadLimit: return "sound alias payload exceeds limit";
    case RetailCensusError::SoundAliasCatalogPublishFailed: return "sound alias catalog publication failed";
    case RetailCensusError::ClipMapLayoutUnsupported: return "unsupported clipMap_t layout";
    case RetailCensusError::ClipMapCollectionLimit: return "ClipMap collection exceeds limit";
    case RetailCensusError::ClipMapNameInvalid: return "invalid ClipMap name";
    case RetailCensusError::ClipMapNameTooLong: return "ClipMap name exceeds limit";
    case RetailCensusError::ClipMapCountInvalid: return "invalid ClipMap child count";
    case RetailCensusError::ClipMapPayloadLimit: return "ClipMap payload exceeds limit";
    case RetailCensusError::ClipMapPointerInvalid: return "invalid ClipMap pointer";
    case RetailCensusError::ClipMapDependencyUnsupported: return "ClipMap dependency is not published";
    case RetailCensusError::ClipMapAliasInvalid: return "invalid ClipMap alias";
    case RetailCensusError::ComWorldLayoutUnsupported: return "unsupported ComWorld layout";
    case RetailCensusError::ComWorldCollectionLimit: return "ComWorld collection exceeds limit";
    case RetailCensusError::ComWorldNameInvalid: return "invalid ComWorld name";
    case RetailCensusError::ComWorldNameTooLong: return "ComWorld name exceeds limit";
    case RetailCensusError::ComWorldLightCountInvalid: return "invalid ComWorld primary-light count";
    case RetailCensusError::ComWorldLightNameInvalid: return "invalid ComPrimaryLight defName";
    case RetailCensusError::ComWorldLightNameTooLong: return "ComPrimaryLight defName exceeds limit";
    case RetailCensusError::ComWorldStringBytesLimit: return "ComWorld strings exceed aggregate limit";
    case RetailCensusError::ComWorldPayloadLimit: return "ComWorld payload exceeds limit";
    case RetailCensusError::ComWorldAliasInvalid: return "invalid ComWorld alias";
    case RetailCensusError::LightDefLayoutUnsupported: return "unsupported GfxLightDef layout";
    case RetailCensusError::LightDefCollectionLimit: return "GfxLightDef collection exceeds limit";
    case RetailCensusError::LightDefNameInvalid: return "invalid GfxLightDef name";
    case RetailCensusError::LightDefNameTooLong: return "GfxLightDef name exceeds limit";
    case RetailCensusError::LightDefImageInvalid: return "GfxLightDef image dependency is not published";
    case RetailCensusError::LightDefAliasInvalid: return "invalid GfxLightDef alias";
    case RetailCensusError::PostXModelAssetUnsupported:
        return "asset after the first XModel is not an inline technique set";
    case RetailCensusError::SemanticTraceLimit:
        return "database semantic trace entry limit exceeded";
    case RetailCensusError::AllocationFailed: return "allocation failed";
    }
    return "unknown retail census error";
}

const char *RetailCensusStageString(RetailCensusStage stage) noexcept
{
    switch (stage)
    {
    case RetailCensusStage::NotStarted: return "not-started";
    case RetailCensusStage::Prefix: return "prefix";
    case RetailCensusStage::Inflate: return "inflate";
    case RetailCensusStage::XFile: return "xfile";
    case RetailCensusStage::AssetList: return "asset-list";
    case RetailCensusStage::ScriptStringPointers: return "script-string-pointers";
    case RetailCensusStage::ScriptStrings: return "script-strings";
    case RetailCensusStage::AssetTable: return "asset-table";
    case RetailCensusStage::TechniqueSet: return "technique-set";
    case RetailCensusStage::TechniqueSetName: return "technique-set-name";
    case RetailCensusStage::Technique: return "technique";
    case RetailCensusStage::MaterialPasses: return "material-passes";
    case RetailCensusStage::VertexDeclaration: return "vertex-declaration";
    case RetailCensusStage::VertexShader: return "vertex-shader";
    case RetailCensusStage::VertexShaderName: return "vertex-shader-name";
    case RetailCensusStage::VertexShaderProgram: return "vertex-shader-program";
    case RetailCensusStage::PixelShader: return "pixel-shader";
    case RetailCensusStage::PixelShaderProgram: return "pixel-shader-program";
    case RetailCensusStage::ShaderArguments: return "shader-arguments";
    case RetailCensusStage::TechniqueName: return "technique-name";
    case RetailCensusStage::SecondTechniqueSet: return "material-technique-set";
    case RetailCensusStage::SecondTechniqueSetName: return "material-technique-set-name";
    case RetailCensusStage::SecondTechnique: return "material-technique";
    case RetailCensusStage::SecondMaterialPasses: return "material-technique-passes";
    case RetailCensusStage::SecondVertexShader: return "material-vertex-shader";
    case RetailCensusStage::SecondVertexShaderProgram: return "material-vertex-program";
    case RetailCensusStage::SecondPixelShader: return "material-pixel-shader";
    case RetailCensusStage::SecondPixelShaderProgram: return "material-pixel-program";
    case RetailCensusStage::SecondShaderArguments: return "material-shader-arguments";
    case RetailCensusStage::SecondTechniqueName: return "material-technique-name";
    case RetailCensusStage::Material: return "material";
    case RetailCensusStage::MaterialName: return "material-name";
    case RetailCensusStage::MaterialTextureTable: return "material-texture-table";
    case RetailCensusStage::Image: return "gfx-image";
    case RetailCensusStage::ImageName: return "gfx-image-name";
    case RetailCensusStage::ImageLoadDef: return "gfx-image-load-def";
    case RetailCensusStage::ImageResource: return "gfx-image-resource";
    case RetailCensusStage::MaterialStateBits: return "material-state-bits";
    case RetailCensusStage::WorldTechniqueSet: return "world-technique-set";
    case RetailCensusStage::WorldTechniqueSetName: return "world-technique-set-name";
    case RetailCensusStage::WorldXModel: return "world-xmodel";
    case RetailCensusStage::WorldXModelName: return "world-xmodel-name";
    case RetailCensusStage::WorldXModelBoneNames: return "world-xmodel-bone-names";
    case RetailCensusStage::WorldXModelParentList: return "world-xmodel-parent-list";
    case RetailCensusStage::WorldXModelQuats: return "world-xmodel-quats";
    case RetailCensusStage::WorldXModelTrans: return "world-xmodel-trans";
    case RetailCensusStage::WorldXModelPartClassification: return "world-xmodel-part-classification";
    case RetailCensusStage::WorldXModelBaseMat: return "world-xmodel-base-mat";
    case RetailCensusStage::WorldXModelSurfaceHeaders: return "world-xmodel-surface-headers";
    case RetailCensusStage::WorldXModelSurfaceBlendInfo: return "world-xmodel-surface-blend-info";
    case RetailCensusStage::WorldXModelSurfaceVertices: return "world-xmodel-surface-vertices";
    case RetailCensusStage::WorldXModelSurfaceVertLists: return "world-xmodel-surface-vert-lists";
    case RetailCensusStage::WorldXModelSurfaceCollisionTree: return "world-xmodel-surface-collision-tree";
    case RetailCensusStage::WorldXModelSurfaceCollisionNodes: return "world-xmodel-surface-collision-nodes";
    case RetailCensusStage::WorldXModelSurfaceCollisionLeaves: return "world-xmodel-surface-collision-leaves";
    case RetailCensusStage::WorldXModelSurfaceIndices: return "world-xmodel-surface-indices";
    case RetailCensusStage::WorldXModelMaterialHandles: return "world-xmodel-material-handles";
    case RetailCensusStage::WorldXModelMaterial: return "world-xmodel-material";
    case RetailCensusStage::WorldXModelMaterialName: return "world-xmodel-material-name";
    case RetailCensusStage::WorldXModelMaterialTextures: return "world-xmodel-material-textures";
    case RetailCensusStage::WorldXModelImage: return "world-xmodel-image";
    case RetailCensusStage::WorldXModelImageName: return "world-xmodel-image-name";
    case RetailCensusStage::WorldXModelImageLoadDef: return "world-xmodel-image-load-def";
    case RetailCensusStage::WorldXModelImageResource: return "world-xmodel-image-resource";
    case RetailCensusStage::WorldXModelMaterialConstants: return "world-xmodel-material-constants";
    case RetailCensusStage::WorldXModelMaterialStateBits: return "world-xmodel-material-state-bits";
    case RetailCensusStage::WorldXModelCollisionSurfaces: return "world-xmodel-collision-surfaces";
    case RetailCensusStage::WorldXModelCollisionTriangles: return "world-xmodel-collision-triangles";
    case RetailCensusStage::WorldXModelBoneInfo: return "world-xmodel-bone-info";
    case RetailCensusStage::WorldXModelPhysPreset: return "world-xmodel-phys-preset";
    case RetailCensusStage::WorldPhysPreset: return "world-phys-preset";
    case RetailCensusStage::WorldPhysPresetName: return "world-phys-preset-name";
    case RetailCensusStage::WorldPhysPresetSoundAlias: return "world-phys-preset-sound-alias";
    case RetailCensusStage::WorldXModelPhysGeoms: return "world-xmodel-phys-geoms";
    case RetailCensusStage::WorldPhysGeomList: return "world-phys-geom-list";
    case RetailCensusStage::WorldPhysGeomInfos: return "world-phys-geom-infos";
    case RetailCensusStage::WorldPhysGeomBrush: return "world-phys-geom-brush";
    case RetailCensusStage::WorldPhysGeomBrushSides: return "world-phys-geom-brush-sides";
    case RetailCensusStage::WorldPhysGeomBrushSidePlane: return "world-phys-geom-brush-side-plane";
    case RetailCensusStage::WorldPhysGeomBrushAdjacent: return "world-phys-geom-brush-adjacent";
    case RetailCensusStage::WorldPhysGeomBrushPlanes: return "world-phys-geom-brush-planes";
    case RetailCensusStage::WorldXModelPublish: return "world-xmodel-publish";
    case RetailCensusStage::WorldMaterialTechnique: return "world-material-technique";
    case RetailCensusStage::WorldMaterialPasses: return "world-material-passes";
    case RetailCensusStage::WorldMaterialVertexDeclaration: return "world-material-vertex-declaration";
    case RetailCensusStage::WorldMaterialVertexShader: return "world-material-vertex-shader";
    case RetailCensusStage::WorldMaterialVertexShaderName: return "world-material-vertex-shader-name";
    case RetailCensusStage::WorldMaterialVertexShaderProgram: return "world-material-vertex-program";
    case RetailCensusStage::WorldMaterialPixelShader: return "world-material-pixel-shader";
    case RetailCensusStage::WorldMaterialPixelShaderName: return "world-material-pixel-shader-name";
    case RetailCensusStage::WorldMaterialPixelShaderProgram: return "world-material-pixel-program";
    case RetailCensusStage::WorldMaterialShaderArguments: return "world-material-shader-arguments";
    case RetailCensusStage::WorldMaterialLiteralConstant: return "world-material-literal-constant";
    case RetailCensusStage::WorldMaterialTechniqueName: return "world-material-technique-name";
    case RetailCensusStage::WorldFxEffect: return "world-fx-effect";
    case RetailCensusStage::WorldFxEffectName: return "world-fx-effect-name";
    case RetailCensusStage::WorldFxElemHeaders: return "world-fx-element-headers";
    case RetailCensusStage::WorldFxElemVelocitySamples: return "world-fx-velocity-samples";
    case RetailCensusStage::WorldFxElemVisualSamples: return "world-fx-visual-samples";
    case RetailCensusStage::WorldFxElemVisualArray: return "world-fx-visual-array";
    case RetailCensusStage::WorldFxElemVisuals: return "world-fx-visuals";
    case RetailCensusStage::WorldFxString: return "world-fx-string";
    case RetailCensusStage::WorldFxTrail: return "world-fx-trail";
    case RetailCensusStage::WorldFxTrailVertices: return "world-fx-trail-vertices";
    case RetailCensusStage::WorldFxTrailIndices: return "world-fx-trail-indices";
    case RetailCensusStage::WorldFxPublish: return "world-fx-publish";
    case RetailCensusStage::WorldFxImpactTable: return "world-fx-impact-table";
    case RetailCensusStage::WorldFxImpactName: return "world-fx-impact-name";
    case RetailCensusStage::WorldFxImpactEntries: return "world-fx-impact-entries";
    case RetailCensusStage::WorldFxImpactPublish: return "world-fx-impact-publish";
    case RetailCensusStage::WorldMenuTasks: return "world-menu-tasks";
    case RetailCensusStage::WorldRawFile: return "world-rawfile";
    case RetailCensusStage::WorldRawFileName: return "world-rawfile-name";
    case RetailCensusStage::WorldRawFileBuffer: return "world-rawfile-buffer";
    case RetailCensusStage::WorldRawFilePublish: return "world-rawfile-publish";
    case RetailCensusStage::WorldXAnimParts: return "world-xanim-parts";
    case RetailCensusStage::WorldXAnimName: return "world-xanim-name";
    case RetailCensusStage::WorldXAnimPayload: return "world-xanim-payload";
    case RetailCensusStage::WorldXAnimPublish: return "world-xanim-publish";
    case RetailCensusStage::WorldWeaponDef: return "world-weapon-def";
    case RetailCensusStage::WorldWeaponString: return "world-weapon-string";
    case RetailCensusStage::WorldWeaponSoundNameCell: return "world-weapon-sound-name-cell";
    case RetailCensusStage::WorldWeaponSoundName: return "world-weapon-sound-name";
    case RetailCensusStage::WorldWeaponBounceSoundCells: return "world-weapon-bounce-sound-cells";
    case RetailCensusStage::WorldWeaponAccuracyKnots: return "world-weapon-accuracy-knots";
    case RetailCensusStage::WorldWeaponPublish: return "world-weapon-publish";
    case RetailCensusStage::WorldLocalizeEntry: return "world-localize-entry";
    case RetailCensusStage::WorldLocalizeValue: return "world-localize-value";
    case RetailCensusStage::WorldLocalizeName: return "world-localize-name";
    case RetailCensusStage::WorldLocalizePublish: return "world-localize-publish";
    case RetailCensusStage::WorldSoundAliasList: return "world-sound-alias-list";
    case RetailCensusStage::WorldSoundAliasListName: return "world-sound-alias-list-name";
    case RetailCensusStage::WorldSoundAliasHeaders: return "world-sound-alias-headers";
    case RetailCensusStage::WorldSoundAliasString: return "world-sound-alias-string";
    case RetailCensusStage::WorldSoundAliasFile: return "world-sound-alias-file";
    case RetailCensusStage::WorldSoundLoadedSound: return "world-sound-loaded-sound";
    case RetailCensusStage::WorldSoundLoadedSoundName: return "world-sound-loaded-sound-name";
    case RetailCensusStage::WorldSoundLoadedSoundData: return "world-sound-loaded-sound-data";
    case RetailCensusStage::WorldSoundAliasCurve: return "world-sound-alias-curve";
    case RetailCensusStage::WorldSoundAliasSpeakerMap: return "world-sound-alias-speaker-map";
    case RetailCensusStage::WorldSoundAliasPublish: return "world-sound-alias-publish";
    case RetailCensusStage::WorldClipMap: return "world-clipmap";
    case RetailCensusStage::WorldComWorld: return "world-comworld";
    case RetailCensusStage::WorldLightDef: return "world-lightdef";
    case RetailCensusStage::AssetBoundary: return "asset-boundary";
    case RetailCensusStage::Failed: return "failed";
    }
    return "unknown";
}

struct RetailFastfileCensusJob::Impl final : RetailLoadContext
{
    BoundedSourceStream source;
    RetailCensusMode mode = RetailCensusMode::CodePostGfxMaterial;
    bool prerequisiteZone = false;
    RetailCensusLimits limits{};
    RetailSoundAliasLookup soundLookup{};
    std::array<std::uint8_t, PREFIX_BYTES> prefix{};
    std::uint32_t prefixCount = 0u;
    std::vector<std::uint8_t> inflated;
    z_stream stream{};
    bool streamInitialized = false;
    bool streamEnded = false;
    std::size_t cursor = 0u;
    std::size_t assetTableOffset = 0u;
    std::vector<std::uint32_t> scriptTokens;
    std::vector<std::string> scriptStrings;
    std::vector<std::uint32_t> scriptStringBlock4Offsets;
    // Native XString offset aliases are zone-wide, not scoped to the asset
    // family that first loaded the character payload.
    std::unordered_map<std::uint32_t, std::shared_ptr<std::string>>
        block4StringAliases;
    // Preserve serialized offset-token identity as well as locally computed
    // block offsets. This lets later assets share a native XString even when
    // its first appearance was through another already-patched pointer.
    std::unordered_map<std::uint32_t, std::shared_ptr<std::string>>
        zoneStringTokenAliases;
    // Relates serialized native block-4 string offsets to the local bounded
    // arena offset that owns the same payload. Prerequisite-only traversal can
    // omit allocations from uncompiled families, so this correspondence is
    // ordered but is not necessarily a single constant bias.
    std::unordered_map<std::uint32_t,
        std::pair<std::uint32_t, std::shared_ptr<std::string>>>
        nativeBlock4StringAliases;
    // Sound list keys frequently point back to XStrings first loaded by an
    // earlier prerequisite asset family. Preserve the ordered native/local
    // correspondence for that DB key independently from child sound strings.
    std::unordered_map<std::uint32_t,
        std::pair<std::uint32_t, std::shared_ptr<std::string>>>
        nativeBlock4SoundListAliases;
    // XStringPtr loads publish a pointer cell and a character payload. Keep
    // their payloads indexed separately so later direct XString references
    // can converge on the same zone-owned string while prerequisite traversal
    // still has temporary offset-accounting gaps.
    std::unordered_map<std::uint32_t, std::shared_ptr<std::string>>
        indirectBlock4StringAliases;
    // Models the native block-4 cells patched by Load_MaterialHandle while
    // traversing FxElemVisuals. Later normal offset tokens dereference these
    // cells; they do not name or instantiate another Material asset.
    std::unordered_map<std::uint32_t, std::uint32_t>
        block4MaterialVisualAliases;
    std::int32_t block4NativeOffsetBias = 0;
    bool block4NativeOffsetBiasKnown = false;
    std::uint32_t block4NativeOffsetBiasStart = UINT32_MAX;
    std::vector<std::pair<std::uint32_t, std::int32_t>> block4BiasSegments;
    std::uint32_t scriptIndex = 0u;
    std::uint32_t assetIndex = 0u;
    std::size_t recordVisited = 0u;
    ZoneStreamMachine arenas;
    bool arenasInitialized = false;
    bool scriptScopesOpen = false;
    bool assetScopeOpen = false;
    std::array<std::uint32_t, 34> techniqueTokens{};
    std::vector<std::uint32_t> worldAssetTypes;
    std::vector<std::uint32_t> worldAssetReferences;
    std::uint32_t worldBodyIndex = 0u;
    ZoneSpan worldTopLevelAliasSlot{};
    ZoneSpan worldXModelAliasSlot{};
    bool worldXModelNested = false;
    std::uint32_t worldXModelNestedFxVisualIndex = 0u;
    ZoneSpan worldPhysPresetInsertAliasSlot{};
    bool worldPhysPresetHasInsertAlias = false;
    std::vector<WorldPhysGeomInfoState> worldPhysGeomInfos;
    std::vector<std::uint32_t> worldPhysGeomSidePlaneReferences;
    std::uint32_t worldPhysGeomIndex = 0u;
    std::uint32_t worldPhysGeomInfosReference = 0u;
    std::uint32_t worldPhysGeomSideIndex = 0u;
    std::uint32_t worldPhysGeomSidesReference = 0u;
    std::uint32_t worldPhysGeomAdjacentReference = 0u;
    std::uint32_t worldPhysGeomPlanesReference = 0u;
    std::uint32_t worldPhysGeomSideCount = 0u;
    std::uint32_t worldPhysGeomEdgeCount = 0u;
    std::size_t worldXModelIndex = 0u;
    std::uint32_t worldSurfaceIndex = 0u;
    std::uint32_t retainedLodVertices = 0u;
    std::uint32_t retainedLodTriangles = 0u;
    std::uint64_t retainedRendererPayloadBytes = 0u;
    std::uint32_t worldRigidVertListIndex = 0u;
    std::uint32_t worldMaterialIndex = 0u;
    std::uint32_t worldTextureIndex = 0u;
    std::uint32_t worldCollisionSurfaceIndex = 0u;
    std::uint32_t worldImageResourceBytes = 0u;
    ZoneSpan worldMaterialAliasSlot{};
    ZoneSpan worldMaterialInsertAliasSlot{};
    bool worldMaterialHasInsertAlias = false;
    bool worldMaterialOwnedByFx = false;
    bool worldMaterialOwnedByWeapon = false;
    bool worldMaterialOwnedByMenu = false;
    bool worldMaterialTopLevel = false;
    std::uint32_t worldMaterialNameReference = 0u;
    bool worldMaterialNameResolved = true;
    std::uint32_t worldMaterialPublicationSerial = 0u;
    std::uint32_t worldMaterialTopLevelAssetIndex = 0u;
    ZoneSpan worldImageAliasSlot{};
    ZoneSpan worldImageInsertAliasSlot{};
    bool worldImageHasInsertAlias = false;
    bool worldImageNameResolved = true;
    std::uint32_t worldTechniqueSlotIndex = 0u;
    bool worldTechniqueSetNameResolved = true;
    std::uint32_t worldMaterialPassIndex = 0u;
    WorldMaterialPassPhase worldMaterialPassPhase =
        WorldMaterialPassPhase::VertexDeclaration;
    std::vector<WorldMaterialPassState> worldMaterialPasses;
    std::uint32_t worldMaterialTechniqueNameToken = 0u;
    std::uint32_t worldMaterialShaderNameToken = 0u;
    std::uint32_t worldMaterialShaderProgramBytes = 0u;
    std::uint32_t worldMaterialArgumentBytes = 0u;
    std::vector<std::uint32_t> worldMaterialLiteralTokens;
    std::uint32_t worldMaterialLiteralIndex = 0u;
    ZoneSpan worldFxAliasSlot{};
    ZoneSpan worldFxStringSpan{};
    ZoneSpan worldRawFileAliasSlot{};
    ZoneSpan worldXAnimAliasSlot{};
    ZoneSpan worldXAnimInsertAliasSlot{};
    bool worldXAnimHasInsertAlias = false;
    std::size_t worldXAnimIndex = 0u;
    WorldXAnimPhase worldXAnimPhase = WorldXAnimPhase::BoneNames;
    std::uint32_t worldXAnimNameReference = 0u;
    std::uint32_t worldXAnimNamesReference = 0u;
    std::uint32_t worldXAnimNotifyReference = 0u;
    std::uint32_t worldXAnimDeltaReference = 0u;
    std::uint32_t worldXAnimDataByteReference = 0u;
    std::uint32_t worldXAnimDataShortReference = 0u;
    std::uint32_t worldXAnimDataIntReference = 0u;
    std::uint32_t worldXAnimRandomShortReference = 0u;
    std::uint32_t worldXAnimRandomByteReference = 0u;
    std::uint32_t worldXAnimRandomIntReference = 0u;
    std::uint32_t worldXAnimIndicesReference = 0u;
    std::uint32_t worldXAnimDeltaTransReference = 0u;
    std::uint32_t worldXAnimDeltaQuatReference = 0u;
    std::uint32_t worldXAnimDeltaTransFramesReference = 0u;
    std::uint32_t worldXAnimDeltaQuatFramesReference = 0u;
    std::uint16_t worldXAnimDeltaTransSize = 0u;
    std::uint16_t worldXAnimDeltaQuatSize = 0u;
    bool worldXAnimDeltaTransSmall = false;
    std::uint64_t retainedXAnimBytes = 0u;
    ZoneSpan worldWeaponAliasSlot{};
    ZoneSpan worldWeaponInsertAliasSlot{};
    bool worldWeaponHasInsertAlias = false;
    std::size_t worldWeaponIndex = 0u;
    std::size_t worldWeaponOperationIndex = 0u;
    std::array<std::uint32_t, 48> worldWeaponStringReferences{};
    std::array<std::uint32_t, 38> worldWeaponXModelReferences{};
    std::array<std::uint32_t, 10> worldWeaponFxReferences{};
    std::array<std::uint32_t, 8> worldWeaponMaterialReferences{};
    std::array<std::uint32_t, 48> worldWeaponSoundReferences{};
    std::uint32_t worldWeaponBounceSoundReference = 0u;
    std::array<std::uint32_t, 29> worldWeaponBounceSoundReferences{};
    std::uint32_t worldWeaponSoundStringReference = 0u;
    std::uint32_t worldWeaponSoundIndex = 0u;
    bool worldWeaponSoundIsBounce = false;
    bool worldWeaponBounceActive = false;
    std::uint32_t worldWeaponBounceIndex = 0u;
    std::array<std::uint32_t, 4> worldWeaponAccuracyReferences{};
    std::uint64_t retainedWeaponBytes = 0u;
    ZoneSpan worldLocalizeAliasSlot{};
    ZoneSpan worldLocalizeInsertAliasSlot{};
    bool worldLocalizeHasInsertAlias = false;
    std::size_t worldLocalizeIndex = 0u;
    std::uint32_t worldLocalizeValueReference = 0u;
    std::uint32_t worldLocalizeNameReference = 0u;
    std::uint64_t retainedLocalizeBytes = 0u;
    ZoneSpan worldSoundAliasSlot{};
    ZoneSpan worldSoundInsertAliasSlot{};
    bool worldSoundHasInsertAlias = false;
    std::size_t worldSoundIndex = 0u;
    std::uint32_t worldSoundListNameReference = 0u;
    std::uint32_t worldSoundHeadReference = 0u;
    std::vector<WorldSoundAliasState> worldSoundAliases;
    std::uint32_t worldSoundAliasIndex = 0u;
    std::uint32_t worldSoundComponentIndex = 0u;
    std::uint32_t worldSoundStringReference = 0u;
    std::uint32_t worldSoundStringBlock4Offset = UINT32_MAX;
    WorldSoundStringTarget worldSoundStringTarget =
        WorldSoundStringTarget::ListName;
    std::uint32_t worldSoundFileDirectoryReference = 0u;
    std::uint32_t worldSoundFileNameReference = 0u;
    std::uint32_t worldSoundLoadedNameReference = 0u;
    std::uint32_t worldSoundLoadedDataReference = 0u;
    std::uint32_t worldSoundLoadedDataBytes = 0u;
    std::uint32_t worldSoundCurveNameReference = 0u;
    std::uint32_t worldSoundSpeakerNameReference = 0u;
    ZoneSpan worldSoundCurveInsertAliasSlot{};
    bool worldSoundCurveHasInsertAlias = false;
    std::uint64_t retainedSoundBytes = 0u;
    RetailClipMapLoadFamily clipMapLoader;
    RetailComWorldLoadFamily comWorldLoader;
    RetailLightDefLoadFamily lightDefLoader;
    ZoneSpan worldMenuStringSpan{};
    std::uint32_t worldFxImpactAssetIndex = 0u;
    std::uint32_t worldFxImpactNameReference = 0u;
    std::uint32_t worldFxImpactTableReference = 0u;
    bool worldFxImpactHasInsertAlias = false;
    std::uint32_t worldMenuAssetIndex = 0u;
    std::deque<WorldMenuTask> worldMenuTasks;
    ZoneSpan worldMenuFixedSpan{};
    std::size_t worldFxIndex = 0u;
    std::uint32_t worldFxElemIndex = 0u;
    std::uint32_t worldFxVisualIndex = 0u;
    WorldFxElemPhase worldFxElemPhase = WorldFxElemPhase::VelocitySamples;
    std::uint32_t worldFxStringReference = 0u;
    std::uint32_t worldFxTrailVertsReference = 0u;
    std::uint32_t worldFxTrailIndicesReference = 0u;
    std::uint32_t worldFxTrailVertexCount = 0u;
    std::uint32_t worldFxTrailIndexCount = 0u;
    std::uint64_t retainedRawFileBytes = 0u;
    std::uint32_t materialPassBytes = 0u;
    std::uint32_t vertexShaderProgramBytes = 0u;
    std::uint32_t pixelShaderProgramBytes = 0u;
    std::uint32_t materialArgumentBytes = 0u;
    std::uint32_t pixelShaderToken = 0u;
    std::uint32_t materialArgumentToken = 0u;
    std::uint32_t vertexShaderNameBlock4Offset = 0u;
    kisak::web::D3D9ShaderContract vertexContract;
    kisak::web::D3D9ShaderContract pixelContract;
    std::array<std::uint32_t, 3> prefixTypes{};
    std::array<std::uint32_t, 3> prefixReferences{};
    ZoneAssetRegistry registry;
    std::array<ZoneSpan, 3> topLevelAliasSlots{};
    std::array<std::uint32_t, 34> secondTechniqueTokens{};
    std::uint32_t secondMaterialPassBytes = 0u;
    std::uint32_t secondVertexProgramBytes = 0u;
    std::uint32_t secondPixelProgramBytes = 0u;
    std::uint32_t secondArgumentBytes = 0u;
    std::uint32_t secondArgumentCount = 0u;
    std::uint32_t secondTechniqueNameToken = 0u;
    std::uint32_t secondVertexShaderNameToken = 0u;
    std::uint32_t secondPixelShaderNameToken = 0u;
    std::uint32_t materialTechniqueSetToken = 0u;
    std::uint32_t materialTextureTableToken = 0u;
    std::uint32_t materialStateBitsToken = 0u;
    std::uint32_t materialStateBitsBytes = 0u;
    std::uint32_t imageTextureToken = 0u;
    std::uint32_t imageResourceBytes = 0u;
    ZoneSpan materialImageAliasSlot{};
    RetailFastfileCensus result{};
    RetailCensusStepBudget *activeLoadBudget = nullptr;
    RetailCensusStepReport *activeLoadReport = nullptr;
    bool *activeLoadBlocked = nullptr;

    ~Impl()
    {
        if (streamInitialized) inflateEnd(&stream);
    }

    bool Available(std::size_t count) const noexcept
    {
        return cursor <= inflated.size() && count <= inflated.size() - cursor;
    }

    RetailCensusError Push(std::uint32_t block) noexcept
    {
        return MapZoneError(arenas.Push(block));
    }

    RetailCensusError Pop() noexcept
    {
        return MapZoneError(arenas.Pop());
    }

    RetailCensusError Plan(
        std::uint32_t alignment,
        std::uint64_t length,
        ZoneSpan *span = nullptr) noexcept
    {
        ZoneLoadPlan plan;
        const RetailCensusError error = MapZoneError(
            arenas.PlanLoad(alignment, length, plan));
        if (error != RetailCensusError::None) return error;
        if (plan.kind != ZoneLoadKind::Immediate)
            return RetailCensusError::ZoneStreamInvalid;
        if (span) *span = plan.span;
        return RetailCensusError::None;
    }

    ZoneStreamMachine &Streams() noexcept override { return arenas; }
    ZoneAssetRegistry &Assets() noexcept override { return registry; }
    RetailFastfileCensus &Ownership() noexcept override { return result; }
    const RetailCensusLimits &LoaderLimits() const noexcept override
    {
        return limits;
    }
    std::size_t InflatedCursor() const noexcept override { return cursor; }
    std::span<const std::uint8_t> InflatedTail() const noexcept override
    {
        return cursor <= inflated.size()
            ? std::span<const std::uint8_t>(inflated).subspan(cursor)
            : std::span<const std::uint8_t>{};
    }
    RetailLoadVisit VisitRecord(std::size_t bytes) noexcept override
    {
        if (!activeLoadBudget || !activeLoadReport || !activeLoadBlocked ||
            !Available(bytes))
        {
            if (activeLoadBlocked) *activeLoadBlocked = true;
            return RetailLoadVisit::Blocked;
        }
        const std::size_t byteBudget =
            activeLoadBudget->maxBytes - activeLoadReport->traversedBytes;
        const std::size_t count = std::min(bytes - recordVisited, byteBudget);
        recordVisited += count;
        activeLoadReport->traversedBytes += static_cast<std::uint32_t>(count);
        if (recordVisited != bytes) return RetailLoadVisit::Partial;
        recordVisited = 0u;
        return RetailLoadVisit::Complete;
    }
    void ConsumeRecord(std::size_t bytes) noexcept override
    {
        cursor += bytes;
        if (activeLoadReport) ++activeLoadReport->recordsProcessed;
    }
    void BlockForInflatedInput() noexcept override
    {
        if (activeLoadBlocked) *activeLoadBlocked = true;
    }
    RetailCensusError PushStream(std::uint32_t block) noexcept override
    {
        return Push(block);
    }
    RetailCensusError PopStream() noexcept override { return Pop(); }
    RetailCensusError PlanStream(
        std::uint32_t alignment,
        std::uint64_t length,
        ZoneSpan *span,
        ZoneLoadKind *kind) noexcept override
    {
        ZoneLoadPlan plan;
        const RetailCensusError error =
            MapZoneError(arenas.PlanLoad(alignment, length, plan));
        if (error != RetailCensusError::None) return error;
        if (span) *span = plan.span;
        if (kind) *kind = plan.kind;
        return RetailCensusError::None;
    }

    RetailCensusError AppendSemanticTrace(
        kisak::database::SemanticTraceEventKind kind,
        std::uint32_t assetType,
        std::uint32_t tracedAssetIndex,
        std::uint32_t identity,
        std::uint32_t inflatedOffset,
        const ZoneSpan &span,
        std::string_view name = {},
        const ZoneSpan &related = {}) noexcept
    {
        if (assetType >= RETAIL_CENSUS_ASSET_TYPE_COUNT)
            return RetailCensusError::AssetTypeInvalid;
        if (result.semanticTrace.size() >= limits.maxSemanticTraceEntries)
            return RetailCensusError::SemanticTraceLimit;
        try
        {
            kisak::database::SemanticTraceEntry entry;
            entry.kind = kind;
            entry.assetType = static_cast<XAssetType>(assetType);
            entry.assetIndex = tracedAssetIndex;
            entry.identity = identity;
            entry.inflatedOffset = inflatedOffset;
            entry.streamBlock = span.block;
            entry.streamOffset = span.offset;
            entry.relatedBlock = related.block;
            entry.relatedOffset = related.offset;
            entry.name.assign(name);
            result.semanticTrace.push_back(std::move(entry));
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        return RetailCensusError::None;
    }

    RetailCensusError Trace(
        kisak::database::SemanticTraceEventKind kind,
        std::uint32_t assetType,
        std::uint32_t tracedAssetIndex,
        std::uint32_t identity,
        std::uint32_t inflatedOffset,
        const ZoneSpan &span,
        std::string_view name,
        const ZoneSpan &related) noexcept override
    {
        return AppendSemanticTrace(kind, assetType, tracedAssetIndex, identity,
            inflatedOffset, span, name, related);
    }

    RetailCensusError EnsureBoundarySemanticTrace() noexcept
    {
        if ((!result.stoppedBeforeDifferentWorldAssetType &&
             !result.stoppedAfterCanonicalRawFile) ||
            result.nextBodyIndex >= worldAssetTypes.size())
        {
            return RetailCensusError::None;
        }
        if (!result.semanticTrace.empty())
        {
            const auto &last = result.semanticTrace.back();
            if (last.kind == kisak::database::SemanticTraceEventKind::Boundary &&
                last.assetIndex == result.nextBodyIndex)
            {
                return RetailCensusError::None;
            }
        }
        const std::uint32_t block = arenas.ActiveBlock();
        return AppendSemanticTrace(
            kisak::database::SemanticTraceEventKind::Boundary,
            result.nextBodyType,
            result.nextBodyIndex,
            0u,
            static_cast<std::uint32_t>(cursor),
            {block, arenas.Cursor(block), 0u},
            {},
            {
                4u,
                result.assetTableBlock4Offset +
                    result.nextBodyIndex * ASSET_BYTES + 4u,
                4u,
            });
    }

    RetailCensusError ValidatePrefix() noexcept
    {
        if (std::equal(AUTHENTICATED_MAGIC.begin(), AUTHENTICATED_MAGIC.end(), prefix.begin()))
            return RetailCensusError::AuthenticatedUnsupported;
        if (!std::equal(UNSIGNED_MAGIC.begin(), UNSIGNED_MAGIC.end(), prefix.begin()))
            return RetailCensusError::InvalidMagic;
        result.version = ReadU32(prefix.data() + 8u);
        return result.version == 5u
            ? RetailCensusError::None
            : RetailCensusError::UnsupportedVersion;
    }

    RetailCensusError BeginWorldTechniqueSet(
        RetailCensusStage &stage) noexcept
    {
        if (worldBodyIndex >= worldAssetTypes.size() ||
            worldAssetTypes[worldBodyIndex] != ASSET_TYPE_TECHNIQUE_SET ||
            worldAssetReferences[worldBodyIndex] != INLINE_POINTER)
        {
            return RetailCensusError::FirstAssetUnsupported;
        }
        worldTopLevelAliasSlot = {
            4u,
            result.assetTableBlock4Offset + worldBodyIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(
                    worldTopLevelAliasSlot, ASSET_TYPE_TECHNIQUE_SET));
            error != RetailCensusError::None)
        {
            return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None)
        {
            return error;
        }
        ZoneSpan span;
        if (const RetailCensusError error = Plan(
                4u, TECHNIQUE_SET_BYTES, &span);
            error != RetailCensusError::None)
        {
            return error;
        }
        try
        {
            result.worldTechniqueSets.emplace_back();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        RetailWorldTechniqueSet &entry = result.worldTechniqueSets.back();
        entry.assetIndex = worldBodyIndex;
        entry.block0Offset = span.offset;
        if (const RetailCensusError error = AppendSemanticTrace(
                kisak::database::SemanticTraceEventKind::AssetBegin,
                ASSET_TYPE_TECHNIQUE_SET,
                worldBodyIndex,
                0u,
                static_cast<std::uint32_t>(cursor),
                span,
                {},
                worldTopLevelAliasSlot);
            error != RetailCensusError::None)
        {
            return error;
        }
        techniqueTokens.fill(0u);
        worldTechniqueSetNameResolved = true;
        result.worldTechniqueSetBodiesEntered = static_cast<std::uint32_t>(
            result.worldTechniqueSets.size());
        result.worldNextAssetIndex = worldBodyIndex;
        if (!result.worldXModels.empty() && result.worldXModels.front().published &&
            worldBodyIndex > result.worldXModels.front().assetIndex)
        {
            if (result.worldPostXModelTechniqueSetAssetIndex == UINT32_MAX)
            {
                result.worldPostXModelTechniqueSetAssetIndex = worldBodyIndex;
            }
            ++result.worldPostXModelTechniqueSetBodiesEntered;
        }
        if (worldBodyIndex == 0u)
            result.worldFirstTechniqueSetBlock0Offset = span.offset;
        stage = RetailCensusStage::WorldTechniqueSet;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldXModel(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_XMODEL ||
            worldAssetReferences[assetIndex] != INLINE_POINTER)
        {
            return RetailCensusError::XModelLayoutUnsupported;
        }
        if (result.worldXModels.size() >= limits.maxWorldXModels)
            return RetailCensusError::XModelCollectionLimit;
        worldXModelAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(worldXModelAliasSlot, ASSET_TYPE_XMODEL));
            error != RetailCensusError::None)
        {
            return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None)
        {
            return error;
        }
        ZoneSpan span;
        if (const RetailCensusError error = Plan(4u, XMODEL_BYTES, &span);
            error != RetailCensusError::None)
        {
            return error;
        }
        try
        {
            result.worldXModels.emplace_back();
            result.worldXModels.back().asset = std::make_shared<XModel>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        worldXModelIndex = result.worldXModels.size() - 1u;
        RetailWorldXModel &model = result.worldXModels.back();
        *model.asset = {};
        model.assetIndex = assetIndex;
        model.registrySourceIndex = assetIndex;
        model.headerBlock0Offset = span.offset;
        model.rendererPayloadSelected = worldXModelIndex == 0u;
        model.topLevelAsset = true;
        if (const RetailCensusError error = AppendSemanticTrace(
                kisak::database::SemanticTraceEventKind::AssetBegin,
                ASSET_TYPE_XMODEL,
                assetIndex,
                0u,
                static_cast<std::uint32_t>(cursor),
                span,
                {},
                worldXModelAliasSlot);
            error != RetailCensusError::None)
        {
            return error;
        }
        worldXModelNested = false;
        worldSurfaceIndex = 0u;
        retainedLodVertices = 0u;
        retainedLodTriangles = 0u;
        worldRigidVertListIndex = 0u;
        worldMaterialIndex = 0u;
        worldTextureIndex = 0u;
        worldCollisionSurfaceIndex = 0u;
        worldImageResourceBytes = 0u;
        worldPhysPresetInsertAliasSlot = {};
        worldPhysPresetHasInsertAlias = false;
        result.worldNextAssetIndex = assetIndex;
        result.nextBodyIndex = assetIndex;
        result.nextBodyType = ASSET_TYPE_XMODEL;
        result.nextBodyReference = INLINE_POINTER;
        stage = RetailCensusStage::WorldXModel;
        return RetailCensusError::None;
    }

    RetailCensusError BeginNestedWorldXModel(
        std::uint32_t token,
        const ZoneSpan &aliasSlot,
        std::uint32_t visualIndex,
        RetailCensusStage &stage) noexcept
    {
        if ((token != INLINE_POINTER && token != SHARED_POINTER) ||
            result.worldXModels.size() >= limits.maxWorldXModels)
        {
            return token == INLINE_POINTER || token == SHARED_POINTER
                ? RetailCensusError::XModelCollectionLimit
                : RetailCensusError::FxElemVisualInvalid;
        }
        worldXModelAliasSlot = aliasSlot;
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(aliasSlot, ASSET_TYPE_XMODEL));
            error != RetailCensusError::None)
        {
            return error;
        }
        if (token == SHARED_POINTER)
        {
            if (const RetailCensusError error = Plan(4u, 4u);
                error != RetailCensusError::None) return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan span;
        if (const RetailCensusError error = Plan(4u, XMODEL_BYTES, &span);
            error != RetailCensusError::None) return error;
        try
        {
            result.worldXModels.emplace_back();
            result.worldXModels.back().asset = std::make_shared<XModel>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        worldXModelIndex = result.worldXModels.size() - 1u;
        RetailWorldXModel &model = result.worldXModels.back();
        *model.asset = {};
        model.assetIndex = result.worldFxEffects[worldFxIndex].assetIndex;
        model.registrySourceIndex = aliasSlot.offset;
        model.headerBlock0Offset = span.offset;
        model.rendererPayloadSelected = false;
        model.topLevelAsset = false;
        worldXModelNested = true;
        worldXModelNestedFxVisualIndex = visualIndex;
        worldSurfaceIndex = 0u;
        retainedLodVertices = 0u;
        retainedLodTriangles = 0u;
        worldRigidVertListIndex = 0u;
        worldMaterialIndex = 0u;
        worldTextureIndex = 0u;
        worldCollisionSurfaceIndex = 0u;
        worldImageResourceBytes = 0u;
        worldPhysPresetInsertAliasSlot = {};
        worldPhysPresetHasInsertAlias = false;
        stage = RetailCensusStage::WorldXModel;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldFxEffect(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_FX ||
            worldAssetReferences[assetIndex] != INLINE_POINTER)
        {
            return RetailCensusError::FxEffectLayoutUnsupported;
        }
        if (result.worldFxEffects.size() >= limits.maxFxEffects)
            return RetailCensusError::FxEffectCountLimit;
        worldFxAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(worldFxAliasSlot, ASSET_TYPE_FX));
            error != RetailCensusError::None) return error;
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan span;
        if (const RetailCensusError error = Plan(
                4u, FX_EFFECT_DEF_BYTES, &span);
            error != RetailCensusError::None) return error;
        try
        {
            result.worldFxEffects.emplace_back();
            result.worldFxEffects.back().asset =
                std::make_shared<FxEffectDef>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        worldFxIndex = result.worldFxEffects.size() - 1u;
        RetailWorldFxEffectDef &effect = result.worldFxEffects.back();
        *effect.asset = {};
        effect.assetIndex = assetIndex;
        effect.headerBlock0Offset = span.offset;
        if (const RetailCensusError error = AppendSemanticTrace(
                kisak::database::SemanticTraceEventKind::AssetBegin,
                ASSET_TYPE_FX,
                assetIndex,
                0u,
                static_cast<std::uint32_t>(cursor),
                span,
                {},
                worldFxAliasSlot);
            error != RetailCensusError::None)
        {
            return error;
        }
        worldFxElemIndex = 0u;
        worldFxVisualIndex = 0u;
        worldFxElemPhase = WorldFxElemPhase::VelocitySamples;
        result.worldNextAssetIndex = assetIndex;
        result.nextBodyIndex = assetIndex;
        result.nextBodyType = ASSET_TYPE_FX;
        result.nextBodyReference = INLINE_POINTER;
        stage = RetailCensusStage::WorldFxEffect;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldRawFile(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_RAW_FILE ||
            worldAssetReferences[assetIndex] != INLINE_POINTER)
        {
            return RetailCensusError::RawFileLayoutUnsupported;
        }
        if (result.worldRawFiles.size() >= limits.maxRawFiles)
            return RetailCensusError::RawFileCollectionLimit;
        worldRawFileAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(
                    worldRawFileAliasSlot, ASSET_TYPE_RAW_FILE));
            error != RetailCensusError::None)
        {
            return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan span;
        if (const RetailCensusError error = Plan(4u, RAWFILE_BYTES, &span);
            error != RetailCensusError::None) return error;
        try { result.worldRawFiles.emplace_back(); }
        catch (...) { return RetailCensusError::AllocationFailed; }
        RetailWorldRawFile &rawFile = result.worldRawFiles.back();
        rawFile.assetIndex = assetIndex;
        rawFile.headerBlock0Offset = span.offset;
        if (const RetailCensusError error = AppendSemanticTrace(
                kisak::database::SemanticTraceEventKind::AssetBegin,
                ASSET_TYPE_RAW_FILE,
                assetIndex,
                0u,
                static_cast<std::uint32_t>(cursor),
                span,
                {},
                worldRawFileAliasSlot);
            error != RetailCensusError::None)
        {
            return error;
        }
        result.worldNextAssetIndex = assetIndex;
        result.nextBodyIndex = assetIndex;
        result.nextBodyType = ASSET_TYPE_RAW_FILE;
        result.nextBodyReference = INLINE_POINTER;
        stage = RetailCensusStage::WorldRawFile;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldFxImpactTable(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_IMPACT_FX ||
            (worldAssetReferences[assetIndex] != INLINE_POINTER &&
             worldAssetReferences[assetIndex] != SHARED_POINTER))
        {
            return RetailCensusError::FxEffectLayoutUnsupported;
        }
        worldFxImpactHasInsertAlias =
            worldAssetReferences[assetIndex] == SHARED_POINTER;
        if (worldFxImpactHasInsertAlias)
        {
            if (const RetailCensusError error = Plan(4u, 4u);
                error != RetailCensusError::None) return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        if (const RetailCensusError error = Plan(
                4u, FX_IMPACT_TABLE_BYTES);
            error != RetailCensusError::None) return error;
        worldFxImpactAssetIndex = assetIndex;
        worldFxImpactNameReference = 0u;
        worldFxImpactTableReference = 0u;
        result.worldNextAssetIndex = assetIndex;
        result.nextBodyIndex = assetIndex;
        result.nextBodyType = ASSET_TYPE_IMPACT_FX;
        result.nextBodyReference = worldAssetReferences[assetIndex];
        stage = RetailCensusStage::WorldFxImpactTable;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldMenuList(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_MENU_LIST ||
            (worldAssetReferences[assetIndex] != INLINE_POINTER &&
             worldAssetReferences[assetIndex] != SHARED_POINTER))
        {
            return RetailCensusError::InvalidArgument;
        }
        worldMenuTasks.clear();
        if (block4NativeOffsetBiasStart == UINT32_MAX)
            block4NativeOffsetBiasStart = arenas.Cursor(4u);
        worldMenuAssetIndex = assetIndex;
        worldMenuTasks.push_back({WorldMenuTaskKind::Push0});
        if (worldAssetReferences[assetIndex] == SHARED_POINTER)
            worldMenuTasks.push_back({WorldMenuTaskKind::InsertPointer});
        worldMenuTasks.push_back({WorldMenuTaskKind::MenuList});
        result.nextBodyIndex = assetIndex;
        result.nextBodyType = ASSET_TYPE_MENU_LIST;
        result.nextBodyReference = worldAssetReferences[assetIndex];
        stage = RetailCensusStage::WorldMenuTasks;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldStringTable(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (worldAssetReferences[assetIndex] != INLINE_POINTER)
            return RetailCensusError::InvalidArgument;
        worldMenuTasks.clear();
        worldMenuAssetIndex = assetIndex;
        worldMenuTasks.push_back({WorldMenuTaskKind::StringTable});
        worldMenuTasks.push_back({WorldMenuTaskKind::Finish});
        stage = RetailCensusStage::WorldMenuTasks;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldLocalizeEntry(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_LOCALIZE ||
            (worldAssetReferences[assetIndex] != INLINE_POINTER &&
             worldAssetReferences[assetIndex] != SHARED_POINTER))
        {
            return RetailCensusError::LocalizeLayoutUnsupported;
        }
        if (result.worldLocalizeEntries.size() >= limits.maxLocalizeEntries)
            return RetailCensusError::LocalizeCollectionLimit;

        worldLocalizeAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(
                    worldLocalizeAliasSlot, ASSET_TYPE_LOCALIZE));
            error != RetailCensusError::None)
        {
            return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan headerSpan;
        if (const RetailCensusError error = Plan(
                4u, LOCALIZE_ENTRY_BYTES, &headerSpan);
            error != RetailCensusError::None) return error;

        worldLocalizeHasInsertAlias =
            worldAssetReferences[assetIndex] == SHARED_POINTER;
        worldLocalizeInsertAliasSlot = {};
        if (worldLocalizeHasInsertAlias)
        {
            if (const RetailCensusError error = Push(4u);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Plan(
                    4u, 4u, &worldLocalizeInsertAliasSlot);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Pop();
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = MapRegistryError(
                    registry.ReserveAlias(
                        worldLocalizeInsertAliasSlot, ASSET_TYPE_LOCALIZE));
                error != RetailCensusError::None) return error;
        }

        try
        {
            result.worldLocalizeEntries.emplace_back();
            RetailPublishedLocalizeEntry &entry =
                result.worldLocalizeEntries.back();
            entry.storage =
                std::make_shared<CanonicalLocalizeEntryStorage>();
            entry.asset = std::make_shared<LocalizeEntry>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        worldLocalizeIndex = result.worldLocalizeEntries.size() - 1u;
        RetailPublishedLocalizeEntry &entry =
            result.worldLocalizeEntries.back();
        *entry.asset = {};
        entry.assetIndex = assetIndex;
        entry.serializedReference = worldAssetReferences[assetIndex];
        entry.headerBlock0Offset = headerSpan.offset;
        if (worldLocalizeHasInsertAlias)
            entry.insertPointerBlock4Offset =
                worldLocalizeInsertAliasSlot.offset;
        worldLocalizeValueReference = 0u;
        worldLocalizeNameReference = 0u;
        stage = RetailCensusStage::WorldLocalizeEntry;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldMaterial(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_MATERIAL ||
            (worldAssetReferences[assetIndex] != INLINE_POINTER &&
             worldAssetReferences[assetIndex] != SHARED_POINTER))
        {
            return RetailCensusError::MaterialLayoutUnsupported;
        }
        worldMaterialAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(
                    worldMaterialAliasSlot, ASSET_TYPE_MATERIAL));
            error != RetailCensusError::None) return error;
        worldMaterialHasInsertAlias =
            worldAssetReferences[assetIndex] == SHARED_POINTER;
        worldMaterialInsertAliasSlot = {};
        if (worldMaterialHasInsertAlias)
        {
            if (const RetailCensusError error = Plan(
                    4u, 4u, &worldMaterialInsertAliasSlot);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = MapRegistryError(
                    registry.ReserveAlias(
                        worldMaterialInsertAliasSlot, ASSET_TYPE_MATERIAL));
                error != RetailCensusError::None) return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan headerSpan;
        if (const RetailCensusError error = Plan(
                4u, MATERIAL_BYTES, &headerSpan);
            error != RetailCensusError::None) return error;
        try
        {
            result.worldMaterials.emplace_back();
            result.worldMaterials.back().asset =
                std::make_shared<Material>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        RetailXModelMaterial &material = result.worldMaterials.back();
        *material.asset = {};
        material.handleIndex = assetIndex;
        material.headerBlock0Offset = headerSpan.offset;
        worldMaterialTopLevel = true;
        worldMaterialTopLevelAssetIndex = assetIndex;
        worldMaterialOwnedByFx = false;
        worldMaterialOwnedByWeapon = false;
        worldMaterialOwnedByMenu = false;
        worldTextureIndex = 0u;
        stage = RetailCensusStage::WorldXModelMaterial;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldSoundAliasList(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_SOUND ||
            (worldAssetReferences[assetIndex] != INLINE_POINTER &&
             worldAssetReferences[assetIndex] != SHARED_POINTER))
        {
            return RetailCensusError::SoundAliasLayoutUnsupported;
        }
        if (result.worldSoundAliasLists.size() >= limits.maxSoundAliasLists)
            return RetailCensusError::SoundAliasCollectionLimit;

        worldSoundAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(worldSoundAliasSlot, ASSET_TYPE_SOUND));
            error != RetailCensusError::None) return error;
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan headerSpan;
        if (const RetailCensusError error = Plan(
                4u, SOUND_ALIAS_LIST_BYTES, &headerSpan);
            error != RetailCensusError::None) return error;

        worldSoundHasInsertAlias =
            worldAssetReferences[assetIndex] == SHARED_POINTER;
        worldSoundInsertAliasSlot = {};
        if (worldSoundHasInsertAlias)
        {
            if (const RetailCensusError error = Push(4u);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Plan(
                    4u, 4u, &worldSoundInsertAliasSlot);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Pop();
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = MapRegistryError(
                    registry.ReserveAlias(
                        worldSoundInsertAliasSlot, ASSET_TYPE_SOUND));
                error != RetailCensusError::None) return error;
        }

        try
        {
            result.worldSoundAliasLists.emplace_back();
            RetailPublishedSoundAliasList &entry =
                result.worldSoundAliasLists.back();
            entry.storage =
                std::make_shared<CanonicalSoundAliasListStorage>();
            entry.asset = std::make_shared<snd_alias_list_t>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        worldSoundIndex = result.worldSoundAliasLists.size() - 1u;
        RetailPublishedSoundAliasList &entry =
            result.worldSoundAliasLists.back();
        *entry.asset = {};
        entry.assetIndex = assetIndex;
        entry.serializedReference = worldAssetReferences[assetIndex];
        entry.headerBlock0Offset = headerSpan.offset;
        if (worldSoundHasInsertAlias)
            entry.insertPointerBlock4Offset = worldSoundInsertAliasSlot.offset;
        worldSoundListNameReference = 0u;
        worldSoundHeadReference = 0u;
        worldSoundAliases.clear();
        worldSoundAliasIndex = 0u;
        stage = RetailCensusStage::WorldSoundAliasList;
        return RetailCensusError::None;
    }

    static void AssignWeaponString(
        WeaponDef &weapon,
        std::uint32_t index,
        const char *value) noexcept
    {
        weapon_loader::AssignWeaponString(weapon, index, value);
    }

    static void AssignWeaponAccuracyKnots(
        WeaponDef &weapon,
        std::uint32_t index,
        float (*value)[WEAP_ACCURACY_COUNT]) noexcept
    {
        weapon_loader::AssignWeaponAccuracyKnots(weapon, index, value);
    }

    static void AssignWeaponXModel(
        WeaponDef &weapon,
        std::uint32_t index,
        XModel *value) noexcept
    {
        weapon_loader::AssignWeaponXModel(weapon, index, value);
    }

    static void AssignWeaponFx(
        WeaponDef &weapon,
        std::uint32_t index,
        const FxEffectDef *value) noexcept
    {
        weapon_loader::AssignWeaponFx(weapon, index, value);
    }

    static void AssignWeaponMaterial(
        WeaponDef &weapon,
        std::uint32_t index,
        Material *value) noexcept
    {
        weapon_loader::AssignWeaponMaterial(weapon, index, value);
    }

    static void AssignWeaponSound(
        WeaponDef &weapon,
        std::uint32_t index,
        snd_alias_list_t *value) noexcept
    {
        weapon_loader::AssignWeaponSound(weapon, index, value);
    }

    bool TranslateNativeBlock4Span(ZoneSpan &target) const noexcept
    {
        if (target.block != 4u)
        {
            return true;
        }
        std::int32_t bias = 0;
        bool found = false;
        for (const auto &segment : block4BiasSegments)
        {
            if (segment.first <= target.offset)
            {
                bias = segment.second;
                found = true;
            }
        }
        if (!found && block4NativeOffsetBiasKnown &&
            target.offset >= block4NativeOffsetBiasStart)
        {
            bias = block4NativeOffsetBias;
            found = true;
        }
        if (!found) return true;
        const std::int64_t adjusted = static_cast<std::int64_t>(target.offset) +
            bias;
        if (adjusted < 0 || adjusted > UINT32_MAX)
            return false;
        target.offset = static_cast<std::uint32_t>(adjusted);
        return target.offset <= arenas.DeclaredSize(4u) &&
            target.length <= arenas.DeclaredSize(4u) - target.offset;
    }

    bool ResolvePriorZoneStringPayload(
        std::uint32_t token,
        std::shared_ptr<std::string> &value,
        std::uint32_t &offset) noexcept
    {
        ZoneSpan nativeTarget;
        if (!DecodeZoneAliasToken(token, nativeTarget) ||
            nativeTarget.block != 4u)
            return false;
        const auto rememberNativeResolution = [&]() noexcept {
            if (!value || offset == UINT32_MAX) return false;
            try
            {
                nativeBlock4StringAliases.emplace(
                    nativeTarget.offset, std::make_pair(offset, value));
                zoneStringTokenAliases.emplace(token, value);
            }
            catch (...)
            {
                // The owned string remains valid even if this compatibility
                // correspondence cannot be cached.
            }
            return true;
        };
        const auto nativeIndexed =
            nativeBlock4StringAliases.find(nativeTarget.offset);
        if (nativeIndexed != nativeBlock4StringAliases.end() &&
            nativeIndexed->second.second)
        {
            offset = nativeIndexed->second.first;
            value = nativeIndexed->second.second;
            return true;
        }
        ZoneSpan target = nativeTarget;
        if (!TranslateNativeBlock4Span(target)) return false;
        const auto indexed = block4StringAliases.find(target.offset);
        if (indexed != block4StringAliases.end() && indexed->second)
        {
            value = indexed->second;
            offset = target.offset;
            return rememberNativeResolution();
        }
        // DB_ConvertOffsetToPointer converts an address, not an asset key.
        // XStrings therefore may legally point into a prior character payload
        // (WeaponDef animation fields commonly retain only the suffix of an
        // XAnimParts name).  Preserve that native behavior while keeping the
        // derived suffix independently owned and stable.
        std::shared_ptr<std::string> containing;
        std::size_t suffixOffset = 0u;
        for (const auto &[candidateOffset, candidate] : block4StringAliases)
        {
            if (!candidate || target.offset < candidateOffset)
                continue;
            const std::uint64_t delta =
                static_cast<std::uint64_t>(target.offset) - candidateOffset;
            if (delta <= candidate->size())
            {
                containing = candidate;
                suffixOffset = static_cast<std::size_t>(delta);
                break;
            }
        }
        if (containing)
        {
            try
            {
                value = std::make_shared<std::string>(
                    containing->substr(suffixOffset));
                block4StringAliases.emplace(target.offset, value);
            }
            catch (...) { return false; }
            offset = target.offset;
            return rememberNativeResolution();
        }
        if (prerequisiteZone)
        {
            auto closest = indirectBlock4StringAliases.end();
            std::uint32_t closestDelta = UINT32_MAX;
            for (auto candidate = indirectBlock4StringAliases.begin();
                 candidate != indirectBlock4StringAliases.end(); ++candidate)
            {
                if (!candidate->second) continue;
                const std::uint32_t delta = candidate->first > target.offset
                    ? candidate->first - target.offset
                    : target.offset - candidate->first;
                if (delta > FX_ELEM_DEF_BYTES || delta >= closestDelta)
                    continue;
                closest = candidate;
                closestDelta = delta;
            }
            if (closest != indirectBlock4StringAliases.end())
            {
                value = closest->second;
                offset = closest->first;
                return rememberNativeResolution();
            }
        }
        auto copyValue = [&](const std::string &source) noexcept {
            try { value = std::make_shared<std::string>(source); }
            catch (...) { return false; }
            offset = target.offset;
            try { block4StringAliases.emplace(target.offset, value); }
            catch (...) { return false; }
            return rememberNativeResolution();
        };
        for (std::size_t index = 0u;
             index < scriptStringBlock4Offsets.size(); ++index)
        {
            if (scriptStringBlock4Offsets[index] == target.offset)
                return copyValue(scriptStrings[index]);
        }
        for (const RetailWorldTechniqueSet &set : result.worldTechniqueSets)
        {
            if (set.nameBlock4Offset == target.offset)
                return copyValue(set.name);
            for (const RetailWorldMaterialTechnique &technique : set.techniques)
                if (technique.nameBlock4Offset == target.offset)
                    return copyValue(technique.name);
        }
        for (const RetailWorldXModel &model : result.worldXModels)
        {
            if (model.nameBlock4Offset == target.offset && model.canonicalName)
            {
                value = model.canonicalName;
                offset = target.offset;
                return true;
            }
            if (model.physPreset.nameBlock4Offset == target.offset)
                return copyValue(model.physPreset.name);
            if (model.physPreset.soundAliasPrefixBlock4Offset == target.offset)
                return copyValue(model.physPreset.soundAliasPrefix);
            for (const RetailXModelMaterial &material : model.materials)
            {
                if (material.nameBlock4Offset == target.offset &&
                    material.canonicalName)
                {
                    value = material.canonicalName;
                    offset = target.offset;
                    return true;
                }
                for (const RetailXModelImage &image : material.images)
                    if (image.nameBlock4Offset == target.offset)
                        return copyValue(image.name);
            }
        }
        for (const RetailXModelMaterial &material : result.worldMaterials)
        {
            if (material.nameBlock4Offset == target.offset &&
                material.canonicalName)
            {
                value = material.canonicalName;
                offset = target.offset;
                return true;
            }
            for (const RetailXModelImage &image : material.images)
            {
                if (image.nameBlock4Offset == target.offset)
                    return copyValue(image.name);
            }
        }
        for (const RetailWorldFxEffectDef &effect : result.worldFxEffects)
        {
            if (effect.nameBlock4Offset == target.offset && effect.canonicalName)
            {
                value = effect.canonicalName;
                offset = target.offset;
                return true;
            }
            for (const RetailWorldFxMaterial &material : effect.materials)
            {
                if (material.nameBlock4Offset == target.offset &&
                    material.canonicalName)
                {
                    value = material.canonicalName;
                    offset = target.offset;
                    return true;
                }
                for (const RetailXModelImage &image : material.images)
                    if (image.nameBlock4Offset == target.offset)
                        return copyValue(image.name);
            }
        }
        for (const RetailWorldRawFile &rawFile : result.worldRawFiles)
        {
            if (rawFile.nameBlock4Offset == target.offset && rawFile.nameStorage)
            {
                value = rawFile.nameStorage;
                offset = target.offset;
                return true;
            }
        }
        for (const RetailPublishedXAnimParts &parts : result.worldXAnimParts)
        {
            if (parts.nameBlock4Offset == target.offset &&
                parts.storage && parts.storage->name)
            {
                value = parts.storage->name;
                offset = target.offset;
                return true;
            }
        }
        for (const RetailPublishedLocalizeEntry &entry :
             result.worldLocalizeEntries)
        {
            if (!entry.storage) continue;
            if (entry.valueBlock4Offset == target.offset && entry.storage->value)
            {
                value = entry.storage->value;
                offset = target.offset;
                return true;
            }
            if (entry.nameBlock4Offset == target.offset && entry.storage->name)
            {
                value = entry.storage->name;
                offset = target.offset;
                return true;
            }
        }
        for (const RetailPublishedSoundAliasList &entry :
             result.worldSoundAliasLists)
        {
            if (entry.storage && entry.storage->aliasName &&
                entry.nameBlock4Offset == target.offset)
            {
                value = entry.storage->aliasName;
                offset = target.offset;
                return true;
            }
        }
        for (RetailPublishedWeaponDef &candidate : result.worldWeapons)
        {
            if (!candidate.storage) continue;
            for (std::size_t index = 0u;
                 index < candidate.stringBlock4Offsets.size(); ++index)
            {
                if (candidate.stringBlock4Offsets[index] == target.offset &&
                    candidate.storage->strings[index])
                {
                    value = candidate.storage->strings[index];
                    offset = target.offset;
                    return rememberNativeResolution();
                }
            }
            for (std::size_t index = 0u;
                 index < candidate.soundNameStringBlock4Offsets.size(); ++index)
            {
                if (candidate.soundNameStringBlock4Offsets[index] == target.offset &&
                    candidate.storage->soundNames[index])
                {
                    value = candidate.storage->soundNames[index];
                    offset = target.offset;
                    return rememberNativeResolution();
                }
            }
            for (std::size_t index = 0u;
                 index < candidate.bounceSoundNameStringBlock4Offsets.size();
                 ++index)
            {
                if (candidate.bounceSoundNameStringBlock4Offsets[index] ==
                        target.offset &&
                    candidate.storage->bounceSoundNames[index])
                {
                    value = candidate.storage->bounceSoundNames[index];
                    offset = target.offset;
                    return rememberNativeResolution();
                }
            }
        }
        return false;
    }

    bool ResolveOrderedWeaponSoundName(
        std::uint32_t token,
        std::shared_ptr<std::string> &value,
        std::uint32_t &offset) noexcept
    {
        ZoneSpan nativeTarget;
        if (!prerequisiteZone || worldSoundIndex < 4u ||
            !DecodeZoneAliasToken(token, nativeTarget) ||
            nativeTarget.block != 4u)
            return false;

        // IW3 emits each top-level asset-type run in database-name order. A
        // prerequisite WeaponDef can load the exact sound-name XString before
        // the corresponding type-7 row through Load_snd_alias_list_name. Use
        // that retained payload only after the preceding serialized sound rows
        // prove the ordering invariant. This bridges a missing native/local
        // offset correspondence without synthesizing a name or moving a cursor.
        const std::size_t orderedBegin = worldSoundIndex > 8u
            ? worldSoundIndex - 8u : 0u;
        const std::string *lowerBound = nullptr;
        for (std::size_t index = orderedBegin; index < worldSoundIndex; ++index)
        {
            const RetailPublishedSoundAliasList &entry =
                result.worldSoundAliasLists[index];
            if (!entry.published || !entry.storage || !entry.storage->aliasName ||
                (lowerBound && !DatabaseNameLess(
                    *lowerBound, *entry.storage->aliasName)))
                return false;
            lowerBound = entry.storage->aliasName.get();
        }
        if (!lowerBound) return false;

        const auto alreadyPublished = [&](const std::string &name) noexcept {
            return std::any_of(
                result.worldSoundAliasLists.begin(),
                result.worldSoundAliasLists.begin() +
                    static_cast<std::ptrdiff_t>(worldSoundIndex),
                [&](const RetailPublishedSoundAliasList &entry) {
                    return entry.published && entry.storage &&
                        entry.storage->aliasName &&
                        DatabaseNamesEqual(*entry.storage->aliasName, name);
                });
        };
        std::shared_ptr<std::string> candidate;
        const auto consider = [&](const std::shared_ptr<std::string> &name,
                                  std::uint32_t) noexcept {
            if (!name || !ValidPublishedName(*name) ||
                !DatabaseNameLess(*lowerBound, *name) ||
                alreadyPublished(*name))
                return;
            if (!candidate || DatabaseNameLess(*name, *candidate))
                candidate = name;
        };
        for (const RetailPublishedWeaponDef &weapon : result.worldWeapons)
        {
            if (!weapon.storage) continue;
            for (std::size_t index = 0u;
                 index < weapon.storage->soundNames.size(); ++index)
            {
                consider(weapon.storage->soundNames[index],
                    weapon.soundNameStringBlock4Offsets[index]);
            }
            for (std::size_t index = 0u;
                 index < weapon.storage->bounceSoundNames.size(); ++index)
            {
                consider(weapon.storage->bounceSoundNames[index],
                    weapon.bounceSoundNameStringBlock4Offsets[index]);
            }
        }
        if (!candidate) return false;

        std::uint32_t predecessorNative = 0u;
        std::uint32_t predecessorLocal = 0u;
        std::uint32_t successorNative = UINT32_MAX;
        std::uint32_t successorLocal = 0u;
        for (const auto &[anchorNative, local] : nativeBlock4StringAliases)
        {
            if (!local.second) continue;
            if (anchorNative < nativeTarget.offset &&
                anchorNative >= predecessorNative)
            {
                predecessorNative = anchorNative;
                predecessorLocal = local.first;
            }
            else if (anchorNative > nativeTarget.offset &&
                anchorNative < successorNative)
            {
                successorNative = anchorNative;
                successorLocal = local.first;
            }
        }
        const auto project = [&](std::uint32_t anchorNative,
                                 std::uint32_t anchorLocal,
                                 std::uint32_t fallback) noexcept {
            if (anchorNative == 0u || anchorNative == UINT32_MAX)
                return fallback;
            const std::int64_t projected =
                static_cast<std::int64_t>(nativeTarget.offset) + anchorLocal -
                anchorNative;
            return projected < 0 || projected > UINT32_MAX
                ? fallback : static_cast<std::uint32_t>(projected);
        };
        const std::uint32_t lower = project(
            predecessorNative, predecessorLocal, 0u);
        const std::uint32_t upper = project(
            successorNative, successorLocal, UINT32_MAX);

        std::uint32_t candidateOffset = UINT32_MAX;
        bool candidateInBounds = false;
        const auto selectStorage = [&](const std::shared_ptr<std::string> &name,
                                       std::uint32_t localOffset) noexcept {
            if (!name || !DatabaseNamesEqual(*name, *candidate) ||
                localOffset == UINT32_MAX)
                return;
            if (localOffset >= lower && localOffset <= upper)
            {
                if (!candidateInBounds || localOffset < candidateOffset)
                {
                    candidateOffset = localOffset;
                    candidate = name;
                    candidateInBounds = true;
                }
            }
            else if (!candidateInBounds &&
                (candidateOffset == UINT32_MAX || localOffset < candidateOffset))
            {
                candidateOffset = localOffset;
                candidate = name;
            }
        };
        for (const RetailPublishedWeaponDef &weapon : result.worldWeapons)
        {
            if (!weapon.storage) continue;
            for (std::size_t index = 0u;
                 index < weapon.storage->soundNames.size(); ++index)
            {
                selectStorage(weapon.storage->soundNames[index],
                    weapon.soundNameStringBlock4Offsets[index]);
            }
            for (std::size_t index = 0u;
                 index < weapon.storage->bounceSoundNames.size(); ++index)
            {
                selectStorage(weapon.storage->bounceSoundNames[index],
                    weapon.bounceSoundNameStringBlock4Offsets[index]);
            }
        }
        if (candidateOffset == UINT32_MAX) return false;

        value = std::move(candidate);
        offset = candidateOffset;
        return true;
    }

    ZoneRegistryError ResolveRegistryAlias(
        std::uint32_t token,
        std::uint32_t expectedType,
        std::uint32_t &identity) const noexcept
    {
        ZoneRegistryError error =
            registry.ResolveAlias(token, expectedType, identity);
        if (error == ZoneRegistryError::None ||
            (block4BiasSegments.empty() && !block4NativeOffsetBiasKnown))
        {
            return error;
        }
        ZoneSpan target;
        if (!DecodeZoneAliasToken(token, target) || target.block != 4u ||
            !TranslateNativeBlock4Span(target))
        {
            return error;
        }
        std::uint32_t adjustedToken = 0u;
        if (!EncodeZoneAliasToken(target, adjustedToken))
            return error;
        return registry.ResolveAlias(adjustedToken, expectedType, identity);
    }

    ZoneRegistryError ResolveAssetAlias(
        std::uint32_t token,
        std::uint32_t expectedType,
        std::uint32_t &identity) const noexcept override
    {
        return ResolveRegistryAlias(token, expectedType, identity);
    }

    void *FindCanonicalAsset(
        std::uint32_t type,
        std::uint32_t identity) noexcept override
    {
        if (type == ASSET_TYPE_XMODEL)
        {
            for (RetailWorldXModel &entry : result.worldXModels)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_FX)
        {
            for (RetailWorldFxEffectDef &entry : result.worldFxEffects)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_MATERIAL)
        {
            for (RetailXModelMaterial &entry : result.worldMaterials)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
            for (RetailWorldXModel &model : result.worldXModels)
                for (RetailXModelMaterial &entry : model.materials)
                    if (entry.published && entry.identity == identity && entry.asset)
                        return entry.asset.get();
            for (RetailWorldFxEffectDef &effect : result.worldFxEffects)
                for (RetailWorldFxMaterial &entry : effect.materials)
                    if (entry.published && entry.identity == identity && entry.asset)
                        return entry.asset.get();
        }
        else if (type == ASSET_TYPE_SOUND)
        {
            for (RetailPublishedSoundAliasList &entry : result.worldSoundAliasLists)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_XANIM_PARTS)
        {
            for (RetailPublishedXAnimParts &entry : result.worldXAnimParts)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_WEAPON)
        {
            for (RetailPublishedWeaponDef &entry : result.worldWeapons)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_CLIPMAP || type == ASSET_TYPE_CLIPMAP_PVS)
        {
            for (RetailPublishedClipMap &entry : result.worldClipMaps)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_COMWORLD)
        {
            for (RetailPublishedComWorld &entry : result.worldComWorlds)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_IMAGE)
        {
            for (RetailPublishedGfxImage &entry : result.worldImages)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        else if (type == ASSET_TYPE_LIGHT_DEF)
        {
            for (RetailPublishedLightDef &entry : result.worldLightDefs)
                if (entry.published && entry.identity == identity && entry.asset)
                    return entry.asset.get();
        }
        return nullptr;
    }

    bool ValidPointerToken(
        std::uint32_t token,
        std::uint32_t alignment) const noexcept override
    {
        return ValidPriorZonePointer(token, alignment);
    }

    bool ResolveXString(
        std::uint32_t token,
        std::shared_ptr<std::string> &value,
        std::uint32_t &block4Offset) noexcept override
    {
        return ResolvePriorZoneStringPayload(token, value, block4Offset);
    }

    RetailCensusError RememberXString(
        std::uint32_t serializedToken,
        const ZoneSpan &span,
        const std::shared_ptr<std::string> &value) noexcept override
    {
        if (!value || span.block != 4u)
            return RetailCensusError::InvalidArgument;
        try
        {
            block4StringAliases.emplace(span.offset, value);
            if (serializedToken != 0u && serializedToken != INLINE_POINTER &&
                serializedToken != SHARED_POINTER)
            {
                zoneStringTokenAliases.emplace(serializedToken, value);
                ZoneSpan native;
                if (DecodeZoneAliasToken(serializedToken, native) &&
                    native.block == 4u)
                {
                    nativeBlock4StringAliases.emplace(
                        native.offset, std::make_pair(span.offset, value));
                }
            }
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        return RetailCensusError::None;
    }

    bool RecordBlock4CompatibilityBias(
        std::uint32_t start,
        std::uint32_t biasValue) noexcept
    {
        if (biasValue > 4096u) return false;
        const std::int32_t bias = static_cast<std::int32_t>(biasValue);
        if (!block4BiasSegments.empty())
        {
            const auto &prior = block4BiasSegments.back();
            if (start < prior.first || bias < prior.second ||
                bias - prior.second > 4096)
                return false;
        }
        try
        {
            if (block4BiasSegments.empty() ||
                block4BiasSegments.back() != std::pair{start, bias})
                block4BiasSegments.emplace_back(start, bias);
        }
        catch (...) { return false; }
        return true;
    }

    bool CalibrateBlock4BiasFromWeaponXString(
        std::uint32_t token,
        const RetailPublishedWeaponDef &entry) noexcept
    {
        ZoneSpan target;
        if (prerequisiteZone || !entry.storage ||
            !DecodeZoneAliasToken(token, target) || target.block != 4u)
            return false;

        // First prefer a string already loaded by this WeaponDef. Generated
        // Load_WeaponDef processes XStrings in field order, so a later field
        // may point into an earlier inline string after temporary families have
        // shifted the compatibility arena.
        std::uint32_t currentWeaponCandidate = 0u;
        std::uint32_t currentWeaponEarliest = UINT32_MAX;
        for (std::size_t index = 0u; index < entry.stringBlock4Offsets.size(); ++index)
        {
            const std::uint32_t offset = entry.stringBlock4Offsets[index];
            if (offset != UINT32_MAX && entry.storage->strings[index] &&
                offset >= target.offset && offset - target.offset <= 4096u)
            {
                currentWeaponCandidate = std::max(currentWeaponCandidate, offset);
                currentWeaponEarliest = std::min(currentWeaponEarliest, offset);
            }
        }
        if (currentWeaponCandidate != 0u)
        {
            const std::uint32_t bias = currentWeaponCandidate - target.offset;
            const std::uint32_t coveredPrefix =
                currentWeaponCandidate - currentWeaponEarliest;
            const std::uint32_t start = target.offset >= coveredPrefix
                ? target.offset - coveredPrefix : target.offset;
            if (RecordBlock4CompatibilityBias(start, bias)) return true;
        }

        // WeaponDef::szXAnims may instead point into a canonical XAnimParts
        // name. Choose only the nearest published name in the same bounded
        // drift window, and require monotonic bias growth above.
        std::uint32_t xanimCandidate = UINT32_MAX;
        for (const RetailPublishedXAnimParts &parts : result.worldXAnimParts)
        {
            if (!parts.published || !parts.storage || !parts.storage->name ||
                parts.nameBlock4Offset < target.offset ||
                parts.nameBlock4Offset - target.offset > 4096u)
                continue;
            xanimCandidate = std::min(xanimCandidate, parts.nameBlock4Offset);
        }
        return xanimCandidate != UINT32_MAX &&
            RecordBlock4CompatibilityBias(
                target.offset, xanimCandidate - target.offset);
    }

    bool ResolvePriorWeaponSoundCell(
        std::uint32_t token,
        std::shared_ptr<std::string> &value) noexcept
    {
        ZoneSpan nativeTarget;
        if (!DecodeZoneAliasToken(token, nativeTarget) ||
            nativeTarget.block != 4u)
            return false;
        std::uint32_t resolvedCellOffset = UINT32_MAX;
        std::uint32_t resolvedStringOffset = UINT32_MAX;
        auto find = [&](const ZoneSpan &target) noexcept {
            for (RetailPublishedWeaponDef &candidate : result.worldWeapons)
            {
                if (!candidate.storage) continue;
                for (std::size_t index = 0u;
                     index < candidate.soundNameCellBlock4Offsets.size(); ++index)
                {
                    if (candidate.soundNameCellBlock4Offsets[index] ==
                            target.offset && candidate.storage->soundNames[index])
                    {
                        value = candidate.storage->soundNames[index];
                        resolvedCellOffset =
                            candidate.soundNameCellBlock4Offsets[index];
                        resolvedStringOffset =
                            candidate.soundNameStringBlock4Offsets[index];
                        return true;
                    }
                }
                for (std::size_t index = 0u;
                     index < candidate.bounceSoundNameCellBlock4Offsets.size();
                     ++index)
                {
                    if (candidate.bounceSoundNameCellBlock4Offsets[index] ==
                            target.offset &&
                        candidate.storage->bounceSoundNames[index])
                    {
                        value = candidate.storage->bounceSoundNames[index];
                        resolvedCellOffset =
                            candidate.bounceSoundNameCellBlock4Offsets[index];
                        resolvedStringOffset = candidate.
                            bounceSoundNameStringBlock4Offsets[index];
                        return true;
                    }
                }
            }
            return false;
        };
        const auto rememberInlinePayload = [&]() noexcept {
            if (!value || resolvedCellOffset == UINT32_MAX ||
                resolvedStringOffset != resolvedCellOffset + 4u ||
                nativeTarget.offset > 0x0ffffffbu)
                return;
            try
            {
                nativeBlock4StringAliases.emplace(
                    nativeTarget.offset + 4u,
                    std::make_pair(resolvedStringOffset, value));
            }
            catch (...) {}
        };
        // A native alias may still name an object from an earlier range whose
        // portable and native virtual offsets agree.  Prefer that exact
        // address before applying a later range's compatibility translation.
        if (find(nativeTarget))
        {
            rememberInlinePayload();
            return true;
        }
        ZoneSpan target = nativeTarget;
        if (!TranslateNativeBlock4Span(target)) return false;
        if (target.offset != nativeTarget.offset && find(target))
        {
            rememberInlinePayload();
            return true;
        }
        return false;
    }

    RetailCensusError FinishWorldWeaponSoundName(
        const std::shared_ptr<std::string> &name) noexcept
    {
        RetailPublishedWeaponDef &entry = result.worldWeapons[worldWeaponIndex];
        if (prerequisiteZone)
        {
            const std::shared_ptr<std::string> retained =
                name && ValidPublishedName(*name) ? name : nullptr;
            if (worldWeaponSoundIsBounce)
            {
                entry.storage->bounceSoundNames[worldWeaponSoundIndex] = retained;
                (*entry.storage->bounceSounds)[worldWeaponSoundIndex] = nullptr;
                ++worldWeaponBounceIndex;
            }
            else
            {
                entry.storage->soundNames[worldWeaponSoundIndex] = retained;
                AssignWeaponSound(*entry.asset, worldWeaponSoundIndex, nullptr);
                ++worldWeaponOperationIndex;
            }
            return RetailCensusError::None;
        }
        if (!name || !ValidPublishedName(*name))
            return RetailCensusError::WeaponSoundNameInvalid;
        if (soundLookup.function == nullptr)
            return RetailCensusError::WeaponSoundLookupFailed;
        snd_alias_list_t *alias =
            soundLookup.function(*name, soundLookup.userData);
        if (alias == nullptr)
            return RetailCensusError::WeaponSoundLookupFailed;
        if (worldWeaponSoundIsBounce)
        {
            if (!entry.storage->bounceSounds || worldWeaponSoundIndex >= 29u)
                return RetailCensusError::WeaponSoundNameInvalid;
            entry.storage->bounceSoundNames[worldWeaponSoundIndex] = name;
            (*entry.storage->bounceSounds)[worldWeaponSoundIndex] = alias;
            ++worldWeaponBounceIndex;
        }
        else
        {
            entry.storage->soundNames[worldWeaponSoundIndex] = name;
            AssignWeaponSound(*entry.asset, worldWeaponSoundIndex, alias);
            ++worldWeaponOperationIndex;
        }
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldWeaponDef(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_WEAPON ||
            (worldAssetReferences[assetIndex] != INLINE_POINTER &&
             worldAssetReferences[assetIndex] != SHARED_POINTER))
        {
            return RetailCensusError::WeaponLayoutUnsupported;
        }
        if (result.worldWeapons.size() >= limits.maxWeapons)
            return RetailCensusError::WeaponCollectionLimit;

        worldWeaponAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(worldWeaponAliasSlot, ASSET_TYPE_WEAPON));
            error != RetailCensusError::None)
        {
            return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan headerSpan;
        if (const RetailCensusError error = Plan(
                4u, WEAPON_DEF_BYTES, &headerSpan);
            error != RetailCensusError::None) return error;

        worldWeaponHasInsertAlias =
            worldAssetReferences[assetIndex] == SHARED_POINTER;
        worldWeaponInsertAliasSlot = {};
        if (worldWeaponHasInsertAlias)
        {
            if (const RetailCensusError error = Push(4u);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Plan(
                    4u, 4u, &worldWeaponInsertAliasSlot);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Pop();
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = MapRegistryError(
                    registry.ReserveAlias(
                        worldWeaponInsertAliasSlot, ASSET_TYPE_WEAPON));
                error != RetailCensusError::None) return error;
        }

        try
        {
            result.worldWeapons.emplace_back();
            RetailPublishedWeaponDef &entry = result.worldWeapons.back();
            entry.storage = std::make_shared<CanonicalWeaponDefStorage>();
            entry.asset = std::make_shared<WeaponDef>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        worldWeaponIndex = result.worldWeapons.size() - 1u;
        RetailPublishedWeaponDef &entry = result.worldWeapons.back();
        *entry.asset = {};
        entry.assetIndex = assetIndex;
        entry.serializedReference = worldAssetReferences[assetIndex];
        entry.headerBlock0Offset = headerSpan.offset;
        entry.stringBlock4Offsets.fill(UINT32_MAX);
        entry.soundNameCellBlock4Offsets.fill(UINT32_MAX);
        entry.soundNameStringBlock4Offsets.fill(UINT32_MAX);
        entry.bounceSoundNameCellBlock4Offsets.fill(UINT32_MAX);
        entry.bounceSoundNameStringBlock4Offsets.fill(UINT32_MAX);
        entry.accuracyKnotBlock4Offsets.fill(UINT32_MAX);
        if (worldWeaponHasInsertAlias)
            entry.insertPointerBlock4Offset = worldWeaponInsertAliasSlot.offset;
        if (const RetailCensusError error = AppendSemanticTrace(
                kisak::database::SemanticTraceEventKind::AssetBegin,
                ASSET_TYPE_WEAPON,
                assetIndex,
                0u,
                static_cast<std::uint32_t>(cursor),
                headerSpan,
                {},
                worldWeaponAliasSlot);
            error != RetailCensusError::None)
        {
            return error;
        }
        worldWeaponOperationIndex = 0u;
        worldWeaponStringReferences.fill(0u);
        worldWeaponXModelReferences.fill(0u);
        worldWeaponFxReferences.fill(0u);
        worldWeaponMaterialReferences.fill(0u);
        worldWeaponSoundReferences.fill(0u);
        worldWeaponBounceSoundReference = 0u;
        worldWeaponBounceSoundReferences.fill(0u);
        worldWeaponSoundStringReference = 0u;
        worldWeaponSoundIndex = 0u;
        worldWeaponSoundIsBounce = false;
        worldWeaponBounceActive = false;
        worldWeaponBounceIndex = 0u;
        worldWeaponAccuracyReferences.fill(0u);
        result.worldNextAssetIndex = assetIndex;
        result.nextBodyIndex = assetIndex;
        result.nextBodyType = ASSET_TYPE_WEAPON;
        result.nextBodyReference = worldAssetReferences[assetIndex];
        stage = RetailCensusStage::WorldWeaponDef;
        return RetailCensusError::None;
    }

    RetailCensusError ScheduleWorldWeaponOperation(
        RetailCensusStage &stage) noexcept
    {
        RetailPublishedWeaponDef &entry = result.worldWeapons[worldWeaponIndex];
        WeaponDef &weapon = *entry.asset;
        while (worldWeaponOperationIndex < WEAPON_OPERATIONS.size())
        {
            if (worldWeaponBounceActive)
            {
                if (worldWeaponBounceIndex >=
                    worldWeaponBounceSoundReferences.size())
                {
                    weapon.bounceSound = entry.storage->bounceSounds->data();
                    worldWeaponBounceActive = false;
                    ++worldWeaponOperationIndex;
                    continue;
                }
                worldWeaponSoundIsBounce = true;
                worldWeaponSoundIndex = worldWeaponBounceIndex;
                const std::uint32_t token =
                    worldWeaponBounceSoundReferences[worldWeaponBounceIndex];
                if (token == 0u)
                {
                    (*entry.storage->bounceSounds)[worldWeaponBounceIndex] = nullptr;
                    ++worldWeaponBounceIndex;
                    continue;
                }
                if (token == INLINE_POINTER)
                {
                    if (4u > limits.maxWeaponPayloadBytes - entry.payloadBytes ||
                        4u > limits.maxRetainedWeaponBytes - retainedWeaponBytes)
                    {
                        return RetailCensusError::WeaponPayloadLimit;
                    }
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(4u, 4u, &span);
                        error != RetailCensusError::None) return error;
                    entry.bounceSoundNameCellBlock4Offsets[
                        worldWeaponBounceIndex] = span.offset;
                    entry.payloadBytes += 4u;
                    retainedWeaponBytes += 4u;
                    stage = RetailCensusStage::WorldWeaponSoundNameCell;
                    return RetailCensusError::None;
                }
                if (token == SHARED_POINTER)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    (*entry.storage->bounceSounds)[worldWeaponSoundIndex] = nullptr;
                    ++worldWeaponBounceIndex;
                    continue;
                }
                std::shared_ptr<std::string> name;
                if (!ResolvePriorWeaponSoundCell(token, name))
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    (*entry.storage->bounceSounds)[worldWeaponSoundIndex] = nullptr;
                    ++worldWeaponBounceIndex;
                    continue;
                }
                if (const RetailCensusError error =
                        FinishWorldWeaponSoundName(name);
                    error != RetailCensusError::None) return error;
                continue;
            }

            const WorldWeaponOperation operation =
                WEAPON_OPERATIONS[worldWeaponOperationIndex];
            if (operation.kind == WorldWeaponOperationKind::String)
            {
                const std::uint32_t token =
                    worldWeaponStringReferences[operation.index];
                if (token == 0u)
                {
                    AssignWeaponString(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                if (token == INLINE_POINTER)
                {
                    stage = RetailCensusStage::WorldWeaponString;
                    return RetailCensusError::None;
                }
                if (token == SHARED_POINTER)
                    return RetailCensusError::WeaponStringInvalid;
                std::shared_ptr<std::string> resolved;
                std::uint32_t resolvedOffset = UINT32_MAX;
                bool stringResolved = ResolvePriorZoneStringPayload(
                    token, resolved, resolvedOffset);
                if (!stringResolved &&
                    CalibrateBlock4BiasFromWeaponXString(token, entry))
                    stringResolved = ResolvePriorZoneStringPayload(
                        token, resolved, resolvedOffset);
                if (!stringResolved)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponStringInvalid;
                    AssignWeaponString(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                entry.storage->strings[operation.index] = std::move(resolved);
                entry.stringBlock4Offsets[operation.index] = resolvedOffset;
                AssignWeaponString(
                    weapon,
                    operation.index,
                    entry.storage->strings[operation.index]->c_str());
                ++worldWeaponOperationIndex;
                continue;
            }

            if (operation.kind == WorldWeaponOperationKind::XModel)
            {
                const std::uint32_t token =
                    worldWeaponXModelReferences[operation.index];
                if (token == 0u)
                {
                    AssignWeaponXModel(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                if (token == INLINE_POINTER || token == SHARED_POINTER)
                {
                    if (!prerequisiteZone || token == INLINE_POINTER)
                        return RetailCensusError::WeaponDependencyUnsupported;
                    AssignWeaponXModel(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                void *resolved = nullptr;
                if (weapon_loader::ResolveCanonicalDependency(
                        *this, token, ASSET_TYPE_XMODEL, resolved) !=
                    RetailCensusError::None)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponDependencyUnsupported;
                    AssignWeaponXModel(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                AssignWeaponXModel(
                    weapon, operation.index, static_cast<XModel *>(resolved));
                ++worldWeaponOperationIndex;
                continue;
            }

            if (operation.kind == WorldWeaponOperationKind::Fx)
            {
                const std::uint32_t token =
                    worldWeaponFxReferences[operation.index];
                if (token == 0u)
                {
                    AssignWeaponFx(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                if (token == INLINE_POINTER || token == SHARED_POINTER)
                {
                    if (!prerequisiteZone || token == INLINE_POINTER)
                        return RetailCensusError::WeaponDependencyUnsupported;
                    AssignWeaponFx(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                void *resolved = nullptr;
                if (weapon_loader::ResolveCanonicalDependency(
                        *this, token, ASSET_TYPE_FX, resolved) !=
                    RetailCensusError::None)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponDependencyUnsupported;
                    AssignWeaponFx(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                AssignWeaponFx(weapon, operation.index,
                    static_cast<const FxEffectDef *>(resolved));
                ++worldWeaponOperationIndex;
                continue;
            }

            if (operation.kind == WorldWeaponOperationKind::Material)
            {
                const std::uint32_t token =
                    worldWeaponMaterialReferences[operation.index];
                if (token == 0u)
                {
                    AssignWeaponMaterial(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                if (token == INLINE_POINTER || token == SHARED_POINTER)
                {
                    worldMaterialTopLevel = false;
                    worldMaterialOwnedByFx = false;
                    worldMaterialOwnedByWeapon = true;
                    worldMaterialAliasSlot = {
                        0u,
                        entry.headerBlock0Offset +
                            WEAPON_MATERIAL_OFFSETS[operation.index],
                        4u,
                    };
                    worldMaterialHasInsertAlias = token == SHARED_POINTER;
                    worldMaterialInsertAliasSlot = {};
                    if (worldMaterialHasInsertAlias)
                    {
                        if (const RetailCensusError error = Plan(
                                4u, 4u, &worldMaterialInsertAliasSlot);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = MapRegistryError(
                                registry.ReserveAlias(
                                    worldMaterialInsertAliasSlot,
                                    ASSET_TYPE_MATERIAL));
                            error != RetailCensusError::None) return error;
                    }
                    if (const RetailCensusError error = Push(0u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, MATERIAL_BYTES, &span);
                        error != RetailCensusError::None) return error;
                    try
                    {
                        result.worldMaterials.emplace_back();
                        result.worldMaterials.back().asset =
                            std::make_shared<Material>();
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    RetailXModelMaterial &material =
                        result.worldMaterials.back();
                    *material.asset = {};
                    material.handleIndex = operation.index;
                    material.headerBlock0Offset = span.offset;
                    worldTextureIndex = 0u;
                    stage = RetailCensusStage::WorldXModelMaterial;
                    return RetailCensusError::None;
                }
                void *resolvedAsset = nullptr;
                if (weapon_loader::ResolveCanonicalDependency(
                        *this, token, ASSET_TYPE_MATERIAL, resolvedAsset) !=
                    RetailCensusError::None)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponDependencyUnsupported;
                    AssignWeaponMaterial(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                AssignWeaponMaterial(weapon, operation.index,
                    static_cast<Material *>(resolvedAsset));
                ++worldWeaponOperationIndex;
                continue;
            }

            if (operation.kind == WorldWeaponOperationKind::Sound)
            {
                const std::uint32_t token =
                    worldWeaponSoundReferences[operation.index];
                if (token == 0u)
                {
                    AssignWeaponSound(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                worldWeaponSoundIsBounce = false;
                worldWeaponSoundIndex = operation.index;
                if (token == INLINE_POINTER)
                {
                    if (4u > limits.maxWeaponPayloadBytes - entry.payloadBytes ||
                        4u > limits.maxRetainedWeaponBytes - retainedWeaponBytes)
                    {
                        return RetailCensusError::WeaponPayloadLimit;
                    }
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(4u, 4u, &span);
                        error != RetailCensusError::None) return error;
                    entry.soundNameCellBlock4Offsets[operation.index] = span.offset;
                    entry.payloadBytes += 4u;
                    retainedWeaponBytes += 4u;
                    stage = RetailCensusStage::WorldWeaponSoundNameCell;
                    return RetailCensusError::None;
                }
                if (token == SHARED_POINTER)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    AssignWeaponSound(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                std::shared_ptr<std::string> name;
                if (!ResolvePriorWeaponSoundCell(token, name))
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    AssignWeaponSound(weapon, operation.index, nullptr);
                    ++worldWeaponOperationIndex;
                    continue;
                }
                if (const RetailCensusError error =
                        FinishWorldWeaponSoundName(name);
                    error != RetailCensusError::None) return error;
                continue;
            }

            if (operation.kind == WorldWeaponOperationKind::BounceSound)
            {
                const std::uint32_t token = worldWeaponBounceSoundReference;
                if (token == 0u)
                {
                    weapon.bounceSound = nullptr;
                    ++worldWeaponOperationIndex;
                    continue;
                }
                if (token == INLINE_POINTER)
                {
                    constexpr std::uint32_t bytes = 29u * 4u;
                    if (bytes > limits.maxWeaponPayloadBytes - entry.payloadBytes ||
                        bytes > limits.maxRetainedWeaponBytes - retainedWeaponBytes)
                    {
                        return RetailCensusError::WeaponPayloadLimit;
                    }
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(4u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    entry.bounceSoundArrayBlock4Offset = span.offset;
                    entry.payloadBytes += bytes;
                    retainedWeaponBytes += bytes;
                    stage = RetailCensusStage::WorldWeaponBounceSoundCells;
                    return RetailCensusError::None;
                }
                if (token == SHARED_POINTER)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    weapon.bounceSound = nullptr;
                    ++worldWeaponOperationIndex;
                    continue;
                }
                ZoneSpan target;
                if (!DecodeZoneAliasToken(token, target) || target.block != 4u ||
                    !TranslateNativeBlock4Span(target))
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    weapon.bounceSound = nullptr;
                    ++worldWeaponOperationIndex;
                    continue;
                }
                const auto found = std::find_if(
                    result.worldWeapons.begin(), result.worldWeapons.end(),
                    [&](const RetailPublishedWeaponDef &candidate) {
                        return candidate.storage &&
                            candidate.storage->bounceSounds &&
                            candidate.bounceSoundArrayBlock4Offset == target.offset;
                    });
                if (found == result.worldWeapons.end())
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    weapon.bounceSound = nullptr;
                    ++worldWeaponOperationIndex;
                    continue;
                }
                entry.storage->bounceSounds = found->storage->bounceSounds;
                entry.bounceSoundArrayBlock4Offset = target.offset;
                weapon.bounceSound = entry.storage->bounceSounds->data();
                ++worldWeaponOperationIndex;
                continue;
            }

            const std::uint32_t knotIndex = operation.index;
            const std::uint32_t token =
                worldWeaponAccuracyReferences[knotIndex];
            const std::uint32_t graphIndex = knotIndex < 2u ? 0u : 1u;
            const int signedCount = weapon.accuracyGraphKnotCount[graphIndex];
            if (signedCount < 0 ||
                static_cast<std::uint32_t>(signedCount) >
                    limits.maxWeaponAccuracyKnots)
            {
                return RetailCensusError::WeaponAccuracyInvalid;
            }
            if (token == 0u)
            {
                AssignWeaponAccuracyKnots(weapon, knotIndex, nullptr);
                ++worldWeaponOperationIndex;
                continue;
            }
            if (token == INLINE_POINTER)
            {
                const std::uint64_t bytes =
                    static_cast<std::uint64_t>(signedCount) * 8u;
                if (bytes > limits.maxWeaponPayloadBytes - entry.payloadBytes ||
                    bytes > limits.maxRetainedWeaponBytes - retainedWeaponBytes)
                {
                    return RetailCensusError::WeaponPayloadLimit;
                }
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, bytes, &span);
                    error != RetailCensusError::None) return error;
                entry.accuracyKnotBlock4Offsets[knotIndex] = span.offset;
                entry.payloadBytes += static_cast<std::uint32_t>(bytes);
                retainedWeaponBytes += bytes;
                stage = RetailCensusStage::WorldWeaponAccuracyKnots;
                return RetailCensusError::None;
            }
            if (token == SHARED_POINTER)
                return RetailCensusError::WeaponAccuracyInvalid;
            ZoneSpan target;
            if (!DecodeZoneAliasToken(token, target) || target.block != 4u ||
                !TranslateNativeBlock4Span(target))
                return RetailCensusError::WeaponAccuracyInvalid;
            bool found = false;
            for (std::size_t weaponIndex = 0u;
                 weaponIndex <= worldWeaponIndex && !found;
                 ++weaponIndex)
            {
                RetailPublishedWeaponDef &candidate =
                    result.worldWeapons[weaponIndex];
                if (!candidate.storage) continue;
                for (std::size_t candidateIndex = 0u;
                     candidateIndex < candidate.accuracyKnotBlock4Offsets.size();
                     ++candidateIndex)
                {
                    if (candidate.accuracyKnotBlock4Offsets[candidateIndex] !=
                            target.offset ||
                        !candidate.storage->accuracyKnots[candidateIndex])
                    {
                        continue;
                    }
                    entry.storage->accuracyKnots[knotIndex] =
                        candidate.storage->accuracyKnots[candidateIndex];
                    entry.accuracyKnotBlock4Offsets[knotIndex] = target.offset;
                    AssignWeaponAccuracyKnots(
                        weapon,
                        knotIndex,
                        reinterpret_cast<float (*)[WEAP_ACCURACY_COUNT]>(
                            entry.storage->accuracyKnots[knotIndex]->data()));
                    found = true;
                    break;
                }
            }
            if (!found && prerequisiteZone && ValidDeclaredZonePointer(token, 4u))
            {
                AssignWeaponAccuracyKnots(weapon, knotIndex, nullptr);
                ++worldWeaponOperationIndex;
                continue;
            }
            if (!found) return RetailCensusError::WeaponAccuracyInvalid;
            ++worldWeaponOperationIndex;
        }
        stage = RetailCensusStage::WorldWeaponPublish;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldXAnimParts(
        std::uint32_t assetIndex,
        RetailCensusStage &stage) noexcept
    {
        if (assetIndex >= worldAssetTypes.size() ||
            worldAssetTypes[assetIndex] != ASSET_TYPE_XANIM_PARTS ||
            (worldAssetReferences[assetIndex] != INLINE_POINTER &&
             worldAssetReferences[assetIndex] != SHARED_POINTER))
        {
            return RetailCensusError::XAnimLayoutUnsupported;
        }
        if (result.worldXAnimParts.size() >= limits.maxXAnimParts)
            return RetailCensusError::XAnimCollectionLimit;
        worldXAnimAliasSlot = {
            4u,
            result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
            4u,
        };
        if (const RetailCensusError error = MapRegistryError(
                registry.ReserveAlias(
                    worldXAnimAliasSlot, ASSET_TYPE_XANIM_PARTS));
            error != RetailCensusError::None)
        {
            return error;
        }
        if (const RetailCensusError error = Push(0u);
            error != RetailCensusError::None) return error;
        ZoneSpan headerSpan;
        if (const RetailCensusError error = Plan(
                4u, XANIM_PARTS_BYTES, &headerSpan);
            error != RetailCensusError::None) return error;

        worldXAnimHasInsertAlias =
            worldAssetReferences[assetIndex] == SHARED_POINTER;
        worldXAnimInsertAliasSlot = {};
        if (worldXAnimHasInsertAlias)
        {
            if (const RetailCensusError error = Push(4u);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Plan(
                    4u, 4u, &worldXAnimInsertAliasSlot);
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = Pop();
                error != RetailCensusError::None) return error;
            if (const RetailCensusError error = MapRegistryError(
                    registry.ReserveAlias(
                        worldXAnimInsertAliasSlot, ASSET_TYPE_XANIM_PARTS));
                error != RetailCensusError::None) return error;
        }

        try
        {
            result.worldXAnimParts.emplace_back();
            RetailPublishedXAnimParts &entry = result.worldXAnimParts.back();
            entry.storage = std::make_shared<CanonicalXAnimPartsStorage>();
            entry.asset = std::make_shared<XAnimParts>();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        worldXAnimIndex = result.worldXAnimParts.size() - 1u;
        RetailPublishedXAnimParts &entry = result.worldXAnimParts.back();
        *entry.asset = {};
        entry.assetIndex = assetIndex;
        entry.serializedReference = worldAssetReferences[assetIndex];
        entry.headerBlock0Offset = headerSpan.offset;
        if (worldXAnimHasInsertAlias)
            entry.insertPointerBlock4Offset = worldXAnimInsertAliasSlot.offset;
        if (const RetailCensusError error = AppendSemanticTrace(
                kisak::database::SemanticTraceEventKind::AssetBegin,
                ASSET_TYPE_XANIM_PARTS,
                assetIndex,
                0u,
                static_cast<std::uint32_t>(cursor),
                headerSpan,
                {},
                worldXAnimAliasSlot);
            error != RetailCensusError::None)
        {
            return error;
        }
        worldXAnimPhase = WorldXAnimPhase::BoneNames;
        worldXAnimNameReference = 0u;
        worldXAnimNamesReference = 0u;
        worldXAnimNotifyReference = 0u;
        worldXAnimDeltaReference = 0u;
        worldXAnimDataByteReference = 0u;
        worldXAnimDataShortReference = 0u;
        worldXAnimDataIntReference = 0u;
        worldXAnimRandomShortReference = 0u;
        worldXAnimRandomByteReference = 0u;
        worldXAnimRandomIntReference = 0u;
        worldXAnimIndicesReference = 0u;
        worldXAnimDeltaTransReference = 0u;
        worldXAnimDeltaQuatReference = 0u;
        worldXAnimDeltaTransFramesReference = 0u;
        worldXAnimDeltaQuatFramesReference = 0u;
        worldXAnimDeltaTransSize = 0u;
        worldXAnimDeltaQuatSize = 0u;
        worldXAnimDeltaTransSmall = false;
        result.worldNextAssetIndex = assetIndex;
        result.nextBodyIndex = assetIndex;
        result.nextBodyType = ASSET_TYPE_XANIM_PARTS;
        result.nextBodyReference = worldAssetReferences[assetIndex];
        stage = RetailCensusStage::WorldXAnimParts;
        return RetailCensusError::None;
    }

    RetailCensusError PlanWorldXAnimPayload(
        std::uint32_t alignment,
        std::uint64_t bytes) noexcept
    {
        RetailPublishedXAnimParts &entry = result.worldXAnimParts[worldXAnimIndex];
        if (bytes > limits.maxXAnimPayloadBytes - entry.payloadBytes ||
            bytes > limits.maxRetainedXAnimBytes - retainedXAnimBytes)
        {
            return RetailCensusError::XAnimPayloadLimit;
        }
        if (const RetailCensusError error = Plan(alignment, bytes);
            error != RetailCensusError::None) return error;
        entry.payloadBytes += static_cast<std::uint32_t>(bytes);
        retainedXAnimBytes += bytes;
        return RetailCensusError::None;
    }

    RetailCensusError ScheduleWorldXAnimPayload(
        RetailCensusStage &stage) noexcept
    {
        XAnimParts &parts = *result.worldXAnimParts[worldXAnimIndex].asset;
        for (;;)
        {
            std::uint64_t bytes = 0u;
            std::uint32_t alignment = 1u;
            bool present = false;
            switch (worldXAnimPhase)
            {
            case WorldXAnimPhase::BoneNames:
                present = worldXAnimNamesReference != 0u;
                bytes = static_cast<std::uint64_t>(parts.boneCount[9]) * 2u;
                alignment = 2u;
                break;
            case WorldXAnimPhase::Notify:
                present = worldXAnimNotifyReference != 0u;
                bytes = static_cast<std::uint64_t>(parts.notifyCount) *
                    XANIM_NOTIFY_BYTES;
                alignment = 4u;
                break;
            case WorldXAnimPhase::DeltaPart:
                present = worldXAnimDeltaReference != 0u;
                bytes = XANIM_DELTA_PART_BYTES;
                alignment = 4u;
                break;
            case WorldXAnimPhase::DeltaTransHeader:
                present = worldXAnimDeltaTransReference != 0u;
                bytes = XANIM_DELTA_TRANS_HEADER_BYTES;
                alignment = 4u;
                break;
            case WorldXAnimPhase::DeltaTransFrames:
                present = true;
                bytes = worldXAnimDeltaTransSize == 0u
                    ? 12u : XANIM_DELTA_TRANS_FRAMES_BYTES;
                alignment = 1u;
                break;
            case WorldXAnimPhase::DeltaTransIndices:
                present = worldXAnimDeltaTransSize != 0u;
                bytes = (static_cast<std::uint64_t>(worldXAnimDeltaTransSize) + 1u) *
                    (parts.numframes >= 0x100u ? 2u : 1u);
                alignment = 1u;
                break;
            case WorldXAnimPhase::DeltaTransData:
                present = worldXAnimDeltaTransSize != 0u &&
                    worldXAnimDeltaTransFramesReference != 0u;
                bytes = (static_cast<std::uint64_t>(worldXAnimDeltaTransSize) + 1u) *
                    (worldXAnimDeltaTransSmall ? 3u : 6u);
                alignment = worldXAnimDeltaTransSmall ? 1u : 4u;
                break;
            case WorldXAnimPhase::DeltaQuatHeader:
                present = worldXAnimDeltaQuatReference != 0u;
                bytes = XANIM_DELTA_QUAT_HEADER_BYTES;
                alignment = 4u;
                break;
            case WorldXAnimPhase::DeltaQuatFrames:
                present = true;
                bytes = worldXAnimDeltaQuatSize == 0u
                    ? 4u : XANIM_DELTA_QUAT_FRAMES_BYTES;
                alignment = 1u;
                break;
            case WorldXAnimPhase::DeltaQuatIndices:
                present = worldXAnimDeltaQuatSize != 0u;
                bytes = (static_cast<std::uint64_t>(worldXAnimDeltaQuatSize) + 1u) *
                    (parts.numframes >= 0x100u ? 2u : 1u);
                alignment = 1u;
                break;
            case WorldXAnimPhase::DeltaQuatData:
                present = worldXAnimDeltaQuatSize != 0u &&
                    worldXAnimDeltaQuatFramesReference != 0u;
                bytes = (static_cast<std::uint64_t>(worldXAnimDeltaQuatSize) + 1u) * 4u;
                alignment = 4u;
                break;
            case WorldXAnimPhase::DataByte:
                present = worldXAnimDataByteReference != 0u;
                bytes = parts.dataByteCount;
                break;
            case WorldXAnimPhase::DataShort:
                present = worldXAnimDataShortReference != 0u;
                bytes = static_cast<std::uint64_t>(parts.dataShortCount) * 2u;
                alignment = 2u;
                break;
            case WorldXAnimPhase::DataInt:
                present = worldXAnimDataIntReference != 0u;
                bytes = static_cast<std::uint64_t>(parts.dataIntCount) * 4u;
                alignment = 4u;
                break;
            case WorldXAnimPhase::RandomDataShort:
                present = worldXAnimRandomShortReference != 0u;
                bytes = static_cast<std::uint64_t>(parts.randomDataShortCount) * 2u;
                alignment = 2u;
                break;
            case WorldXAnimPhase::RandomDataByte:
                present = worldXAnimRandomByteReference != 0u;
                bytes = parts.randomDataByteCount;
                break;
            case WorldXAnimPhase::RandomDataInt:
                present = worldXAnimRandomIntReference != 0u;
                bytes = static_cast<std::uint64_t>(parts.randomDataIntCount) * 4u;
                alignment = 4u;
                break;
            case WorldXAnimPhase::Indices:
                present = worldXAnimIndicesReference != 0u;
                if (parts.indexCount > limits.maxXAnimIndices)
                    return RetailCensusError::XAnimPayloadLimit;
                bytes = static_cast<std::uint64_t>(parts.indexCount) *
                    (parts.numframes >= 0x100u ? 2u : 1u);
                alignment = parts.numframes >= 0x100u ? 2u : 1u;
                break;
            case WorldXAnimPhase::Complete:
                stage = RetailCensusStage::WorldXAnimPublish;
                return RetailCensusError::None;
            }
            if (present)
            {
                if (const RetailCensusError error =
                        PlanWorldXAnimPayload(alignment, bytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXAnimPayload;
                return RetailCensusError::None;
            }

            if (worldXAnimPhase == WorldXAnimPhase::DeltaPart)
                worldXAnimPhase = WorldXAnimPhase::DataByte;
            else if (worldXAnimPhase == WorldXAnimPhase::DeltaTransHeader)
                worldXAnimPhase = WorldXAnimPhase::DeltaQuatHeader;
            else if (worldXAnimPhase == WorldXAnimPhase::DeltaTransData)
                worldXAnimPhase = WorldXAnimPhase::DeltaQuatHeader;
            else if (worldXAnimPhase == WorldXAnimPhase::DeltaQuatHeader)
                worldXAnimPhase = WorldXAnimPhase::DataByte;
            else if (worldXAnimPhase == WorldXAnimPhase::DeltaQuatData)
                worldXAnimPhase = WorldXAnimPhase::DataByte;
            else
                worldXAnimPhase = static_cast<WorldXAnimPhase>(
                    static_cast<std::uint8_t>(worldXAnimPhase) + 1u);
        }
    }

    bool ValidPriorZonePointer(
        std::uint32_t token,
        std::uint32_t alignment = 1u) const noexcept
    {
        if (token == 0u || token == INLINE_POINTER || token == SHARED_POINTER ||
            alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        {
            return false;
        }
        const std::uint32_t packed = token - 1u;
        const std::uint32_t block = packed >> 28u;
        const std::uint32_t offset = packed & 0x0fffffffu;
        return block < ZONE_STREAM_BLOCK_COUNT &&
            (offset & (alignment - 1u)) == 0u &&
            offset < arenas.DeclaredSize(block) &&
            4u <= arenas.DeclaredSize(block) - offset &&
            offset < arenas.HighWater(block);
    }

    bool ValidDeclaredZonePointer(
        std::uint32_t token,
        std::uint32_t alignment = 1u) const noexcept
    {
        ZoneSpan target;
        return alignment != 0u &&
            (alignment & (alignment - 1u)) == 0u &&
            DecodeZoneAliasToken(token, target) &&
            (target.offset & (alignment - 1u)) == 0u &&
            target.block < ZONE_STREAM_BLOCK_COUNT &&
            target.offset < arenas.DeclaredSize(target.block) &&
            4u <= arenas.DeclaredSize(target.block) - target.offset;
    }

    template <typename T>
    RetailCensusError ResolvePriorWorldXModelArray(
        std::uint32_t token,
        std::size_t count,
        std::uint32_t alignment,
        const std::vector<T> RetailWorldXModel::*valuesMember,
        const std::uint32_t RetailWorldXModel::*offsetMember,
        std::vector<T> &values,
        std::uint32_t &resolvedOffset,
        RetailCensusError invalidAlias) noexcept
    {
        ZoneSpan target;
        if (alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
            !DecodeZoneAliasToken(token, target) || target.block != 4u ||
            !TranslateNativeBlock4Span(target) ||
            (target.offset & (alignment - 1u)) != 0u ||
            count > std::numeric_limits<std::uint32_t>::max() / sizeof(T))
        {
            return invalidAlias;
        }
        const std::uint32_t bytes =
            static_cast<std::uint32_t>(count * sizeof(T));
        if (target.offset > arenas.DeclaredSize(4u) ||
            bytes > arenas.DeclaredSize(4u) - target.offset ||
            target.offset > arenas.HighWater(4u) ||
            bytes > arenas.HighWater(4u) - target.offset)
        {
            return invalidAlias;
        }
        const RetailWorldXModel *source = nullptr;
        std::size_t sourceIndex = 0u;
        for (const RetailWorldXModel &candidate : result.worldXModels)
        {
            const std::vector<T> &candidateValues = candidate.*valuesMember;
            const std::uint32_t candidateOffset = candidate.*offsetMember;
            if (!candidate.published || target.offset < candidateOffset)
                continue;
            const std::uint32_t delta = target.offset - candidateOffset;
            if (delta % sizeof(T) != 0u) continue;
            const std::size_t begin = delta / sizeof(T);
            if (begin <= candidateValues.size() &&
                count <= candidateValues.size() - begin)
            {
                source = &candidate;
                sourceIndex = begin;
                break;
            }
        }
        if (source == nullptr)
        {
            if (!prerequisiteZone) return invalidAlias;
            values.clear();
            resolvedOffset = target.offset;
            return RetailCensusError::None;
        }
        const std::vector<T> &sourceValues = source->*valuesMember;
        std::vector<T> assigned;
        try
        {
            assigned.assign(
                sourceValues.begin() + static_cast<std::ptrdiff_t>(sourceIndex),
                sourceValues.begin() + static_cast<std::ptrdiff_t>(
                    sourceIndex + count));
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        values.swap(assigned);
        resolvedOffset = target.offset;
        return RetailCensusError::None;
    }

    RetailCensusError AssignWorldXModelBoneNames(
        RetailWorldXModel &model,
        std::span<const std::uint16_t> indices) noexcept
    {
        if (indices.size() != model.numBones)
            return RetailCensusError::XModelScriptStringInvalid;
        std::vector<std::uint16_t> assignedIndices;
        std::vector<std::string> assignedNames;
        try
        {
            assignedIndices.assign(indices.begin(), indices.end());
            assignedNames.reserve(indices.size());
            for (const std::uint16_t token : indices)
            {
                if (token >= scriptStrings.size())
                    return RetailCensusError::XModelScriptStringInvalid;
                assignedNames.push_back(scriptStrings[token]);
            }
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        model.boneNameScriptStringIndices.swap(assignedIndices);
        model.boneNames.swap(assignedNames);
        return RetailCensusError::None;
    }

    RetailCensusError ResolvePriorWorldXModelBoneNames(
        RetailWorldXModel &model) noexcept
    {
        std::vector<std::uint16_t> indices;
        std::uint32_t resolvedOffset = 0u;
        if (const RetailCensusError error = ResolvePriorWorldXModelArray(
                model.boneNamesReference, model.numBones, 2u,
                &RetailWorldXModel::boneNameScriptStringIndices,
                &RetailWorldXModel::boneNamesBlock4Offset,
                indices, resolvedOffset,
                RetailCensusError::XModelScriptStringAliasInvalid);
            error != RetailCensusError::None)
        {
            return error;
        }
        if (prerequisiteZone && indices.empty() && model.numBones != 0u)
        {
            model.boneNamesBlock4Offset = resolvedOffset;
            return RetailCensusError::None;
        }
        if (const RetailCensusError error =
                AssignWorldXModelBoneNames(
                    model, std::span<const std::uint16_t>(indices));
            error != RetailCensusError::None)
        {
            return error;
        }
        model.boneNamesBlock4Offset = resolvedOffset;
        return RetailCensusError::None;
    }

    RetailCensusError BeginWorldMaterialTechnique(
        std::uint32_t slot,
        RetailCensusStage &stage) noexcept
    {
        if (result.worldTechniqueSets.empty() || slot >= techniqueTokens.size() ||
            techniqueTokens[slot] != INLINE_POINTER)
        {
            return RetailCensusError::TechniqueReferenceUnsupported;
        }
        ZoneSpan span;
        if (const RetailCensusError error = Plan(
                4u, TECHNIQUE_HEADER_BYTES, &span);
            error != RetailCensusError::None)
        {
            return error;
        }
        try
        {
            result.worldTechniqueSets.back().techniques.emplace_back();
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        RetailWorldMaterialTechnique &technique =
            result.worldTechniqueSets.back().techniques.back();
        technique.slot = slot;
        technique.headerBlock4Offset = span.offset;
        worldMaterialPassIndex = 0u;
        worldMaterialPassPhase = WorldMaterialPassPhase::VertexDeclaration;
        worldMaterialPasses.clear();
        worldMaterialLiteralTokens.clear();
        worldMaterialLiteralIndex = 0u;
        worldMaterialTechniqueNameToken = 0u;
        worldMaterialShaderNameToken = 0u;
        worldMaterialShaderProgramBytes = 0u;
        worldMaterialArgumentBytes = 0u;
        stage = RetailCensusStage::WorldMaterialTechnique;
        return RetailCensusError::None;
    }

    RetailCensusError Parse(
        RetailCensusStepBudget budget,
        RetailCensusStepReport &report,
        RetailCensusStage &stage,
        bool &blocked,
        bool &complete) noexcept
    {
        blocked = false;
        complete = false;
        activeLoadBudget = &budget;
        activeLoadReport = &report;
        activeLoadBlocked = &blocked;
        auto visitRecord = [&](std::size_t bytes) noexcept -> int {
            if (!Available(bytes))
            {
                blocked = true;
                return -1;
            }
            const std::size_t byteBudget = budget.maxBytes - report.traversedBytes;
            const std::size_t count = std::min(bytes - recordVisited, byteBudget);
            recordVisited += count;
            report.traversedBytes += static_cast<std::uint32_t>(count);
            if (recordVisited != bytes) return 0;
            recordVisited = 0u;
            return 1;
        };
        auto activeWorldXModel = [&]() noexcept -> RetailWorldXModel & {
            return result.worldXModels[worldXModelIndex];
        };
        auto activeWorldFx = [&]() noexcept -> RetailWorldFxEffectDef & {
            return result.worldFxEffects[worldFxIndex];
        };
        auto activeWorldFxElem = [&]() noexcept -> RetailWorldFxElemDef & {
            return result.worldFxEffects[worldFxIndex].elemDefs[worldFxElemIndex];
        };
        auto activeWorldXAnim = [&]() noexcept -> RetailPublishedXAnimParts & {
            return result.worldXAnimParts[worldXAnimIndex];
        };
        auto activeWorldWeapon = [&]() noexcept -> RetailPublishedWeaponDef & {
            return result.worldWeapons[worldWeaponIndex];
        };
        auto activeWorldLocalize = [&]() noexcept
            -> RetailPublishedLocalizeEntry & {
            return result.worldLocalizeEntries[worldLocalizeIndex];
        };
        auto activeWorldSound = [&]() noexcept
            -> RetailPublishedSoundAliasList & {
            return result.worldSoundAliasLists[worldSoundIndex];
        };
        auto activeWorldMaterial = [&]() noexcept -> RetailXModelMaterial & {
            if (worldMaterialTopLevel || worldMaterialOwnedByWeapon ||
                worldMaterialOwnedByMenu)
                return result.worldMaterials.back();
            return worldMaterialOwnedByFx
                ? activeWorldFx().materials.back()
                : activeWorldXModel().materials.back();
        };
        auto prependWorldMenuTasks =
            [&](std::initializer_list<WorldMenuTask> tasks) noexcept
                -> RetailCensusError {
                try
                {
                    auto iterator = tasks.end();
                    while (iterator != tasks.begin())
                    {
                        --iterator;
                        worldMenuTasks.push_front(*iterator);
                    }
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                return RetailCensusError::None;
            };
        auto assignWorldSoundString = [&](WorldSoundStringTarget target,
                                           std::shared_ptr<std::string> value,
                                           std::uint32_t block4Offset) noexcept
            -> RetailCensusError {
                RetailPublishedSoundAliasList &entry = activeWorldSound();
                CanonicalSoundAliasListStorage &storage = *entry.storage;
                const char *text = value ? value->c_str() : nullptr;
                if (target == WorldSoundStringTarget::ListName)
                {
                    storage.aliasName = std::move(value);
                    entry.nameBlock4Offset = block4Offset;
                    entry.asset->aliasName = text;
                    return RetailCensusError::None;
                }
                if (worldSoundAliasIndex >= storage.aliasStrings.size() ||
                    !storage.aliases)
                {
                    return RetailCensusError::SoundAliasLayoutUnsupported;
                }
                snd_alias_t &alias =
                    (*storage.aliases)[worldSoundAliasIndex];
                const auto assignAlias = [&](std::size_t index,
                                             const char *pointer) noexcept {
                    storage.aliasStrings[worldSoundAliasIndex][index] =
                        std::move(value);
                    switch (index)
                    {
                    case 0u: alias.aliasName = pointer; break;
                    case 1u: alias.subtitle = pointer; break;
                    case 2u: alias.secondaryAliasName = pointer; break;
                    default: alias.chainAliasName = pointer; break;
                    }
                };
                switch (target)
                {
                case WorldSoundStringTarget::AliasName:
                    assignAlias(0u, text); break;
                case WorldSoundStringTarget::Subtitle:
                    assignAlias(1u, text); break;
                case WorldSoundStringTarget::SecondaryAliasName:
                    assignAlias(2u, text); break;
                case WorldSoundStringTarget::ChainAliasName:
                    assignAlias(3u, text); break;
                case WorldSoundStringTarget::FileDirectory:
                    storage.fileStrings[worldSoundAliasIndex][0u] =
                        std::move(value);
                    storage.soundFiles[worldSoundAliasIndex]->u.streamSnd
                        .filename.info.raw.dir = text;
                    break;
                case WorldSoundStringTarget::FileName:
                    storage.fileStrings[worldSoundAliasIndex][1u] =
                        std::move(value);
                    storage.soundFiles[worldSoundAliasIndex]->u.streamSnd
                        .filename.info.raw.name = text;
                    break;
                case WorldSoundStringTarget::CurveName:
                    storage.curveNames[worldSoundAliasIndex] =
                        std::move(value);
                    storage.curves[worldSoundAliasIndex]->filename = text;
                    break;
                case WorldSoundStringTarget::SpeakerMapName:
                    storage.speakerMapNames[worldSoundAliasIndex] =
                        std::move(value);
                    storage.speakerMaps[worldSoundAliasIndex]->name = text;
                    break;
                case WorldSoundStringTarget::ListName:
                    break;
                }
                return RetailCensusError::None;
            };
        auto scheduleWorldSoundComponent =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                RetailPublishedSoundAliasList &entry = activeWorldSound();
                CanonicalSoundAliasListStorage &storage = *entry.storage;
                while (worldSoundAliasIndex < worldSoundAliases.size())
                {
                    WorldSoundAliasState &state =
                        worldSoundAliases[worldSoundAliasIndex];
                    if (worldSoundComponentIndex < 4u)
                    {
                        worldSoundStringTarget = static_cast<WorldSoundStringTarget>(
                            static_cast<std::uint8_t>(
                                WorldSoundStringTarget::AliasName) +
                            worldSoundComponentIndex);
                        worldSoundStringReference =
                            state.stringReferences[worldSoundComponentIndex];
                        worldSoundStringBlock4Offset = UINT32_MAX;
                        if (worldSoundStringReference == 0u)
                        {
                            if (const RetailCensusError error =
                                    assignWorldSoundString(
                                        worldSoundStringTarget, nullptr,
                                        UINT32_MAX);
                                error != RetailCensusError::None) return error;
                            ++worldSoundComponentIndex;
                            continue;
                        }
                        nextStage = RetailCensusStage::WorldSoundAliasString;
                        return RetailCensusError::None;
                    }
                    if (worldSoundComponentIndex == 4u)
                    {
                        if (state.soundFileReference == 0u)
                        {
                            ++worldSoundComponentIndex;
                            continue;
                        }
                        if (state.soundFileReference != INLINE_POINTER)
                        {
                            ZoneSpan target;
                            bool found = false;
                            if (DecodeZoneAliasToken(
                                    state.soundFileReference, target) &&
                                target.block == 4u)
                            {
                                for (RetailPublishedSoundAliasList &candidate :
                                     result.worldSoundAliasLists)
                                {
                                    if (!candidate.storage) continue;
                                    for (std::size_t index = 0u;
                                         index < candidate.storage->
                                            soundFileBlock4Offsets.size();
                                         ++index)
                                    {
                                        if (candidate.storage->
                                                soundFileBlock4Offsets[index] !=
                                                target.offset ||
                                            !candidate.storage->soundFiles[index])
                                            continue;
                                        storage.soundFiles[worldSoundAliasIndex] =
                                            candidate.storage->soundFiles[index];
                                        storage.soundFileBlock4Offsets[
                                            worldSoundAliasIndex] = target.offset;
                                        (*storage.aliases)[worldSoundAliasIndex]
                                            .soundFile = storage.soundFiles[
                                                worldSoundAliasIndex].get();
                                        found = true;
                                        break;
                                    }
                                    if (found) break;
                                }
                            }
                            if (!found && !prerequisiteZone)
                                return RetailCensusError::SoundAliasDependencyUnsupported;
                            ++worldSoundComponentIndex;
                            continue;
                        }
                        ZoneSpan soundFileSpan;
                        if (const RetailCensusError error = Plan(
                                4u, SOUND_FILE_BYTES, &soundFileSpan);
                            error != RetailCensusError::None) return error;
                        storage.soundFileBlock4Offsets[worldSoundAliasIndex] =
                            soundFileSpan.offset;
                        nextStage = RetailCensusStage::WorldSoundAliasFile;
                        return RetailCensusError::None;
                    }
                    if (worldSoundComponentIndex == 5u)
                    {
                        if (state.curveReference == 0u)
                        {
                            ++worldSoundComponentIndex;
                            continue;
                        }
                        if (state.curveReference != INLINE_POINTER &&
                            state.curveReference != SHARED_POINTER)
                        {
                            // Curves are database assets. A prerequisite-zone
                            // alias may refer to an already registered curve;
                            // the sound list remains canonical even when this
                            // traversal does not retain that independent asset.
                            ++worldSoundComponentIndex;
                            continue;
                        }
                        if (const RetailCensusError error = Push(0u);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = Plan(
                                4u, SOUND_CURVE_BYTES);
                            error != RetailCensusError::None) return error;
                        worldSoundCurveHasInsertAlias =
                            state.curveReference == SHARED_POINTER;
                        if (worldSoundCurveHasInsertAlias)
                        {
                            if (const RetailCensusError error = Push(4u);
                                error != RetailCensusError::None) return error;
                            if (const RetailCensusError error = Plan(
                                    4u, 4u, &worldSoundCurveInsertAliasSlot);
                                error != RetailCensusError::None) return error;
                            if (const RetailCensusError error = Pop();
                                error != RetailCensusError::None) return error;
                        }
                        nextStage = RetailCensusStage::WorldSoundAliasCurve;
                        return RetailCensusError::None;
                    }
                    if (worldSoundComponentIndex == 6u)
                    {
                        if (state.speakerMapReference == 0u)
                        {
                            ++worldSoundComponentIndex;
                            continue;
                        }
                        if (state.speakerMapReference != INLINE_POINTER)
                        {
                            ++worldSoundComponentIndex;
                            continue;
                        }
                        if (const RetailCensusError error = Plan(
                                4u, SPEAKER_MAP_BYTES);
                            error != RetailCensusError::None) return error;
                        nextStage = RetailCensusStage::WorldSoundAliasSpeakerMap;
                        return RetailCensusError::None;
                    }
                    ++worldSoundAliasIndex;
                    worldSoundComponentIndex = 0u;
                }
                entry.asset->head = storage.aliases && !storage.aliases->empty()
                    ? storage.aliases->data() : nullptr;
                nextStage = RetailCensusStage::WorldSoundAliasPublish;
                return RetailCensusError::None;
            };
        auto hasCompletedWorldMaterialTechnique = [&]() noexcept {
            return std::any_of(
                result.worldTechniqueSets.begin(),
                result.worldTechniqueSets.end(),
                [](const RetailWorldTechniqueSet &set) {
                    return std::any_of(
                        set.techniques.begin(), set.techniques.end(),
                        [](const RetailWorldMaterialTechnique &technique) {
                            return technique.completed;
                        });
                });
        };
        auto finishWorldXModel = [&](const char *operation,
                                     bool dependency,
                                     bool surfaceArray) noexcept {
            RetailWorldXModel &model = activeWorldXModel();
            model.boundaryInflatedOffset = static_cast<std::uint32_t>(cursor);
            model.stoppedBeforeSurfaceArray = surfaceArray;
            result.block0HighWaterAtBoundary = arenas.HighWater(0u);
            result.block4CursorAtBoundary = arenas.Cursor(4u);
            result.worldRegistryAliasCount = registry.AliasCount();
            result.worldRegistryDefinedAliasCount = registry.DefinedAliasCount();
            result.registryAssetCount = registry.AssetCount();
            result.registryAliasCount = registry.AliasCount();
            result.registryDefinedAliasCount = registry.DefinedAliasCount();
            result.stoppedBeforeWorldXModelDependency = dependency;
            result.stoppedBeforeDifferentWorldAssetType = false;
            result.unsupportedOperation = operation;
            stage = RetailCensusStage::AssetBoundary;
            complete = true;
        };
        auto addPhysGeomPayload = [&](std::uint64_t bytes) noexcept {
            RetailWorldXModel &model = activeWorldXModel();
            const std::uint64_t total =
                static_cast<std::uint64_t>(model.physGeomPayloadBytes) + bytes;
            if (total > limits.maxPhysGeomPayloadBytes || total > UINT32_MAX)
                return RetailCensusError::PhysGeomPayloadLimit;
            model.physGeomPayloadBytes = static_cast<std::uint32_t>(total);
            return RetailCensusError::None;
        };
        auto scheduleWorldPhysGeomBrush =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                while (worldPhysGeomIndex < worldPhysGeomInfos.size())
                {
                    const std::uint32_t token =
                        worldPhysGeomInfos[worldPhysGeomIndex].brushReference;
                    if (token == 0u) { ++worldPhysGeomIndex; continue; }
                    if (token != INLINE_POINTER)
                    {
                        if (!ValidPriorZonePointer(token, 4u))
                            return RetailCensusError::PhysGeomBrushInvalid;
                        ++worldPhysGeomIndex;
                        continue;
                    }
                    if (const RetailCensusError error = Plan(
                            4u, BRUSH_WRAPPER_BYTES);
                        error != RetailCensusError::None) return error;
                    nextStage = RetailCensusStage::WorldPhysGeomBrush;
                    return RetailCensusError::None;
                }
                activeWorldXModel().physGeomsTraversed = true;
                nextStage = RetailCensusStage::WorldXModelPublish;
                return RetailCensusError::None;
            };
        auto publishWorldPhysPreset =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                RetailWorldXModel &model = activeWorldXModel();
                RetailXModelPhysPreset &preset = model.physPreset;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_PHYS_PRESET, model.assetIndex,
                            preset.name, preset.identity));
                    error != RetailCensusError::None) return error;
                if (worldPhysPresetHasInsertAlias)
                {
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                worldPhysPresetInsertAliasSlot,
                                preset.identity));
                        error != RetailCensusError::None) return error;
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                preset.traversed = true;
                preset.published = true;
                model.physPresetIdentity = preset.identity;
                model.physPresetTraversed = true;
                nextStage = RetailCensusStage::WorldXModelPhysGeoms;
                return RetailCensusError::None;
            };
        auto dispatchSupportedWorldAsset =
            [&](std::uint32_t index, RetailCensusStage &nextStage) noexcept
                -> RetailCensusError {
                result.worldNextAssetIndex = index;
                result.nextBodyIndex = index;
                result.stoppedBeforeWorldTechniqueDependency = false;
                result.stoppedBeforeWorldXModelDependency = false;
                result.unsupportedOperation = nullptr;
                if (index >= worldAssetTypes.size())
                {
                    result.nextBodyType = 0u;
                    result.nextBodyReference = 0u;
                    result.stoppedBeforeDifferentWorldAssetType = false;
                    nextStage = RetailCensusStage::AssetBoundary;
                    complete = true;
                    return RetailCensusError::None;
                }
                result.nextBodyType = worldAssetTypes[index];
                result.nextBodyReference = worldAssetReferences[index];
                if (result.nextBodyType == ASSET_TYPE_CLIPMAP ||
                    result.nextBodyType == ASSET_TYPE_CLIPMAP_PVS)
                {
                    clipMapLoader.Reset();
                    if (const RetailCensusError error = clipMapLoader.Begin(
                            *this, index, result.nextBodyType,
                            result.nextBodyReference);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    nextStage = RetailCensusStage::WorldClipMap;
                    return RetailCensusError::None;
                }
                if (result.nextBodyType == ASSET_TYPE_COMWORLD)
                {
                    comWorldLoader.Reset();
                    if (const RetailCensusError error = comWorldLoader.Begin(
                            *this, index, result.nextBodyReference);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    nextStage = RetailCensusStage::WorldComWorld;
                    return RetailCensusError::None;
                }
                if (result.nextBodyType == ASSET_TYPE_LIGHT_DEF)
                {
                    lightDefLoader.Reset();
                    if (const RetailCensusError error = lightDefLoader.Begin(
                            *this, index, result.nextBodyReference);
                        error != RetailCensusError::None)
                        return error;
                    nextStage = RetailCensusStage::WorldLightDef;
                    return RetailCensusError::None;
                }
                if (result.nextBodyType == ASSET_TYPE_SOUND &&
                    (result.nextBodyReference == INLINE_POINTER ||
                     result.nextBodyReference == SHARED_POINTER))
                {
                    return BeginWorldSoundAliasList(index, nextStage);
                }
                if (result.nextBodyType == ASSET_TYPE_SOUND)
                {
                    std::uint32_t identity = 0u;
                    if (ResolveRegistryAlias(
                            result.nextBodyReference,
                            ASSET_TYPE_SOUND,
                            identity) != ZoneRegistryError::None)
                    {
                        return RetailCensusError::SoundAliasDependencyUnsupported;
                    }
                    const auto found = std::find_if(
                        result.worldSoundAliasLists.begin(),
                        result.worldSoundAliasLists.end(),
                        [identity](const RetailPublishedSoundAliasList &entry) {
                            return entry.identity == identity && entry.asset;
                        });
                    if (found == result.worldSoundAliasLists.end())
                        return RetailCensusError::SoundAliasDependencyUnsupported;
                    try { result.worldSoundAliasLists.push_back(*found); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    RetailPublishedSoundAliasList &entry =
                        result.worldSoundAliasLists.back();
                    entry.assetIndex = index;
                    entry.serializedReference = result.nextBodyReference;
                    entry.pointerAlias = true;
                    entry.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    nextStage = RetailCensusStage::WorldSoundAliasPublish;
                    return RetailCensusError::None;
                }
                if (result.nextBodyType == ASSET_TYPE_MATERIAL &&
                    (result.nextBodyReference == INLINE_POINTER ||
                     result.nextBodyReference == SHARED_POINTER))
                {
                    return BeginWorldMaterial(index, nextStage);
                }
                if (result.nextBodyType == ASSET_TYPE_LOCALIZE &&
                    (result.nextBodyReference == INLINE_POINTER ||
                     result.nextBodyReference == SHARED_POINTER))
                {
                    return BeginWorldLocalizeEntry(index, nextStage);
                }
                if (result.nextBodyType == ASSET_TYPE_LOCALIZE)
                {
                    if (result.worldLocalizeEntries.size() >=
                        limits.maxLocalizeEntries)
                    {
                        return RetailCensusError::LocalizeCollectionLimit;
                    }
                    std::uint32_t identity = 0u;
                    std::shared_ptr<LocalizeEntry> priorAsset;
                    std::shared_ptr<CanonicalLocalizeEntryStorage> priorStorage;
                    if (result.nextBodyReference != 0u)
                    {
                        if (ResolveRegistryAlias(
                                result.nextBodyReference,
                                ASSET_TYPE_LOCALIZE,
                                identity) != ZoneRegistryError::None)
                        {
                            return RetailCensusError::LocalizeAliasInvalid;
                        }
                        const auto found = std::find_if(
                            result.worldLocalizeEntries.begin(),
                            result.worldLocalizeEntries.end(),
                            [identity](const RetailPublishedLocalizeEntry &entry) {
                                return entry.identity == identity && entry.asset;
                            });
                        if (found == result.worldLocalizeEntries.end())
                            return RetailCensusError::LocalizeAliasInvalid;
                        priorAsset = found->asset;
                        priorStorage = found->storage;
                    }
                    try { result.worldLocalizeEntries.emplace_back(); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    worldLocalizeIndex =
                        result.worldLocalizeEntries.size() - 1u;
                    RetailPublishedLocalizeEntry &entry =
                        result.worldLocalizeEntries.back();
                    entry.assetIndex = index;
                    entry.serializedReference = result.nextBodyReference;
                    entry.identity = identity;
                    entry.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    entry.pointerAlias = result.nextBodyReference != 0u;
                    entry.published = true;
                    entry.asset = std::move(priorAsset);
                    entry.storage = std::move(priorStorage);
                    nextStage = RetailCensusStage::WorldLocalizePublish;
                    return RetailCensusError::None;
                }
                if (result.nextBodyType == ASSET_TYPE_WEAPON &&
                    (result.nextBodyReference == INLINE_POINTER ||
                     result.nextBodyReference == SHARED_POINTER))
                {
                    return BeginWorldWeaponDef(index, nextStage);
                }
                if (result.nextBodyType == ASSET_TYPE_WEAPON)
                {
                    if (result.worldWeapons.size() >= limits.maxWeapons)
                        return RetailCensusError::WeaponCollectionLimit;
                    std::uint32_t identity = 0u;
                    std::shared_ptr<WeaponDef> priorAsset;
                    std::shared_ptr<CanonicalWeaponDefStorage> priorStorage;
                    std::array<std::uint32_t, 48> priorStringOffsets{};
                    std::array<std::uint32_t, 48> priorSoundCellOffsets{};
                    std::array<std::uint32_t, 48> priorSoundStringOffsets{};
                    std::array<std::uint32_t, 29> priorBounceCellOffsets{};
                    std::array<std::uint32_t, 29> priorBounceStringOffsets{};
                    std::uint32_t priorBounceArrayOffset = UINT32_MAX;
                    std::array<std::uint32_t, 4> priorKnotOffsets{};
                    if (result.nextBodyReference != 0u)
                    {
                        if (ResolveRegistryAlias(
                                result.nextBodyReference,
                                ASSET_TYPE_WEAPON,
                                identity) != ZoneRegistryError::None)
                        {
                            return RetailCensusError::WeaponAliasInvalid;
                        }
                        const auto found = std::find_if(
                            result.worldWeapons.begin(),
                            result.worldWeapons.end(),
                            [identity](const RetailPublishedWeaponDef &entry) {
                                return entry.identity == identity && entry.asset;
                            });
                        if (found == result.worldWeapons.end())
                            return RetailCensusError::WeaponAliasInvalid;
                        priorAsset = found->asset;
                        priorStorage = found->storage;
                        priorStringOffsets = found->stringBlock4Offsets;
                        priorSoundCellOffsets =
                            found->soundNameCellBlock4Offsets;
                        priorSoundStringOffsets =
                            found->soundNameStringBlock4Offsets;
                        priorBounceCellOffsets =
                            found->bounceSoundNameCellBlock4Offsets;
                        priorBounceStringOffsets =
                            found->bounceSoundNameStringBlock4Offsets;
                        priorBounceArrayOffset =
                            found->bounceSoundArrayBlock4Offset;
                        priorKnotOffsets = found->accuracyKnotBlock4Offsets;
                    }
                    try { result.worldWeapons.emplace_back(); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    worldWeaponIndex = result.worldWeapons.size() - 1u;
                    RetailPublishedWeaponDef &entry = result.worldWeapons.back();
                    entry.assetIndex = index;
                    entry.serializedReference = result.nextBodyReference;
                    entry.identity = identity;
                    entry.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    entry.pointerAlias = result.nextBodyReference != 0u;
                    entry.published = true;
                    if (priorAsset)
                    {
                        entry.asset = std::move(priorAsset);
                        entry.storage = std::move(priorStorage);
                        entry.stringBlock4Offsets = priorStringOffsets;
                        entry.soundNameCellBlock4Offsets = priorSoundCellOffsets;
                        entry.soundNameStringBlock4Offsets =
                            priorSoundStringOffsets;
                        entry.bounceSoundNameCellBlock4Offsets =
                            priorBounceCellOffsets;
                        entry.bounceSoundNameStringBlock4Offsets =
                            priorBounceStringOffsets;
                        entry.bounceSoundArrayBlock4Offset =
                            priorBounceArrayOffset;
                        entry.accuracyKnotBlock4Offsets = priorKnotOffsets;
                    }
                    nextStage = RetailCensusStage::WorldWeaponPublish;
                    return RetailCensusError::None;
                }
                if (result.nextBodyType == ASSET_TYPE_XANIM_PARTS &&
                    (result.nextBodyReference == INLINE_POINTER ||
                     result.nextBodyReference == SHARED_POINTER))
                {
                    return BeginWorldXAnimParts(index, nextStage);
                }
                if (result.nextBodyType == ASSET_TYPE_XANIM_PARTS)
                {
                    if (result.worldXAnimParts.size() >= limits.maxXAnimParts)
                        return RetailCensusError::XAnimCollectionLimit;
                    std::uint32_t identity = 0u;
                    std::shared_ptr<XAnimParts> priorAsset;
                    std::shared_ptr<CanonicalXAnimPartsStorage> priorStorage;
                    std::uint32_t priorNameOffset = 0u;
                    if (result.nextBodyReference != 0u)
                    {
                        if (ResolveRegistryAlias(
                                result.nextBodyReference,
                                ASSET_TYPE_XANIM_PARTS,
                                identity) != ZoneRegistryError::None)
                        {
                            return RetailCensusError::XAnimAliasInvalid;
                        }
                        const auto found = std::find_if(
                            result.worldXAnimParts.begin(),
                            result.worldXAnimParts.end(),
                            [identity](const RetailPublishedXAnimParts &entry) {
                                return entry.identity == identity && entry.asset;
                            });
                        if (found == result.worldXAnimParts.end())
                            return RetailCensusError::XAnimAliasInvalid;
                        priorAsset = found->asset;
                        priorStorage = found->storage;
                        priorNameOffset = found->nameBlock4Offset;
                    }
                    try
                    {
                        result.worldXAnimParts.emplace_back();
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    worldXAnimIndex = result.worldXAnimParts.size() - 1u;
                    RetailPublishedXAnimParts &entry =
                        result.worldXAnimParts.back();
                    entry.assetIndex = index;
                    entry.serializedReference = result.nextBodyReference;
                    entry.identity = identity;
                    entry.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    entry.pointerAlias = result.nextBodyReference != 0u;
                    entry.published = true;
                    if (priorAsset)
                    {
                        entry.asset = std::move(priorAsset);
                        entry.storage = std::move(priorStorage);
                        entry.nameBlock4Offset = priorNameOffset;
                    }
                    nextStage = RetailCensusStage::WorldXAnimPublish;
                    return RetailCensusError::None;
                }
                if (result.nextBodyReference == INLINE_POINTER &&
                    result.nextBodyType == ASSET_TYPE_TECHNIQUE_SET)
                {
                    worldBodyIndex = index;
                    return BeginWorldTechniqueSet(nextStage);
                }
                if (result.nextBodyReference == INLINE_POINTER &&
                    result.nextBodyType == ASSET_TYPE_XMODEL)
                {
                    return BeginWorldXModel(index, nextStage);
                }
                if (result.nextBodyReference == INLINE_POINTER &&
                    result.nextBodyType == ASSET_TYPE_FX)
                {
                    return BeginWorldFxEffect(index, nextStage);
                }
                if (result.nextBodyReference == INLINE_POINTER &&
                    result.nextBodyType == ASSET_TYPE_RAW_FILE)
                {
                    return BeginWorldRawFile(index, nextStage);
                }
                if (prerequisiteZone &&
                    result.nextBodyType == ASSET_TYPE_IMPACT_FX &&
                    (result.nextBodyReference == INLINE_POINTER ||
                     result.nextBodyReference == SHARED_POINTER))
                {
                    return BeginWorldFxImpactTable(index, nextStage);
                }
                if (prerequisiteZone &&
                    result.nextBodyType == ASSET_TYPE_MENU_LIST &&
                    (result.nextBodyReference == INLINE_POINTER ||
                     result.nextBodyReference == SHARED_POINTER))
                {
                    return BeginWorldMenuList(index, nextStage);
                }
                if (prerequisiteZone &&
                    result.nextBodyType == ASSET_TYPE_STRING_TABLE &&
                    result.nextBodyReference == INLINE_POINTER)
                {
                    return BeginWorldStringTable(index, nextStage);
                }
                const ZoneSpan current{
                    arenas.ActiveBlock(),
                    arenas.Cursor(arenas.ActiveBlock()),
                    0u,
                };
                const ZoneSpan tableAlias{
                    4u,
                    result.assetTableBlock4Offset + index * ASSET_BYTES + 4u,
                    4u,
                };
                if (const RetailCensusError error = AppendSemanticTrace(
                        kisak::database::SemanticTraceEventKind::Boundary,
                        result.nextBodyType,
                        index,
                        0u,
                        static_cast<std::uint32_t>(cursor),
                        current,
                        {},
                        tableAlias);
                    error != RetailCensusError::None)
                {
                    return error;
                }
                result.stoppedBeforeDifferentWorldAssetType = true;
                nextStage = RetailCensusStage::AssetBoundary;
                complete = true;
                return RetailCensusError::None;
            };
        auto finishWorldFxElem =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                elem.traversed = true;
                ++worldFxElemIndex;
                worldFxVisualIndex = 0u;
                worldFxElemPhase = WorldFxElemPhase::VelocitySamples;
                nextStage = worldFxElemIndex < activeWorldFx().elemDefs.size()
                    ? RetailCensusStage::WorldFxElemVelocitySamples
                    : RetailCensusStage::WorldFxPublish;
                return RetailCensusError::None;
            };
        auto publishWorldTechniqueSet =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                RetailWorldTechniqueSet &entry = result.worldTechniqueSets.back();
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (worldTechniqueSetNameResolved)
                {
                    if (const RetailCensusError error = MapRegistryError(
                            registry.RegisterAsset(
                                ASSET_TYPE_TECHNIQUE_SET,
                                entry.assetIndex,
                                entry.name,
                                entry.identity));
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                worldTopLevelAliasSlot,
                                entry.identity));
                        error != RetailCensusError::None) return error;
                    entry.published = true;
                }
                entry.boundaryInflatedOffset =
                    static_cast<std::uint32_t>(cursor);
                if (const RetailCensusError error = AppendSemanticTrace(
                        kisak::database::SemanticTraceEventKind::AssetPublish,
                        ASSET_TYPE_TECHNIQUE_SET,
                        entry.assetIndex,
                        entry.identity,
                        entry.boundaryInflatedOffset,
                        {0u, entry.block0Offset, TECHNIQUE_SET_BYTES},
                        entry.name,
                        worldTopLevelAliasSlot);
                    error != RetailCensusError::None)
                {
                    return error;
                }
                ++result.completedAssetCount;
                if (entry.assetIndex ==
                    result.worldPostXModelTechniqueSetAssetIndex)
                {
                    result.worldPostXModelTechniqueSetPublished = true;
                }
                if (!result.worldXModels.empty() &&
                    result.worldXModels.front().published &&
                    entry.assetIndex > result.worldXModels.front().assetIndex)
                {
                    ++result.worldPostXModelTechniqueSetCompletedCount;
                }
                if (entry.assetIndex == 0u)
                {
                    result.worldFirstTechniqueSetIdentity = entry.identity;
                    result.worldFirstTechniqueSetPublished = true;
                }
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount = registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                return dispatchSupportedWorldAsset(entry.assetIndex + 1u, nextStage);
            };
        auto scheduleWorldMaterialPass =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                while (worldMaterialPassIndex < worldMaterialPasses.size())
                {
                    const WorldMaterialPassState &pass =
                        worldMaterialPasses[worldMaterialPassIndex];
                    if (worldMaterialPassPhase ==
                        WorldMaterialPassPhase::VertexDeclaration)
                    {
                        worldMaterialPassPhase = WorldMaterialPassPhase::VertexShader;
                        if (pass.vertexDeclarationToken == INLINE_POINTER)
                        {
                            if (const RetailCensusError error = Plan(
                                    4u, VERTEX_DECLARATION_BYTES);
                                error != RetailCensusError::None) return error;
                            nextStage = RetailCensusStage::WorldMaterialVertexDeclaration;
                            return RetailCensusError::None;
                        }
                        if (!ValidPriorZonePointer(
                                pass.vertexDeclarationToken, 4u))
                            return RetailCensusError::VertexDeclarationUnsupported;
                        continue;
                    }
                    if (worldMaterialPassPhase ==
                        WorldMaterialPassPhase::VertexShader)
                    {
                        worldMaterialPassPhase = WorldMaterialPassPhase::PixelShader;
                        if (pass.vertexShaderToken == INLINE_POINTER)
                        {
                            if (const RetailCensusError error = Plan(
                                    4u, VERTEX_SHADER_BYTES);
                                error != RetailCensusError::None) return error;
                            nextStage = RetailCensusStage::WorldMaterialVertexShader;
                            return RetailCensusError::None;
                        }
                        if (!ValidPriorZonePointer(pass.vertexShaderToken, 4u))
                            return RetailCensusError::VertexShaderLayoutUnsupported;
                        continue;
                    }
                    if (worldMaterialPassPhase ==
                        WorldMaterialPassPhase::PixelShader)
                    {
                        worldMaterialPassPhase = WorldMaterialPassPhase::Arguments;
                        if (pass.pixelShaderToken == INLINE_POINTER)
                        {
                            if (const RetailCensusError error = Plan(
                                    4u, PIXEL_SHADER_BYTES);
                                error != RetailCensusError::None) return error;
                            nextStage = RetailCensusStage::WorldMaterialPixelShader;
                            return RetailCensusError::None;
                        }
                        if (!ValidPriorZonePointer(pass.pixelShaderToken, 4u))
                            return RetailCensusError::PixelShaderLayoutUnsupported;
                        continue;
                    }
                    if (worldMaterialPassPhase ==
                        WorldMaterialPassPhase::Arguments)
                    {
                        worldMaterialPassPhase = WorldMaterialPassPhase::Complete;
                        if (pass.argumentCount == 0u)
                        {
                            if (pass.argumentToken != 0u)
                                return RetailCensusError::ShaderArgumentLayoutUnsupported;
                            continue;
                        }
                        if (pass.argumentToken != INLINE_POINTER ||
                            pass.argumentCount > UINT32_MAX / MATERIAL_ARGUMENT_BYTES)
                            return RetailCensusError::ShaderArgumentLayoutUnsupported;
                        worldMaterialArgumentBytes =
                            pass.argumentCount * MATERIAL_ARGUMENT_BYTES;
                        if (const RetailCensusError error = Plan(
                                4u, worldMaterialArgumentBytes);
                            error != RetailCensusError::None) return error;
                        nextStage = RetailCensusStage::WorldMaterialShaderArguments;
                        return RetailCensusError::None;
                    }
                    ++worldMaterialPassIndex;
                    worldMaterialPassPhase =
                        WorldMaterialPassPhase::VertexDeclaration;
                }
                nextStage = RetailCensusStage::WorldMaterialTechniqueName;
                return RetailCensusError::None;
            };
        auto scheduleWorldMaterialLiteral =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                while (worldMaterialLiteralIndex <
                    worldMaterialLiteralTokens.size())
                {
                    const std::uint32_t token =
                        worldMaterialLiteralTokens[worldMaterialLiteralIndex++];
                    if (token == 0u) continue;
                    if (token == INLINE_POINTER)
                    {
                        if (const RetailCensusError error = Plan(
                                4u, MATERIAL_LITERAL_CONSTANT_BYTES);
                            error != RetailCensusError::None) return error;
                        nextStage = RetailCensusStage::WorldMaterialLiteralConstant;
                        return RetailCensusError::None;
                    }
                    if (token == SHARED_POINTER ||
                        !ValidPriorZonePointer(token, 4u))
                        return RetailCensusError::ShaderArgumentLayoutUnsupported;
                }
                return scheduleWorldMaterialPass(nextStage);
            };
        auto scheduleWorldTechniqueReference =
            [&](RetailCensusStage &nextStage) noexcept -> RetailCensusError {
                while (worldTechniqueSlotIndex < techniqueTokens.size())
                {
                    const std::uint32_t slot = worldTechniqueSlotIndex;
                    const std::uint32_t token = techniqueTokens[slot];
                    if (token == 0u)
                    {
                        ++worldTechniqueSlotIndex;
                        continue;
                    }
                    if (token == INLINE_POINTER)
                        return BeginWorldMaterialTechnique(slot, nextStage);
                    if (token == SHARED_POINTER ||
                        !ValidPriorZonePointer(token, 4u))
                        return RetailCensusError::TechniqueAliasInvalid;
                    ++worldTechniqueSlotIndex;
                }
                return publishWorldTechniqueSet(nextStage);
            };
        auto addSurfacePayload = [&](std::uint64_t bytes) noexcept {
            RetailWorldXModel &model = activeWorldXModel();
            const std::uint64_t total =
                static_cast<std::uint64_t>(model.surfacePayloadBytes) + bytes;
            if (total > limits.maxXModelSurfacePayloadBytes ||
                total > std::numeric_limits<std::uint32_t>::max())
            {
                return RetailCensusError::XSurfacePayloadLimit;
            }
            model.surfacePayloadBytes = static_cast<std::uint32_t>(total);
            return RetailCensusError::None;
        };
        auto retainResolvedWorldImage = [&](const RetailXModelImage &image) noexcept {
            if (worldMaterialOwnedByFx || worldMaterialOwnedByMenu ||
                worldMaterialTopLevel)
                return RetailCensusError::None;
            RetailWorldXModel &model = activeWorldXModel();
            const auto existing = std::find_if(
                model.resolvedImages.begin(), model.resolvedImages.end(),
                [&](const RetailXModelImage &entry) {
                    return entry.identity == image.identity;
                });
            if (existing != model.resolvedImages.end())
                return RetailCensusError::None;
            try
            {
                model.resolvedImages.push_back(image);
            }
            catch (...) { return RetailCensusError::AllocationFailed; }
            return RetailCensusError::None;
        };
        auto findPublishedWorldImage = [&](std::uint32_t identity) noexcept
            -> const RetailXModelImage * {
            const auto canonical = std::find_if(
                result.worldImages.begin(), result.worldImages.end(),
                [&](const RetailPublishedGfxImage &entry) {
                    return entry.identity == identity && entry.published;
                });
            if (canonical != result.worldImages.end()) return &*canonical;
            for (const RetailXModelMaterial &material : result.worldMaterials)
            {
                const auto image = std::find_if(
                    material.images.begin(), material.images.end(),
                    [&](const RetailXModelImage &entry) {
                        return entry.identity == identity && entry.published;
                    });
                if (image != material.images.end()) return &*image;
            }
            for (const RetailWorldXModel &model : result.worldXModels)
            {
                for (const RetailXModelMaterial &material : model.materials)
                {
                    const auto image = std::find_if(
                        material.images.begin(), material.images.end(),
                        [&](const RetailXModelImage &entry) {
                            return entry.identity == identity && entry.published;
                        });
                    if (image != material.images.end()) return &*image;
                }
            }
            for (const RetailWorldFxEffectDef &effect : result.worldFxEffects)
            {
                for (const RetailWorldFxMaterial &material : effect.materials)
                {
                    const auto image = std::find_if(
                        material.images.begin(), material.images.end(),
                        [&](const RetailXModelImage &entry) {
                            return entry.identity == identity && entry.published;
                        });
                    if (image != material.images.end()) return &*image;
                }
            }
            return nullptr;
        };
        auto retainResolvedWorldMaterial =
            [&](const RetailXModelMaterial &material) noexcept {
                if (worldMaterialOwnedByFx || worldMaterialOwnedByMenu ||
                    worldMaterialTopLevel)
                    return RetailCensusError::None;
                RetailWorldXModel &model = activeWorldXModel();
                const auto existing = std::find_if(
                    model.resolvedMaterials.begin(),
                    model.resolvedMaterials.end(),
                    [&](const RetailXModelMaterial &entry) {
                        return entry.identity == material.identity;
                    });
                if (existing != model.resolvedMaterials.end())
                    return RetailCensusError::None;
                try
                {
                    model.resolvedMaterials.push_back(material);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                return RetailCensusError::None;
            };
        auto findPublishedWorldMaterial = [&](std::uint32_t identity) noexcept
            -> const RetailXModelMaterial * {
            const auto topLevel = std::find_if(
                result.worldMaterials.begin(), result.worldMaterials.end(),
                [&](const RetailXModelMaterial &entry) {
                    return entry.identity == identity && entry.published;
                });
            if (topLevel != result.worldMaterials.end()) return &*topLevel;
            for (const RetailWorldXModel &model : result.worldXModels)
            {
                const auto material = std::find_if(
                    model.materials.begin(), model.materials.end(),
                    [&](const RetailXModelMaterial &entry) {
                        return entry.identity == identity && entry.published;
                    });
                if (material != model.materials.end()) return &*material;
            }
            for (const RetailWorldFxEffectDef &effect : result.worldFxEffects)
            {
                const auto material = std::find_if(
                    effect.materials.begin(), effect.materials.end(),
                    [&](const RetailWorldFxMaterial &entry) {
                        return entry.identity == identity && entry.published;
                    });
                if (material != effect.materials.end()) return &*material;
            }
            return nullptr;
        };
        auto advanceWorldMaterials = [&](RetailCensusStage &nextStage) noexcept {
            RetailWorldXModel &model = activeWorldXModel();
            while (worldMaterialIndex < model.materialReferences.size())
            {
                const std::uint32_t token =
                    model.materialReferences[worldMaterialIndex];
                if (token == INLINE_POINTER || token == SHARED_POINTER)
                {
                    worldMaterialOwnedByFx = false;
                    worldMaterialAliasSlot = {
                        4u,
                        model.materialHandlesBlock4Offset +
                            worldMaterialIndex * 4u,
                        4u,
                    };
                    if (const RetailCensusError error = MapRegistryError(
                            registry.ReserveAlias(
                                worldMaterialAliasSlot, ASSET_TYPE_MATERIAL));
                        error != RetailCensusError::None)
                    {
                            return error;
                    }
                    worldMaterialHasInsertAlias = token == SHARED_POINTER;
                    if (worldMaterialHasInsertAlias)
                    {
                        if (const RetailCensusError error = Plan(
                                4u, 4u, &worldMaterialInsertAliasSlot);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = MapRegistryError(
                                registry.ReserveAlias(
                                    worldMaterialInsertAliasSlot,
                                    ASSET_TYPE_MATERIAL));
                            error != RetailCensusError::None) return error;
                    }
                    if (const RetailCensusError error = Push(0u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, MATERIAL_BYTES, &span);
                        error != RetailCensusError::None) return error;
                    try
                    {
                        model.materials.emplace_back();
                        model.materials.back().asset =
                            std::make_shared<Material>();
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    RetailXModelMaterial &material = model.materials.back();
                    *material.asset = {};
                    material.handleIndex = worldMaterialIndex;
                    material.headerBlock0Offset = span.offset;
                    worldTextureIndex = 0u;
                    nextStage = RetailCensusStage::WorldXModelMaterial;
                    return RetailCensusError::None;
                }
                if (token == 0u)
                {
                    model.materialIdentities[worldMaterialIndex] = 0u;
                    ++worldMaterialIndex;
                    continue;
                }
                std::uint32_t identity = 0u;
                ZoneRegistryError resolveError = ResolveRegistryAlias(
                    token, ASSET_TYPE_MATERIAL, identity);
                if (resolveError != ZoneRegistryError::None)
                {
                    ZoneSpan target;
                    const auto publishedMaterial = std::find_if(
                        model.materials.begin(), model.materials.end(),
                        [&](const RetailXModelMaterial &material) {
                            return material.published &&
                                material.textureCount != 0u &&
                                DecodeZoneAliasToken(token, target) &&
                                target.block == 4u &&
                                target.offset == material.textureTableBlock4Offset;
                        });
                    if (publishedMaterial == model.materials.end())
                    {
                        if (prerequisiteZone &&
                            ValidPriorZonePointer(token, 4u))
                        {
                            model.materialIdentities[worldMaterialIndex] = 0u;
                            ++worldMaterialIndex;
                            continue;
                        }
                        return RetailCensusError::XModelMaterialAliasInvalid;
                    }
                    if (const RetailCensusError error = MapRegistryError(
                            registry.ReserveAlias(target, ASSET_TYPE_MATERIAL));
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                target, publishedMaterial->identity));
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    resolveError = ResolveRegistryAlias(
                        token, ASSET_TYPE_MATERIAL, identity);
                    if (resolveError != ZoneRegistryError::None)
                        return RetailCensusError::XModelMaterialAliasInvalid;
                }
                const RetailXModelMaterial *resolvedMaterial =
                    findPublishedWorldMaterial(identity);
                if (resolvedMaterial == nullptr)
                {
                    if (prerequisiteZone)
                    {
                        model.materialIdentities[worldMaterialIndex] = 0u;
                        ++worldMaterialIndex;
                        continue;
                    }
                    return RetailCensusError::XModelMaterialAliasInvalid;
                }
                if (prerequisiteZone)
                {
                    model.materialIdentities[worldMaterialIndex] = identity;
                    ++worldMaterialIndex;
                    continue;
                }
                if (const RetailCensusError error =
                        retainResolvedWorldMaterial(*resolvedMaterial);
                    error != RetailCensusError::None) return error;
                for (const RetailXModelMaterialTexture &texture :
                     resolvedMaterial->textures)
                {
                    if (!texture.resolved || texture.imageIdentity == 0u)
                        return RetailCensusError::XModelMaterialAliasInvalid;
                    const RetailXModelImage *resolvedImage =
                        findPublishedWorldImage(texture.imageIdentity);
                    if (resolvedImage == nullptr)
                        return RetailCensusError::XModelMaterialAliasInvalid;
                    if (const RetailCensusError error =
                            retainResolvedWorldImage(*resolvedImage);
                        error != RetailCensusError::None) return error;
                }
                model.materialIdentities[worldMaterialIndex] = identity;
                ++worldMaterialIndex;
            }
            model.materialsTraversed = true;
            nextStage = RetailCensusStage::WorldXModelCollisionSurfaces;
            return RetailCensusError::None;
        };
        auto advanceWorldTextures = [&](RetailCensusStage &nextStage) noexcept {
            RetailXModelMaterial &material = activeWorldMaterial();
            while (worldTextureIndex < material.textures.size())
            {
                RetailXModelMaterialTexture &texture =
                    material.textures[worldTextureIndex];
                const std::uint32_t token = texture.imageReference;
                if (token == INLINE_POINTER || token == SHARED_POINTER)
                {
                    worldImageAliasSlot = {
                        4u,
                        material.textureTableBlock4Offset +
                            worldTextureIndex * MATERIAL_TEXTURE_BYTES + 8u,
                        4u,
                    };
                    if (const RetailCensusError error = MapRegistryError(
                            registry.ReserveAlias(
                                worldImageAliasSlot, ASSET_TYPE_IMAGE));
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    worldImageHasInsertAlias = token == SHARED_POINTER;
                    worldImageNameResolved = true;
                    worldImageInsertAliasSlot = {};
                    if (worldImageHasInsertAlias)
                    {
                        if (const RetailCensusError error = Plan(
                                4u, 4u, &worldImageInsertAliasSlot);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = MapRegistryError(
                                registry.ReserveAlias(
                                    worldImageInsertAliasSlot,
                                    ASSET_TYPE_IMAGE));
                            error != RetailCensusError::None) return error;
                    }
                    if (const RetailCensusError error = Push(0u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, GFX_IMAGE_BYTES, &span);
                        error != RetailCensusError::None) return error;
                    try
                    {
                        material.images.emplace_back();
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    RetailXModelImage &image = material.images.back();
                    image.ownerAssetIndex = result.nextBodyIndex;
                    image.serializedReference = token;
                    image.textureIndex = worldTextureIndex;
                    image.headerBlock0Offset = span.offset;
                    try { image.asset = std::make_shared<GfxImage>(); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    std::memset(image.asset.get(), 0, sizeof(GfxImage));
                    if (worldImageHasInsertAlias)
                    {
                        image.assetInsertPointerBlock4Offset =
                            worldImageInsertAliasSlot.offset;
                    }
                    nextStage = RetailCensusStage::WorldXModelImage;
                    return RetailCensusError::None;
                }
                if (token == 0u)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::MaterialTextureLayoutUnsupported;
                    ++worldTextureIndex;
                    continue;
                }
                std::uint32_t identity = 0u;
                if (ResolveRegistryAlias(token, ASSET_TYPE_IMAGE, identity) !=
                    ZoneRegistryError::None)
                {
                    if (prerequisiteZone &&
                        ValidPriorZonePointer(token, 4u))
                    {
                        ++worldTextureIndex;
                        continue;
                    }
                    return RetailCensusError::XModelImageAliasInvalid;
                }
                const RetailXModelImage *resolvedImage =
                    findPublishedWorldImage(identity);
                if (resolvedImage == nullptr)
                {
                    if (prerequisiteZone)
                    {
                        ++worldTextureIndex;
                        continue;
                    }
                    return RetailCensusError::XModelImageAliasInvalid;
                }
                if (const RetailCensusError error =
                        retainResolvedWorldImage(*resolvedImage);
                    error != RetailCensusError::None) return error;
                texture.imageIdentity = identity;
                texture.resolved = true;
                ++worldTextureIndex;
            }
            nextStage = RetailCensusStage::WorldXModelMaterialConstants;
            return RetailCensusError::None;
        };
        while (report.recordsProcessed < budget.maxRecords &&
            report.traversedBytes < budget.maxBytes)
        {
            if (stage == RetailCensusStage::XFile)
            {
                const int visit = visitRecord(XFILE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                result.xfileSize = ReadU32(record);
                result.externalSize = ReadU32(record + 4u);
                std::uint64_t total = 0u;
                for (std::size_t index = 0u; index < result.blockSizes.size(); ++index)
                {
                    const std::uint32_t size = ReadU32(record + 8u + index * 4u);
                    if (size > limits.maxBlockBytes) return RetailCensusError::BlockSizeLimit;
                    result.blockSizes[index] = size;
                    total += size;
                    if (total > limits.maxTotalBlockBytes) return RetailCensusError::TotalBlockSizeLimit;
                }
                result.declaredBlockBytes = total;
                auto traversalBlockSizes = result.blockSizes;
                if (prerequisiteZone &&
                    traversalBlockSizes[4u] <= UINT32_MAX - 64u * 1024u)
                {
                    // The prerequisite task traversal still owns temporary UI
                    // scaffolding whose allocation accounting is deliberately
                    // isolated from the canonical DB objects. Bound that known
                    // discrepancy without changing the serialized zone limits
                    // used by pointer validation or the published result.
                    traversalBlockSizes[4u] += 64u * 1024u;
                }
                if (const RetailCensusError error = MapZoneError(arenas.Initialize(
                        traversalBlockSizes,
                        {limits.maxTotalBlockBytes, 64u, 4096u, limits.maxTotalBlockBytes}));
                    error != RetailCensusError::None)
                    return error;
                if (const RetailCensusError error = MapRegistryError(registry.Initialize(
                        result.blockSizes,
                        {limits.maxRegistryAssets, limits.maxRegistryAliases,
                         limits.maxRegistryNameBytes}));
                    error != RetailCensusError::None)
                    return error;
                arenasInitialized = true;
                cursor += XFILE_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::AssetList;
                continue;
            }
            if (stage == RetailCensusStage::AssetList)
            {
                const int visit = visitRecord(ASSET_LIST_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::int32_t scriptCount = ReadS32(record);
                const std::uint32_t scriptToken = ReadU32(record + 4u);
                const std::int32_t assetCount = ReadS32(record + 8u);
                const std::uint32_t assetToken = ReadU32(record + 12u);
                if (scriptCount < 0) return RetailCensusError::ScriptStringCountInvalid;
                if (static_cast<std::uint32_t>(scriptCount) > limits.maxScriptStrings)
                    return RetailCensusError::ScriptStringCountLimit;
                if ((scriptCount == 0 && scriptToken != 0u) ||
                    (scriptCount != 0 && scriptToken != INLINE_POINTER))
                    return RetailCensusError::ScriptStringArrayInvalid;
                if (assetCount <= 0) return RetailCensusError::AssetCountInvalid;
                if (static_cast<std::uint32_t>(assetCount) > limits.maxAssets)
                    return RetailCensusError::AssetCountLimit;
                if (assetToken != INLINE_POINTER) return RetailCensusError::AssetArrayInvalid;
                result.scriptStringCount = static_cast<std::uint32_t>(scriptCount);
                result.assetCount = static_cast<std::uint32_t>(assetCount);
                if (mode == RetailCensusMode::WorldTechniqueSetPrefix ||
                    mode == RetailCensusMode::WorldXModelPrefix ||
                    mode == RetailCensusMode::WorldXSurfacePrefix ||
                    mode == RetailCensusMode::WorldXModelDependencies ||
                    mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                    mode == RetailCensusMode::WorldSecondXModelPrefix ||
                    mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                    mode == RetailCensusMode::WorldSecondXModelDependencies ||
                    mode == RetailCensusMode::WorldXModelLoader)
                {
                    try
                    {
                        worldAssetTypes.reserve(result.assetCount);
                        worldAssetReferences.reserve(result.assetCount);
                        result.worldTechniqueSets.reserve(result.assetCount);
                        result.worldXModels.reserve(std::min(
                            result.assetCount, limits.maxWorldXModels));
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                }
                cursor += ASSET_LIST_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::ScriptStringPointers;
                continue;
            }
            if (stage == RetailCensusStage::ScriptStringPointers)
            {
                const std::uint64_t bytes64 = static_cast<std::uint64_t>(result.scriptStringCount) * 4u;
                if (bytes64 > std::numeric_limits<std::size_t>::max())
                    return RetailCensusError::ScriptStringCountLimit;
                const std::size_t bytes = static_cast<std::size_t>(bytes64);
                if (!scriptScopesOpen)
                {
                    if (!arenasInitialized) return RetailCensusError::ZoneStreamInvalid;
                    if (const RetailCensusError error = Push(4u);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Push(4u);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Plan(4u, bytes64);
                        error != RetailCensusError::None) return error;
                    scriptScopesOpen = true;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    scriptTokens.resize(result.scriptStringCount);
                    scriptStrings.resize(result.scriptStringCount);
                    scriptStringBlock4Offsets.assign(
                        result.scriptStringCount, UINT32_MAX);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                for (std::uint32_t index = 0u; index < result.scriptStringCount; ++index)
                    scriptTokens[index] = ReadU32(inflated.data() + cursor + index * 4u);
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::ScriptStrings;
                continue;
            }
            if (stage == RetailCensusStage::ScriptStrings)
            {
                if (scriptIndex == result.scriptStringCount)
                {
                    if (!scriptScopesOpen) return RetailCensusError::ZoneStreamInvalid;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    scriptScopesOpen = false;
                    assetTableOffset = cursor;
                    const std::uint64_t tableBytes =
                        static_cast<std::uint64_t>(result.assetCount) * ASSET_BYTES;
                    if (tableBytes > limits.maxInflatedPrefixBytes ||
                        cursor > limits.maxInflatedPrefixBytes - static_cast<std::size_t>(tableBytes))
                        return RetailCensusError::InflatedPrefixLimit;
                    if (const RetailCensusError error = Push(4u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan tableSpan;
                    if (const RetailCensusError error = Plan(4u, tableBytes, &tableSpan);
                        error != RetailCensusError::None) return error;
                    result.assetTableBlock4Offset = tableSpan.offset;
                    assetScopeOpen = true;
                    stage = RetailCensusStage::AssetTable;
                    continue;
                }
                const std::uint32_t token = scriptTokens[scriptIndex];
                if (token == 0u)
                {
                    ++scriptIndex;
                    ++report.recordsProcessed;
                    continue;
                }
                if (token != INLINE_POINTER)
                    return RetailCensusError::ScriptStringReferenceUnsupported;
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    const std::size_t available = inflated.size() - cursor;
                    if (available >= limits.maxScriptStringBytes)
                        return RetailCensusError::ScriptStringTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxScriptStringBytes)
                    return RetailCensusError::ScriptStringTooLong;
                if (bytes > limits.maxTotalScriptStringBytes ||
                    result.scriptStringBytes > limits.maxTotalScriptStringBytes - bytes)
                    return RetailCensusError::ScriptStringBytesLimit;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    scriptStringBlock4Offsets[scriptIndex] = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                result.scriptStringBytes += static_cast<std::uint32_t>(bytes);
                try
                {
                    scriptStrings[scriptIndex].assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                    block4StringAliases.emplace(
                        scriptStringBlock4Offsets[scriptIndex],
                        std::make_shared<std::string>(
                            scriptStrings[scriptIndex]));
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++scriptIndex;
                ++report.recordsProcessed;
                continue;
            }
            if (stage == RetailCensusStage::AssetTable)
            {
                const std::size_t tableBytes = static_cast<std::size_t>(result.assetCount) * ASSET_BYTES;
                if (assetIndex == 0u && inflated.size() - assetTableOffset < tableBytes)
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (assetIndex == result.assetCount)
                {
                    result.firstBodyIndex = 0u;
                    result.inflatedPrefixBytes = static_cast<std::uint32_t>(
                        assetTableOffset + tableBytes);
                    if (mode == RetailCensusMode::WorldAssetInventory)
                    {
                        if (result.firstGfxWorldAssetIndex == UINT32_MAX)
                            return RetailCensusError::GfxWorldMissing;
                        if (!assetScopeOpen) return RetailCensusError::ZoneStreamInvalid;
                        if (const RetailCensusError error = Pop();
                            error != RetailCensusError::None) return error;
                        assetScopeOpen = false;
                        result.stoppedBeforeAssetBody = true;
                        result.completedAssetCount = 0u;
                        result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                        result.block4CursorAtBoundary = arenas.Cursor(4u);
                        stage = RetailCensusStage::AssetBoundary;
                        complete = true;
                        return RetailCensusError::None;
                    }
                    if (mode == RetailCensusMode::WorldTechniqueSetPrefix ||
                        mode == RetailCensusMode::WorldXModelPrefix ||
                        mode == RetailCensusMode::WorldXSurfacePrefix ||
                        mode == RetailCensusMode::WorldXModelDependencies ||
                        mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                        mode == RetailCensusMode::WorldSecondXModelPrefix ||
                        mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                        mode == RetailCensusMode::WorldSecondXModelDependencies ||
                        mode == RetailCensusMode::WorldXModelLoader)
                    {
                        if (!assetScopeOpen || worldAssetTypes.empty())
                            return RetailCensusError::FirstAssetUnsupported;
                        if (mode == RetailCensusMode::WorldXModelLoader)
                        {
                            if (const RetailCensusError error =
                                    dispatchSupportedWorldAsset(0u, stage);
                                error != RetailCensusError::None)
                            {
                                return error;
                            }
                            if (complete) return RetailCensusError::None;
                            continue;
                        }
                        if (result.firstGfxWorldAssetIndex == UINT32_MAX)
                            return RetailCensusError::GfxWorldMissing;
                        if (worldAssetTypes.front() != ASSET_TYPE_TECHNIQUE_SET ||
                            worldAssetReferences.front() != INLINE_POINTER)
                        {
                            return RetailCensusError::FirstAssetUnsupported;
                        }
                        worldBodyIndex = 0u;
                        if (const RetailCensusError error = BeginWorldTechniqueSet(stage);
                            error != RetailCensusError::None) return error;
                        continue;
                    }
                    if (!assetScopeOpen || result.firstBodyType != ASSET_TYPE_TECHNIQUE_SET ||
                        result.firstBodyReference != INLINE_POINTER)
                        return RetailCensusError::FirstAssetUnsupported;
                    if (result.assetCount < prefixTypes.size() ||
                        prefixTypes != std::array<std::uint32_t, 3>{
                            ASSET_TYPE_TECHNIQUE_SET,
                            ASSET_TYPE_TECHNIQUE_SET,
                            ASSET_TYPE_MATERIAL} ||
                        std::any_of(prefixReferences.begin(), prefixReferences.end(),
                            [](std::uint32_t token) { return token != INLINE_POINTER; }))
                        return RetailCensusError::AssetPrefixUnsupported;
                    if (const RetailCensusError error = Push(0u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, TECHNIQUE_SET_BYTES, &span);
                        error != RetailCensusError::None) return error;
                    result.techniqueSetBlock0Offset = span.offset;
                    stage = RetailCensusStage::TechniqueSet;
                    continue;
                }
                const std::uint8_t *record = inflated.data() +
                    assetTableOffset + static_cast<std::size_t>(assetIndex) * ASSET_BYTES;
                const int visit = visitRecord(ASSET_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::int32_t signedType = ReadS32(record);
                if (signedType < 0 || static_cast<std::uint32_t>(signedType) >= RETAIL_CENSUS_ASSET_TYPE_COUNT)
                    return RetailCensusError::AssetTypeInvalid;
                const std::uint32_t type = static_cast<std::uint32_t>(signedType);
                const std::uint32_t reference = ReadU32(record + 4u);
                if (result.typeCounts[type] == 0u)
                    result.firstTypeIndices[type] = assetIndex;
                result.assetTableOrderHash = Fnv1a32(
                    std::span<const std::uint8_t>(record, ASSET_BYTES),
                    result.assetTableOrderHash);
                if (type == ASSET_TYPE_GFX_WORLD &&
                    result.firstGfxWorldAssetIndex == UINT32_MAX)
                {
                    result.firstGfxWorldAssetIndex = assetIndex;
                    result.firstGfxWorldReference = reference;
                    result.typesBeforeFirstGfxWorld = result.typeCounts;
                    result.inlineReferencesBeforeFirstGfxWorld =
                        result.inlineAssetReferences;
                    result.sharedReferencesBeforeFirstGfxWorld =
                        result.sharedAssetReferences;
                    result.aliasReferencesBeforeFirstGfxWorld =
                        result.aliasAssetReferences;
                    result.nullReferencesBeforeFirstGfxWorld =
                        result.nullAssetReferences;
                }
                if (assetIndex == 0u)
                {
                    result.firstBodyType = type;
                    result.firstBodyReference = reference;
                }
                else if (assetIndex == 1u)
                {
                    result.nextBodyType = type;
                    result.nextBodyReference = reference;
                }
                if (mode == RetailCensusMode::CodePostGfxMaterial &&
                    assetIndex < prefixTypes.size())
                {
                    prefixTypes[assetIndex] = type;
                    prefixReferences[assetIndex] = reference;
                    topLevelAliasSlots[assetIndex] = {
                        4u,
                        result.assetTableBlock4Offset + assetIndex * ASSET_BYTES + 4u,
                        4u,
                    };
                    if (const RetailCensusError error = MapRegistryError(
                            registry.ReserveAlias(topLevelAliasSlots[assetIndex], type));
                        error != RetailCensusError::None)
                        return error;
                }
                else if (mode == RetailCensusMode::WorldTechniqueSetPrefix ||
                    mode == RetailCensusMode::WorldXModelPrefix ||
                    mode == RetailCensusMode::WorldXSurfacePrefix ||
                    mode == RetailCensusMode::WorldXModelDependencies ||
                    mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                    mode == RetailCensusMode::WorldSecondXModelPrefix ||
                    mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                    mode == RetailCensusMode::WorldSecondXModelDependencies ||
                    mode == RetailCensusMode::WorldXModelLoader)
                {
                    try
                    {
                        worldAssetTypes.push_back(type);
                        worldAssetReferences.push_back(reference);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                }
                ++result.typeCounts[type];
                if (reference == 0u) ++result.nullAssetReferences;
                else if (reference == INLINE_POINTER) ++result.inlineAssetReferences;
                else if (reference == SHARED_POINTER) ++result.sharedAssetReferences;
                else ++result.aliasAssetReferences;
                ++assetIndex;
                cursor += ASSET_BYTES;
                ++report.recordsProcessed;
                continue;
            }
            if (stage == RetailCensusStage::WorldTechniqueSet)
            {
                const int visit = visitRecord(TECHNIQUE_SET_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailWorldTechniqueSet &entry = result.worldTechniqueSets.back();
                entry.nameReference = ReadU32(record);
                if (entry.nameReference == 0u ||
                    entry.nameReference == SHARED_POINTER ||
                    record[5u] != 0u || record[6u] != 0u || record[7u] != 0u)
                    return RetailCensusError::TechniqueSetLayoutUnsupported;
                entry.worldVertFormat = record[4u];
                entry.remapReference = ReadU32(record + 8u);
                for (std::uint32_t index = 0u; index < techniqueTokens.size(); ++index)
                    techniqueTokens[index] = ReadU32(record + 12u + index * 4u);
                if (worldBodyIndex == 0u)
                {
                    result.worldFirstTechniqueSetWorldVertFormat = entry.worldVertFormat;
                    result.worldFirstTechniqueSetRemapReference = entry.remapReference;
                    result.worldFirstTechniqueSetHeaderTraversed = true;
                }
                cursor += TECHNIQUE_SET_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldTechniqueSetName;
                continue;
            }
            if (stage == RetailCensusStage::WorldTechniqueSetName)
            {
                RetailWorldTechniqueSet &entry = result.worldTechniqueSets.back();
                if (entry.nameReference != INLINE_POINTER)
                {
                    std::shared_ptr<std::string> name;
                    std::uint32_t offset = UINT32_MAX;
                    if (!ResolvePriorZoneStringPayload(
                            entry.nameReference, name, offset) || !name ||
                        !ValidPublishedName(*name))
                    {
                        if (!prerequisiteZone || !ValidPriorZonePointer(
                                entry.nameReference))
                        {
                            return RetailCensusError::TechniqueSetNameInvalid;
                        }
                        worldTechniqueSetNameResolved = false;
                        ZoneSpan target;
                        if (DecodeZoneAliasToken(entry.nameReference, target))
                            entry.nameBlock4Offset = target.offset;
                    }
                    else
                    {
                        entry.name = *name;
                        entry.nameBlock4Offset = offset;
                    }
                    if (worldBodyIndex == 0u)
                        result.worldFirstTechniqueSetNameBlock4Offset = offset;
                }
                else
                {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxTechniqueNameBytes)
                        return RetailCensusError::TechniqueSetNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::TechniqueSetNameInvalid;
                if (bytes > limits.maxTechniqueNameBytes)
                    return RetailCensusError::TechniqueSetNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    entry.nameBlock4Offset = span.offset;
                    if (worldBodyIndex == 0u)
                        result.worldFirstTechniqueSetNameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    entry.name.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(entry.name))
                    return RetailCensusError::TechniqueSetNameInvalid;
                cursor += bytes;
                ++report.recordsProcessed;
                }
                for (std::uint32_t index = 0u; index < techniqueTokens.size(); ++index)
                {
                    const std::uint32_t token = techniqueTokens[index];
                    if (token == 0u)
                    {
                        ++entry.nullTechniqueReferences;
                        continue;
                    }
                    if (entry.firstTechniqueSlot == UINT32_MAX)
                    {
                        entry.firstTechniqueSlot = index;
                        entry.firstTechniqueReference = token;
                    }
                    if (token == INLINE_POINTER)
                        ++entry.inlineTechniqueReferences;
                    else if (token == SHARED_POINTER)
                        ++entry.sharedTechniqueReferences;
                    else
                        ++entry.aliasTechniqueReferences;
                }
                entry.boundaryInflatedOffset = static_cast<std::uint32_t>(cursor);
                if (worldBodyIndex == 0u)
                {
                    try { result.worldFirstTechniqueSetName = entry.name; }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    result.worldFirstTechniqueSlot = entry.firstTechniqueSlot;
                    result.worldFirstTechniqueReference = entry.firstTechniqueReference;
                    result.worldTechniqueNullReferences = entry.nullTechniqueReferences;
                    result.worldTechniqueInlineReferences = entry.inlineTechniqueReferences;
                    result.worldTechniqueSharedReferences = entry.sharedTechniqueReferences;
                    result.worldTechniqueAliasReferences = entry.aliasTechniqueReferences;
                    result.worldFirstTechniqueSetBoundaryInflatedOffset =
                        entry.boundaryInflatedOffset;
                }
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.stoppedBeforeAssetBody = false;
                if (entry.firstTechniqueSlot == UINT32_MAX)
                {
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (worldTechniqueSetNameResolved)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.RegisterAsset(
                                    ASSET_TYPE_TECHNIQUE_SET,
                                    entry.assetIndex,
                                    entry.name,
                                    entry.identity));
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldTopLevelAliasSlot,
                                    entry.identity));
                            error != RetailCensusError::None) return error;
                        entry.published = true;
                    }
                    if (const RetailCensusError error = AppendSemanticTrace(
                            kisak::database::SemanticTraceEventKind::AssetPublish,
                            ASSET_TYPE_TECHNIQUE_SET,
                            entry.assetIndex,
                            entry.identity,
                            entry.boundaryInflatedOffset,
                            {0u, entry.block0Offset, TECHNIQUE_SET_BYTES},
                            entry.name,
                            worldTopLevelAliasSlot);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    ++result.completedAssetCount;
                    if (entry.assetIndex ==
                        result.worldPostXModelTechniqueSetAssetIndex)
                    {
                        result.worldPostXModelTechniqueSetPublished = true;
                    }
                    if (!result.worldXModels.empty() &&
                        result.worldXModels.front().published &&
                        entry.assetIndex > result.worldXModels.front().assetIndex)
                    {
                        ++result.worldPostXModelTechniqueSetCompletedCount;
                    }
                    if (worldBodyIndex == 0u)
                    {
                        result.worldFirstTechniqueSetIdentity = entry.identity;
                        result.worldFirstTechniqueSetPublished = true;
                    }
                    result.stoppedBeforeWorldTechniqueDependency = false;

                    const std::uint32_t nextIndex = worldBodyIndex + 1u;
                    result.worldNextAssetIndex = nextIndex;
                    result.nextBodyIndex = nextIndex;
                    if (nextIndex >= worldAssetTypes.size())
                    {
                        result.unsupportedOperation =
                            mode == RetailCensusMode::WorldXModelLoader
                                ? nullptr
                                : "Load_XAssetHeader(end of table)";
                    }
                    else
                    {
                        result.nextBodyType = worldAssetTypes[nextIndex];
                        result.nextBodyReference = worldAssetReferences[nextIndex];
                        if (!result.worldXModels.empty() &&
                            result.worldXModels.front().published &&
                            entry.assetIndex > result.worldXModels.front().assetIndex)
                        {
                            result.worldRegistryAliasCount = registry.AliasCount();
                            result.worldRegistryDefinedAliasCount =
                                registry.DefinedAliasCount();
                            result.registryAssetCount = registry.AssetCount();
                            result.registryAliasCount = registry.AliasCount();
                            result.registryDefinedAliasCount =
                                registry.DefinedAliasCount();
                            if (result.nextBodyType == ASSET_TYPE_TECHNIQUE_SET &&
                                result.nextBodyReference == INLINE_POINTER)
                            {
                                worldBodyIndex = nextIndex;
                                if (const RetailCensusError error =
                                        BeginWorldTechniqueSet(stage);
                                    error != RetailCensusError::None)
                                {
                                    return error;
                                }
                                continue;
                            }
                            if ((mode == RetailCensusMode::WorldSecondXModelPrefix ||
                                 mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                                 mode == RetailCensusMode::WorldSecondXModelDependencies ||
                                 mode == RetailCensusMode::WorldXModelLoader) &&
                                result.nextBodyType == ASSET_TYPE_XMODEL &&
                                result.nextBodyReference == INLINE_POINTER)
                            {
                                if (const RetailCensusError error =
                                        BeginWorldXModel(nextIndex, stage);
                                    error != RetailCensusError::None)
                                {
                                    return error;
                                }
                                continue;
                            }
                            if (mode == RetailCensusMode::WorldXModelLoader &&
                                result.nextBodyType == ASSET_TYPE_FX &&
                                result.nextBodyReference == INLINE_POINTER)
                            {
                                if (const RetailCensusError error =
                                        BeginWorldFxEffect(nextIndex, stage);
                                    error != RetailCensusError::None)
                                {
                                    return error;
                                }
                                continue;
                            }
                            if (mode == RetailCensusMode::WorldXModelLoader &&
                                result.nextBodyType == ASSET_TYPE_RAW_FILE &&
                                result.nextBodyReference == INLINE_POINTER)
                            {
                                if (const RetailCensusError error =
                                        BeginWorldRawFile(nextIndex, stage);
                                    error != RetailCensusError::None)
                                {
                                    return error;
                                }
                                continue;
                            }
                            if (mode == RetailCensusMode::WorldXModelLoader)
                            {
                                if (const RetailCensusError error =
                                        dispatchSupportedWorldAsset(
                                            nextIndex, stage);
                                    error != RetailCensusError::None)
                                {
                                    return error;
                                }
                                if (complete) return RetailCensusError::None;
                                continue;
                            }
                            result.stoppedBeforeDifferentWorldAssetType =
                                result.nextBodyType != ASSET_TYPE_TECHNIQUE_SET;
                            result.unsupportedOperation =
                                result.stoppedBeforeDifferentWorldAssetType
                                    ? "Load_XAssetHeader(non-technique-set)"
                                    : "Load_XAssetHeader(non-inline technique-set)";
                            stage = RetailCensusStage::AssetBoundary;
                            complete = true;
                            return RetailCensusError::None;
                        }
                        if (result.nextBodyType == ASSET_TYPE_TECHNIQUE_SET &&
                            result.nextBodyReference == INLINE_POINTER)
                        {
                            worldBodyIndex = nextIndex;
                            if (const RetailCensusError error = BeginWorldTechniqueSet(stage);
                                error != RetailCensusError::None) return error;
                            continue;
                        }
                        if ((mode == RetailCensusMode::WorldXModelPrefix ||
                             mode == RetailCensusMode::WorldXSurfacePrefix ||
                             mode == RetailCensusMode::WorldXModelDependencies ||
                             mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                             mode == RetailCensusMode::WorldSecondXModelPrefix ||
                             mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                             mode == RetailCensusMode::WorldSecondXModelDependencies ||
                             mode == RetailCensusMode::WorldXModelLoader) &&
                            result.nextBodyType == ASSET_TYPE_XMODEL &&
                            result.nextBodyReference == INLINE_POINTER)
                        {
                            if (const RetailCensusError error = BeginWorldXModel(
                                    nextIndex, stage);
                                error != RetailCensusError::None) return error;
                            continue;
                        }
                        if (mode == RetailCensusMode::WorldXModelLoader &&
                            result.nextBodyType == ASSET_TYPE_FX &&
                            result.nextBodyReference == INLINE_POINTER)
                        {
                            if (const RetailCensusError error = BeginWorldFxEffect(
                                    nextIndex, stage);
                                error != RetailCensusError::None) return error;
                            continue;
                        }
                        if (mode == RetailCensusMode::WorldXModelLoader &&
                            result.nextBodyType == ASSET_TYPE_RAW_FILE &&
                            result.nextBodyReference == INLINE_POINTER)
                        {
                            if (const RetailCensusError error = BeginWorldRawFile(
                                    nextIndex, stage);
                                error != RetailCensusError::None) return error;
                            continue;
                        }
                        if (mode == RetailCensusMode::WorldXModelLoader)
                        {
                            if (const RetailCensusError error =
                                    dispatchSupportedWorldAsset(
                                        nextIndex, stage);
                                error != RetailCensusError::None)
                            {
                                return error;
                            }
                            if (complete) return RetailCensusError::None;
                            continue;
                        }
                        result.stoppedBeforeDifferentWorldAssetType =
                            result.nextBodyType != ASSET_TYPE_TECHNIQUE_SET;
                        result.unsupportedOperation =
                            result.stoppedBeforeDifferentWorldAssetType
                                ? "Load_XAssetHeader(non-technique-set)"
                                : "Load_XAssetHeader(non-inline technique-set)";
                    }
                }
                else
                {
                    if (mode == RetailCensusMode::WorldXModelLoader)
                    {
                        worldTechniqueSlotIndex = 0u;
                        if (const RetailCensusError error =
                                scheduleWorldTechniqueReference(stage);
                            error != RetailCensusError::None)
                        {
                            return error;
                        }
                        if (!complete) continue;
                        return RetailCensusError::None;
                    }
                    result.worldNextAssetIndex = worldBodyIndex;
                    result.nextBodyIndex = worldBodyIndex;
                    result.nextBodyType = ASSET_TYPE_TECHNIQUE_SET;
                    result.nextBodyReference = INLINE_POINTER;
                    result.stoppedBeforeWorldTechniqueDependency = true;
                    result.unsupportedOperation = "Load_MaterialTechnique";
                }
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount = registry.DefinedAliasCount();
                stage = RetailCensusStage::AssetBoundary;
                complete = true;
                return RetailCensusError::None;
            }
            if (stage == RetailCensusStage::WorldMaterialTechnique)
            {
                const int visit = visitRecord(TECHNIQUE_HEADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailWorldMaterialTechnique &technique =
                    result.worldTechniqueSets.back().techniques.back();
                worldMaterialTechniqueNameToken = ReadU32(record);
                technique.flags = ReadU16(record + 4u);
                technique.passCount = ReadU16(record + 6u);
                if (technique.passCount == 0u)
                    return RetailCensusError::TechniqueLayoutUnsupported;
                if (technique.passCount > limits.maxTechniquePasses)
                    return RetailCensusError::TechniquePassCountLimit;
                const std::uint64_t passBytes64 =
                    static_cast<std::uint64_t>(technique.passCount) *
                    MATERIAL_PASS_BYTES;
                if (passBytes64 > std::numeric_limits<std::uint32_t>::max())
                    return RetailCensusError::TechniquePassCountLimit;
                cursor += TECHNIQUE_HEADER_BYTES;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        1u, passBytes64, &span);
                    error != RetailCensusError::None) return error;
                technique.passArrayBlock4Offset = span.offset;
                worldMaterialArgumentBytes =
                    static_cast<std::uint32_t>(passBytes64);
                stage = RetailCensusStage::WorldMaterialPasses;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialPasses)
            {
                const int visit = visitRecord(worldMaterialArgumentBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailWorldMaterialTechnique &technique =
                    result.worldTechniqueSets.back().techniques.back();
                try
                {
                    worldMaterialPasses.resize(technique.passCount);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                technique.argumentCount = 0u;
                for (std::uint32_t index = 0u;
                     index < technique.passCount; ++index)
                {
                    const std::uint8_t *pass =
                        record + index * MATERIAL_PASS_BYTES;
                    WorldMaterialPassState &state = worldMaterialPasses[index];
                    state.vertexDeclarationToken = ReadU32(pass);
                    state.vertexShaderToken = ReadU32(pass + 4u);
                    state.pixelShaderToken = ReadU32(pass + 8u);
                    state.argumentCount =
                        static_cast<std::uint32_t>(pass[12u]) +
                        static_cast<std::uint32_t>(pass[13u]) +
                        static_cast<std::uint32_t>(pass[14u]);
                    state.argumentToken = ReadU32(pass + 16u);
                    if (pass[12u] > 64u || pass[13u] > 64u ||
                        pass[14u] > 64u || state.argumentCount > 192u ||
                        technique.argumentCount >
                            UINT32_MAX - state.argumentCount)
                    {
                        return RetailCensusError::ShaderArgumentLayoutUnsupported;
                    }
                    technique.argumentCount += state.argumentCount;
                }
                cursor += worldMaterialArgumentBytes;
                ++report.recordsProcessed;
                worldMaterialPassIndex = 0u;
                worldMaterialPassPhase =
                    WorldMaterialPassPhase::VertexDeclaration;
                if (const RetailCensusError error =
                        scheduleWorldMaterialPass(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialVertexDeclaration)
            {
                const int visit = visitRecord(VERTEX_DECLARATION_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                if (record[0u] == 0u || record[0u] > 16u ||
                    record[1u] > 1u || record[2u] != 0u || record[3u] != 0u ||
                    std::any_of(record + 36u, record + VERTEX_DECLARATION_BYTES,
                        [](std::uint8_t byte) { return byte != 0u; }))
                {
                    return RetailCensusError::VertexDeclarationUnsupported;
                }
                cursor += VERTEX_DECLARATION_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error =
                        scheduleWorldMaterialPass(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialVertexShader ||
                stage == RetailCensusStage::WorldMaterialPixelShader)
            {
                const bool vertex =
                    stage == RetailCensusStage::WorldMaterialVertexShader;
                const std::uint32_t shaderBytes = vertex
                    ? VERTEX_SHADER_BYTES : PIXEL_SHADER_BYTES;
                const int visit = visitRecord(shaderBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                worldMaterialShaderNameToken = ReadU32(record);
                const std::uint32_t runtimeShader = ReadU32(record + 4u);
                const std::uint32_t programToken = ReadU32(record + 8u);
                const std::uint32_t programDwords = ReadU16(record + 12u);
                const std::uint32_t loadForRenderer = ReadU16(record + 14u);
                if ((worldMaterialShaderNameToken != INLINE_POINTER &&
                     !ValidPriorZonePointer(worldMaterialShaderNameToken)) ||
                    runtimeShader != 0u || programToken != INLINE_POINTER ||
                    loadForRenderer > 1u)
                {
                    return vertex
                        ? RetailCensusError::VertexShaderLayoutUnsupported
                        : RetailCensusError::PixelShaderLayoutUnsupported;
                }
                if (programDwords == 0u)
                    return RetailCensusError::ShaderProgramSizeInvalid;
                if (programDwords > limits.maxShaderProgramDwords ||
                    programDwords > UINT32_MAX / 4u)
                    return RetailCensusError::ShaderProgramSizeLimit;
                worldMaterialShaderProgramBytes = programDwords * 4u;
                RetailWorldMaterialTechnique &technique =
                    result.worldTechniqueSets.back().techniques.back();
                std::uint32_t &total = vertex
                    ? technique.vertexProgramDwords
                    : technique.pixelProgramDwords;
                if (total > UINT32_MAX - programDwords)
                    return RetailCensusError::ShaderProgramSizeLimit;
                total += programDwords;
                cursor += shaderBytes;
                ++report.recordsProcessed;
                if (worldMaterialShaderNameToken == INLINE_POINTER)
                {
                    stage = vertex
                        ? RetailCensusStage::WorldMaterialVertexShaderName
                        : RetailCensusStage::WorldMaterialPixelShaderName;
                    continue;
                }
                if (const RetailCensusError error = Plan(
                        4u, worldMaterialShaderProgramBytes);
                    error != RetailCensusError::None) return error;
                stage = vertex
                    ? RetailCensusStage::WorldMaterialVertexShaderProgram
                    : RetailCensusStage::WorldMaterialPixelShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialVertexShaderName ||
                stage == RetailCensusStage::WorldMaterialPixelShaderName)
            {
                const bool vertex =
                    stage == RetailCensusStage::WorldMaterialVertexShaderName;
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxShaderNameBytes)
                        return RetailCensusError::VertexShaderNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u)
                    return RetailCensusError::VertexShaderNameInvalid;
                if (bytes > limits.maxShaderNameBytes)
                    return RetailCensusError::VertexShaderNameTooLong;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(
                        4u, worldMaterialShaderProgramBytes);
                    error != RetailCensusError::None) return error;
                stage = vertex
                    ? RetailCensusStage::WorldMaterialVertexShaderProgram
                    : RetailCensusStage::WorldMaterialPixelShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialVertexShaderProgram ||
                stage == RetailCensusStage::WorldMaterialPixelShaderProgram)
            {
                const bool vertex =
                    stage == RetailCensusStage::WorldMaterialVertexShaderProgram;
                const int visit = visitRecord(worldMaterialShaderProgramBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t version = ReadU32(record);
                const std::uint32_t expectedStage =
                    vertex ? 0xfffe0000u : 0xffff0000u;
                if ((version & 0xffff0000u) != expectedStage ||
                    (version & 0xffffu) == 0u ||
                    ReadU32(record + worldMaterialShaderProgramBytes - 4u) !=
                        0x0000ffffu)
                {
                    return vertex
                        ? RetailCensusError::ShaderProgramSignatureInvalid
                        : RetailCensusError::ShaderContractInvalid;
                }
                cursor += worldMaterialShaderProgramBytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error =
                        scheduleWorldMaterialPass(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialShaderArguments)
            {
                const int visit = visitRecord(worldMaterialArgumentBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                worldMaterialLiteralTokens.clear();
                try
                {
                    worldMaterialLiteralTokens.reserve(
                        worldMaterialArgumentBytes / MATERIAL_ARGUMENT_BYTES);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                for (std::uint32_t offset = 0u;
                     offset < worldMaterialArgumentBytes;
                     offset += MATERIAL_ARGUMENT_BYTES)
                {
                    const std::uint16_t type = ReadU16(record + offset);
                    if (type > 7u)
                        return RetailCensusError::ShaderArgumentLayoutUnsupported;
                    if (type == 1u || type == 7u)
                    {
                        try
                        {
                            worldMaterialLiteralTokens.push_back(
                                ReadU32(record + offset + 4u));
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                    }
                }
                cursor += worldMaterialArgumentBytes;
                ++report.recordsProcessed;
                worldMaterialLiteralIndex = 0u;
                if (const RetailCensusError error =
                        scheduleWorldMaterialLiteral(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialLiteralConstant)
            {
                const int visit = visitRecord(MATERIAL_LITERAL_CONSTANT_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                cursor += MATERIAL_LITERAL_CONSTANT_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error =
                        scheduleWorldMaterialLiteral(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldMaterialTechniqueName)
            {
                RetailWorldMaterialTechnique &technique =
                    result.worldTechniqueSets.back().techniques.back();
                if (worldMaterialTechniqueNameToken == INLINE_POINTER)
                {
                    const auto begin = inflated.begin() +
                        static_cast<std::ptrdiff_t>(cursor);
                    const auto terminator = std::find(begin, inflated.end(), 0u);
                    if (terminator == inflated.end())
                    {
                        if (inflated.size() - cursor >=
                            limits.maxTechniqueNameBytes)
                            return RetailCensusError::TechniqueSetNameTooLong;
                        blocked = true;
                        return RetailCensusError::None;
                    }
                const std::size_t bytes =
                        static_cast<std::size_t>(terminator - begin) + 1u;
                    if (bytes <= 1u)
                        return RetailCensusError::TechniqueNameInvalid;
                    if (bytes > limits.maxTechniqueNameBytes)
                        return RetailCensusError::TechniqueSetNameTooLong;
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(
                                1u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        technique.nameBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    try
                    {
                        technique.name.assign(
                            reinterpret_cast<const char *>(
                                inflated.data() + cursor),
                            bytes - 1u);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    if (!ValidPublishedName(technique.name))
                        return RetailCensusError::TechniqueNameInvalid;
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                else
                {
                    if (!ValidPriorZonePointer(
                            worldMaterialTechniqueNameToken))
                        return RetailCensusError::TechniqueAliasInvalid;
                    ++report.recordsProcessed;
                }
                technique.completed = true;
                technique.boundaryInflatedOffset =
                    static_cast<std::uint32_t>(cursor);
                worldTechniqueSlotIndex = technique.slot + 1u;
                if (const RetailCensusError error =
                        scheduleWorldTechniqueReference(stage);
                    error != RetailCensusError::None) return error;
                if (!complete) continue;
                return RetailCensusError::None;
            }
            if (stage == RetailCensusStage::WorldFxEffect)
            {
                const int visit = visitRecord(FX_EFFECT_DEF_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailWorldFxEffectDef &effect = activeWorldFx();
                const std::int32_t looping = ReadS32(record + 16u);
                const std::int32_t oneShot = ReadS32(record + 20u);
                const std::int32_t emission = ReadS32(record + 24u);
                effect.flags = ReadS32(record + 4u);
                effect.totalSize = ReadS32(record + 8u);
                effect.msecLoopingLife = ReadS32(record + 12u);
                effect.elemDefsReference = ReadU32(record + 28u);
                // totalSize describes the runtime allocation built by the FX
                // converter; native DB traversal neither sizes from nor
                // validates it.
                if (ReadU32(record) != INLINE_POINTER ||
                    effect.msecLoopingLife < 0)
                    return RetailCensusError::FxEffectLayoutUnsupported;
                if (looping < 0 || oneShot < 0 || emission < 0)
                    return RetailCensusError::FxEffectLayoutUnsupported;
                const std::uint64_t elemCount =
                    static_cast<std::uint64_t>(looping) +
                    static_cast<std::uint64_t>(oneShot) +
                    static_cast<std::uint64_t>(emission);
                if (elemCount > limits.maxFxElemDefs)
                    return RetailCensusError::FxEffectCountLimit;
                // Native Load_FxEffectDef treats elemDefs as a presence field:
                // any non-null serialized value is replaced by the allocation
                // for the contiguous FxElemDef array.
                if ((elemCount == 0u) != (effect.elemDefsReference == 0u))
                {
                    return RetailCensusError::FxEffectLayoutUnsupported;
                }
                effect.loopingElemCount = static_cast<std::uint32_t>(looping);
                effect.oneShotElemCount = static_cast<std::uint32_t>(oneShot);
                effect.emissionElemCount = static_cast<std::uint32_t>(emission);
                effect.asset->flags = effect.flags;
                effect.asset->totalSize = effect.totalSize;
                effect.asset->msecLoopingLife = effect.msecLoopingLife;
                effect.asset->elemDefCountLooping = looping;
                effect.asset->elemDefCountOneShot = oneShot;
                effect.asset->elemDefCountEmission = emission;
                cursor += FX_EFFECT_DEF_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldFxEffectName;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxEffectName)
            {
                RetailWorldFxEffectDef &effect = activeWorldFx();
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxXModelNameBytes)
                        return RetailCensusError::FxEffectNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::FxEffectNameInvalid;
                if (bytes > limits.maxXModelNameBytes)
                    return RetailCensusError::FxEffectNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    effect.nameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    effect.name.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                    effect.canonicalName =
                        std::make_shared<std::string>(effect.name);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(effect.name))
                    return RetailCensusError::FxEffectNameInvalid;
                effect.asset->name = effect.canonicalName->c_str();
                cursor += bytes;
                ++report.recordsProcessed;
                const std::uint64_t elemCount =
                    static_cast<std::uint64_t>(effect.loopingElemCount) +
                    effect.oneShotElemCount + effect.emissionElemCount;
                if (elemCount == 0u)
                {
                    stage = RetailCensusStage::WorldFxPublish;
                    continue;
                }
                const std::uint64_t headerBytes = elemCount * FX_ELEM_DEF_BYTES;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, headerBytes, &span);
                    error != RetailCensusError::None) return error;
                effect.elemDefsBlock4Offset = span.offset;
                stage = RetailCensusStage::WorldFxElemHeaders;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxElemHeaders)
            {
                RetailWorldFxEffectDef &effect = activeWorldFx();
                const std::size_t elemCount =
                    static_cast<std::size_t>(effect.loopingElemCount) +
                    effect.oneShotElemCount + effect.emissionElemCount;
                const std::size_t bytes = elemCount * FX_ELEM_DEF_BYTES;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                std::vector<RetailWorldFxElemDef> elems;
                try
                {
                    elems.resize(elemCount);
                    effect.canonicalElemDefWords =
                        std::make_shared<std::vector<std::uint32_t>>(
                            elemCount * (FX_ELEM_DEF_BYTES / 4u));
                    std::memcpy(
                        effect.canonicalElemDefWords->data(),
                        inflated.data() + cursor,
                        bytes);
                    effect.asset->elemDefs =
                        reinterpret_cast<const FxElemDef *>(
                            effect.canonicalElemDefWords->data());
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                std::uint64_t visualCountTotal = 0u;
                for (std::size_t index = 0u; index < elemCount; ++index)
                {
                    const std::uint8_t *record = inflated.data() + cursor +
                        index * FX_ELEM_DEF_BYTES;
                    RetailWorldFxElemDef &elem = elems[index];
                    elem.flags = ReadU32(record);
                    elem.elemType = record[176u];
                    elem.visualCount = record[177u];
                    elem.velocityIntervalCount = record[178u];
                    elem.visualStateIntervalCount = record[179u];
                    elem.velocitySamplesReference = ReadU32(record + 180u);
                    elem.visualSamplesReference = ReadU32(record + 184u);
                    elem.visualsReference = ReadU32(record + 188u);
                    elem.effectReferences = {
                        ReadU32(record + 216u), ReadU32(record + 220u),
                        ReadU32(record + 224u)};
                    elem.trailReference = ReadU32(record + 244u);
                    elem.headerBlock4Offset = effect.elemDefsBlock4Offset +
                        static_cast<std::uint32_t>(index * FX_ELEM_DEF_BYTES);
                    if (elem.elemType > 10u ||
                        (elem.velocitySamplesReference != 0u &&
                         elem.velocitySamplesReference != INLINE_POINTER) ||
                        (elem.visualSamplesReference != 0u &&
                         elem.visualSamplesReference != INLINE_POINTER) ||
                        (elem.trailReference != 0u &&
                         elem.trailReference != INLINE_POINTER))
                    {
                        return RetailCensusError::FxElemLayoutUnsupported;
                    }
                    const bool usesVisualArray = elem.elemType == 9u ||
                        elem.visualCount > 1u;
                    if (usesVisualArray && elem.visualsReference != 0u &&
                        elem.visualsReference != INLINE_POINTER)
                    {
                        return RetailCensusError::FxElemLayoutUnsupported;
                    }
                    visualCountTotal += elem.elemType == 9u
                        ? static_cast<std::uint64_t>(elem.visualCount) * 2u
                        : elem.visualCount > 1u ? elem.visualCount : 1u;
                    if (visualCountTotal > limits.maxFxVisuals)
                        return RetailCensusError::FxEffectCountLimit;
                }
                effect.elemDefs.swap(elems);
                cursor += bytes;
                ++report.recordsProcessed;
                worldFxElemIndex = 0u;
                worldFxVisualIndex = 0u;
                worldFxElemPhase = WorldFxElemPhase::VelocitySamples;
                stage = RetailCensusStage::WorldFxElemVelocitySamples;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxElemVelocitySamples)
            {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                if (elem.velocitySamplesReference != 0u)
                {
                    const std::uint64_t bytes64 =
                        (static_cast<std::uint64_t>(elem.velocityIntervalCount) + 1u) *
                        FX_VELOCITY_SAMPLE_BYTES;
                    if (bytes64 > limits.maxFxSampleBytes)
                        return RetailCensusError::FxElemSampleLimit;
                    const std::size_t bytes = static_cast<std::size_t>(bytes64);
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        elem.velocitySamplesBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    elem.velocitySamplesHash = Fnv1a32(
                        std::span<const std::uint8_t>(
                            inflated.data() + cursor, bytes));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                stage = RetailCensusStage::WorldFxElemVisualSamples;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxElemVisualSamples)
            {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                if (elem.visualSamplesReference != 0u)
                {
                    const std::uint64_t bytes64 =
                        (static_cast<std::uint64_t>(elem.visualStateIntervalCount) + 1u) *
                        FX_VISUAL_SAMPLE_BYTES;
                    if (bytes64 > limits.maxFxSampleBytes)
                        return RetailCensusError::FxElemSampleLimit;
                    const std::size_t bytes = static_cast<std::size_t>(bytes64);
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        elem.visualSamplesBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    elem.visualSamplesHash = Fnv1a32(
                        std::span<const std::uint8_t>(
                            inflated.data() + cursor, bytes));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                stage = RetailCensusStage::WorldFxElemVisualArray;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxElemVisualArray)
            {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                const bool usesArray = elem.elemType == 9u ||
                    elem.visualCount > 1u;
                const std::size_t count = elem.elemType == 9u
                    ? static_cast<std::size_t>(elem.visualCount) * 2u
                    : elem.visualCount > 1u ? elem.visualCount : 1u;
                try
                {
                    elem.visualReferences.assign(count, 0u);
                    elem.visualIdentities.assign(count, 0u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (usesArray && elem.visualsReference != 0u)
                {
                    const std::size_t bytes = count * 4u;
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        elem.visualArrayBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    for (std::size_t index = 0u; index < count; ++index)
                    {
                        elem.visualReferences[index] =
                            ReadU32(inflated.data() + cursor + index * 4u);
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                else if (!usesArray)
                {
                    elem.visualArrayBlock4Offset = elem.headerBlock4Offset + 188u;
                    elem.visualReferences[0] = elem.visualsReference;
                }
                worldFxVisualIndex = 0u;
                stage = RetailCensusStage::WorldFxElemVisuals;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxElemVisuals)
            {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                while (worldFxVisualIndex < elem.visualReferences.size())
                {
                    const std::uint32_t token =
                        elem.visualReferences[worldFxVisualIndex];
                    const ZoneSpan aliasSlot{
                        4u,
                        elem.visualArrayBlock4Offset + worldFxVisualIndex * 4u,
                        4u,
                    };
                    if (elem.elemType == 6u || elem.elemType == 7u || token == 0u)
                    {
                        ++worldFxVisualIndex;
                        continue;
                    }
                    if (elem.elemType == 8u || elem.elemType == 10u)
                    {
                        worldFxStringReference = token;
                        worldFxElemPhase = WorldFxElemPhase::VisualString;
                        stage = RetailCensusStage::WorldFxString;
                        break;
                    }
                    if (elem.elemType == 5u)
                    {
                        if (token == INLINE_POINTER || token == SHARED_POINTER)
                        {
                            if (const RetailCensusError error =
                                    BeginNestedWorldXModel(
                                        token, aliasSlot, worldFxVisualIndex, stage);
                                error != RetailCensusError::None) return error;
                            break;
                        }
                        std::uint32_t identity = 0u;
                        if (ResolveRegistryAlias(
                                token, ASSET_TYPE_XMODEL, identity) !=
                            ZoneRegistryError::None)
                        {
                            return RetailCensusError::FxElemVisualInvalid;
                        }
                        elem.visualIdentities[worldFxVisualIndex] = identity;
                        ++worldFxVisualIndex;
                        continue;
                    }
                    if (token == INLINE_POINTER || token == SHARED_POINTER)
                    {
                        worldMaterialOwnedByFx = true;
                        worldMaterialAliasSlot = aliasSlot;
                        if (const RetailCensusError error = MapRegistryError(
                                registry.ReserveAlias(
                                    aliasSlot, ASSET_TYPE_MATERIAL));
                            error != RetailCensusError::None) return error;
                        worldMaterialHasInsertAlias = token == SHARED_POINTER;
                        if (worldMaterialHasInsertAlias)
                        {
                            if (const RetailCensusError error = Plan(
                                    4u, 4u, &worldMaterialInsertAliasSlot);
                                error != RetailCensusError::None) return error;
                            if (const RetailCensusError error = MapRegistryError(
                                    registry.ReserveAlias(
                                        worldMaterialInsertAliasSlot,
                                        ASSET_TYPE_MATERIAL));
                                error != RetailCensusError::None) return error;
                        }
                        if (const RetailCensusError error = Push(0u);
                            error != RetailCensusError::None) return error;
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(
                                4u, MATERIAL_BYTES, &span);
                            error != RetailCensusError::None) return error;
                        try
                        {
                            activeWorldFx().materials.emplace_back();
                            activeWorldFx().materials.back().asset =
                                std::make_shared<Material>();
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                        RetailWorldFxMaterial &material =
                            activeWorldFx().materials.back();
                        *material.asset = {};
                        material.handleIndex = worldFxVisualIndex;
                        material.headerBlock0Offset = span.offset;
                        worldTextureIndex = 0u;
                        stage = RetailCensusStage::WorldXModelMaterial;
                        break;
                    }
                    std::uint32_t identity = 0u;
                    ZoneRegistryError resolveError = ResolveRegistryAlias(
                        token, ASSET_TYPE_MATERIAL, identity);
                    if (resolveError != ZoneRegistryError::None)
                    {
                        // Native DB_ConvertOffsetToAlias dereferences the
                        // already-patched four-byte cell in block 4. Preserve
                        // that pointer-cell behavior even when the bounded
                        // traversal has not otherwise needed to name the
                        // target cell: an earlier fixed FxElemDef visual is
                        // the canonical Material handle, not a new asset.
                        ZoneSpan target{};
                        std::uint32_t ownerSlot = 0u;
                        if (DecodeZoneAliasToken(token, target) &&
                            target.block == 4u)
                        {
                            const auto exact =
                                block4MaterialVisualAliases.find(target.offset);
                            if (exact != block4MaterialVisualAliases.end())
                            {
                                identity = exact->second;
                                ownerSlot = target.offset;
                            }
                            else
                            {
                                // The prerequisite traversal still has a
                                // bounded UI allocation-accounting gap. Keep
                                // native pointer-cell semantics by selecting
                                // the nearest preceding patched Material
                                // visual cell within one FxElemDef record.
                                for (const auto &[candidateSlot,
                                                 candidateIdentity] :
                                     block4MaterialVisualAliases)
                                {
                                    if (candidateIdentity == 0u ||
                                        candidateSlot > target.offset ||
                                        target.offset - candidateSlot >=
                                            FX_ELEM_DEF_BYTES ||
                                        (identity != 0u &&
                                         candidateSlot <= ownerSlot))
                                    {
                                        continue;
                                    }
                                    identity = candidateIdentity;
                                    ownerSlot = candidateSlot;
                                }
                            }
                        }
                        if (identity == 0u)
                        {
                            if (prerequisiteZone &&
                                ValidPriorZonePointer(token, 4u))
                            {
                                ++worldFxVisualIndex;
                                continue;
                            }
                            return RetailCensusError::FxElemVisualInvalid;
                        }
                        resolveError = registry.ReserveAlias(
                            target, ASSET_TYPE_MATERIAL);
                        if (resolveError != ZoneRegistryError::None)
                            return RetailCensusError::FxElemVisualInvalid;
                        resolveError = registry.PublishAlias(target, identity);
                        if (resolveError != ZoneRegistryError::None)
                            return RetailCensusError::FxElemVisualInvalid;
                    }
                    if (const RetailCensusError error = MapRegistryError(
                            registry.ReserveAlias(
                                aliasSlot, ASSET_TYPE_MATERIAL));
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(aliasSlot, identity));
                        error != RetailCensusError::None) return error;
                    try
                    {
                        block4MaterialVisualAliases.insert_or_assign(
                            aliasSlot.offset, identity);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    elem.visualIdentities[worldFxVisualIndex] = identity;
                    ++worldFxVisualIndex;
                }
                if (stage != RetailCensusStage::WorldFxElemVisuals) continue;
                worldFxElemPhase = WorldFxElemPhase::EffectOnImpact;
                worldFxStringReference = elem.effectReferences[0u];
                stage = RetailCensusStage::WorldFxString;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxString)
            {
                const std::uint32_t token = worldFxStringReference;
                if (token == INLINE_POINTER)
                {
                    const auto begin = inflated.begin() +
                        static_cast<std::ptrdiff_t>(cursor);
                    const auto terminator = std::find(begin, inflated.end(), 0u);
                    if (terminator == inflated.end())
                    {
                        if (inflated.size() - cursor >= limits.maxXModelNameBytes)
                            return RetailCensusError::FxEffectNameTooLong;
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    const std::size_t bytes =
                        static_cast<std::size_t>(terminator - begin) + 1u;
                    if (bytes <= 1u || bytes > limits.maxXModelNameBytes)
                        return bytes <= 1u
                            ? RetailCensusError::FxStringReferenceInvalid
                            : RetailCensusError::FxEffectNameTooLong;
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(
                                1u, bytes, &worldFxStringSpan);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    if (!ValidPublishedName(std::string_view(
                            reinterpret_cast<const char *>(
                                inflated.data() + cursor), bytes - 1u)))
                    {
                        return RetailCensusError::FxStringReferenceInvalid;
                    }
                    if (worldFxStringSpan.block == 4u)
                    {
                        try
                        {
                            block4StringAliases.emplace(
                                worldFxStringSpan.offset,
                                std::make_shared<std::string>(
                                    reinterpret_cast<const char *>(
                                        inflated.data() + cursor),
                                    bytes - 1u));
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                else if (token != 0u && !ValidPriorZonePointer(token))
                {
                    return RetailCensusError::FxStringReferenceInvalid;
                }
                if (worldFxElemPhase == WorldFxElemPhase::VisualString)
                {
                    ++worldFxVisualIndex;
                    stage = RetailCensusStage::WorldFxElemVisuals;
                    continue;
                }
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                if (worldFxElemPhase == WorldFxElemPhase::EffectOnImpact)
                {
                    worldFxElemPhase = WorldFxElemPhase::EffectOnDeath;
                    worldFxStringReference = elem.effectReferences[1u];
                    continue;
                }
                if (worldFxElemPhase == WorldFxElemPhase::EffectOnDeath)
                {
                    worldFxElemPhase = WorldFxElemPhase::EffectEmitted;
                    worldFxStringReference = elem.effectReferences[2u];
                    continue;
                }
                if (worldFxElemPhase != WorldFxElemPhase::EffectEmitted)
                    return RetailCensusError::ZoneStreamInvalid;
                stage = RetailCensusStage::WorldFxTrail;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxTrail)
            {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                if (elem.trailReference == 0u)
                {
                    if (const RetailCensusError error = finishWorldFxElem(stage);
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(
                            4u, FX_TRAIL_DEF_BYTES);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(FX_TRAIL_DEF_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::int32_t vertCount = ReadS32(record + 12u);
                const std::int32_t indexCount = ReadS32(record + 20u);
                worldFxTrailVertsReference = ReadU32(record + 16u);
                worldFxTrailIndicesReference = ReadU32(record + 24u);
                if (vertCount < 0 || indexCount < 0 ||
                    static_cast<std::uint32_t>(vertCount) >
                        limits.maxFxTrailVertices ||
                    static_cast<std::uint32_t>(indexCount) >
                        limits.maxFxTrailIndices ||
                    ((vertCount == 0) != (worldFxTrailVertsReference == 0u)) ||
                    ((indexCount == 0) != (worldFxTrailIndicesReference == 0u)) ||
                    (vertCount != 0 &&
                     worldFxTrailVertsReference != INLINE_POINTER) ||
                    (indexCount != 0 &&
                     worldFxTrailIndicesReference != INLINE_POINTER))
                {
                    return RetailCensusError::FxTrailInvalid;
                }
                worldFxTrailVertexCount = static_cast<std::uint32_t>(vertCount);
                worldFxTrailIndexCount = static_cast<std::uint32_t>(indexCount);
                elem.trailVertexCount = worldFxTrailVertexCount;
                elem.trailIndexCount = worldFxTrailIndexCount;
                elem.trailPayloadHash = Fnv1a32(
                    std::span<const std::uint8_t>(record, FX_TRAIL_DEF_BYTES));
                cursor += FX_TRAIL_DEF_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldFxTrailVertices;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxTrailVertices)
            {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                const std::size_t bytes =
                    static_cast<std::size_t>(worldFxTrailVertexCount) *
                    FX_TRAIL_VERTEX_BYTES;
                if (bytes != 0u)
                {
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(4u, bytes);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    elem.trailPayloadHash = Fnv1a32(
                        std::span<const std::uint8_t>(
                            inflated.data() + cursor, bytes),
                        elem.trailPayloadHash);
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                stage = RetailCensusStage::WorldFxTrailIndices;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxTrailIndices)
            {
                RetailWorldFxElemDef &elem = activeWorldFxElem();
                const std::size_t bytes =
                    static_cast<std::size_t>(worldFxTrailIndexCount) * 2u;
                if (bytes != 0u)
                {
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(2u, bytes);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    elem.trailPayloadHash = Fnv1a32(
                        std::span<const std::uint8_t>(
                            inflated.data() + cursor, bytes),
                        elem.trailPayloadHash);
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                if (const RetailCensusError error = finishWorldFxElem(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxPublish)
            {
                RetailWorldFxEffectDef &effect = activeWorldFx();
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_FX, effect.assetIndex,
                            effect.name, effect.identity));
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.PublishAlias(worldFxAliasSlot, effect.identity));
                    error != RetailCensusError::None) return error;
                effect.published = true;
                effect.boundaryInflatedOffset = static_cast<std::uint32_t>(cursor);
                if (const RetailCensusError error = AppendSemanticTrace(
                        kisak::database::SemanticTraceEventKind::AssetPublish,
                        ASSET_TYPE_FX,
                        effect.assetIndex,
                        effect.identity,
                        effect.boundaryInflatedOffset,
                        {0u, effect.headerBlock0Offset, FX_EFFECT_DEF_BYTES},
                        effect.name,
                        worldFxAliasSlot);
                    error != RetailCensusError::None)
                {
                    return error;
                }
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount = registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        effect.assetIndex + 1u, stage);
                    error != RetailCensusError::None) return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxImpactTable)
            {
                const int visit = visitRecord(FX_IMPACT_TABLE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                worldFxImpactNameReference = ReadU32(inflated.data() + cursor);
                worldFxImpactTableReference = ReadU32(
                    inflated.data() + cursor + 4u);
                if (worldFxImpactNameReference == 0u ||
                    worldFxImpactNameReference == SHARED_POINTER)
                {
                    return RetailCensusError::FxEffectLayoutUnsupported;
                }
                cursor += FX_IMPACT_TABLE_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldFxImpactName;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxImpactName)
            {
                if (worldFxImpactNameReference == INLINE_POINTER)
                {
                    const auto begin = inflated.begin() +
                        static_cast<std::ptrdiff_t>(cursor);
                    const auto terminator = std::find(begin, inflated.end(), 0u);
                    if (terminator == inflated.end())
                    {
                        if (inflated.size() - cursor >=
                            limits.maxXModelNameBytes)
                        {
                            return RetailCensusError::FxEffectNameTooLong;
                        }
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    const std::size_t bytes =
                        static_cast<std::size_t>(terminator - begin) + 1u;
                    if (bytes <= 1u || bytes > limits.maxXModelNameBytes)
                        return RetailCensusError::FxStringReferenceInvalid;
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(1u, bytes);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    if (!ValidPublishedName(std::string_view(
                            reinterpret_cast<const char *>(
                                inflated.data() + cursor), bytes - 1u)))
                    {
                        return RetailCensusError::FxStringReferenceInvalid;
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                else if (!ValidPriorZonePointer(
                        worldFxImpactNameReference))
                {
                    return RetailCensusError::FxStringReferenceInvalid;
                }
                if (worldFxImpactTableReference != 0u)
                {
                    if (const RetailCensusError error = Plan(
                            4u,
                            FX_IMPACT_ENTRY_COUNT * FX_IMPACT_ENTRY_BYTES);
                        error != RetailCensusError::None) return error;
                    stage = RetailCensusStage::WorldFxImpactEntries;
                }
                else
                {
                    stage = RetailCensusStage::WorldFxImpactPublish;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldFxImpactEntries)
            {
                constexpr std::size_t bytes =
                    FX_IMPACT_ENTRY_COUNT * FX_IMPACT_ENTRY_BYTES;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                for (std::size_t offset = 0u; offset < bytes; offset += 4u)
                {
                    const std::uint32_t token = ReadU32(
                        inflated.data() + cursor + offset);
                    if (token == 0u) continue;
                    if (token == INLINE_POINTER || token == SHARED_POINTER ||
                        !ValidPriorZonePointer(token, 4u))
                    {
                        return RetailCensusError::FxElemVisualInvalid;
                    }
                }
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldFxImpactPublish;
                continue;
            }
            if (stage == RetailCensusStage::WorldFxImpactPublish)
            {
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                ++result.completedAssetCount;
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        worldFxImpactAssetIndex + 1u, stage);
                    error != RetailCensusError::None) return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldMenuTasks)
            {
                if (worldMenuTasks.empty())
                    return RetailCensusError::ZoneStreamInvalid;
                const WorldMenuTask task = worldMenuTasks.front();
                auto finishTask = [&]() noexcept {
                    worldMenuTasks.pop_front();
                    ++report.recordsProcessed;
                };
                if (task.kind == WorldMenuTaskKind::Push0 ||
                    task.kind == WorldMenuTaskKind::Push4)
                {
                    if (const RetailCensusError error = Push(
                            task.kind == WorldMenuTaskKind::Push0 ? 0u : 4u);
                        error != RetailCensusError::None) return error;
                    finishTask();
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::Pop)
                {
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    finishTask();
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::InsertPointer)
                {
                    if (const RetailCensusError error = Push(4u);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Plan(4u, 4u);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    finishTask();
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::Finish)
                {
                    finishTask();
                    ++result.completedAssetCount;
                    if (const RetailCensusError error =
                            dispatchSupportedWorldAsset(
                                worldMenuAssetIndex + 1u, stage);
                        error != RetailCensusError::None) return error;
                    if (complete) return RetailCensusError::None;
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::String)
                {
                    if (task.value == 0u)
                    {
                        finishTask();
                        continue;
                    }
                    if (task.value != INLINE_POINTER)
                    {
                        if (!ValidPriorZonePointer(task.value))
                            return RetailCensusError::FxStringReferenceInvalid;
                        finishTask();
                        continue;
                    }
                    const auto begin = inflated.begin() +
                        static_cast<std::ptrdiff_t>(cursor);
                    const auto terminator = std::find(begin, inflated.end(), 0u);
                    if (terminator == inflated.end())
                    {
                        if (inflated.size() - cursor >=
                            limits.maxLocalizeStringBytes)
                        {
                            return RetailCensusError::LocalizeStringTooLong;
                        }
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    const std::size_t bytes =
                        static_cast<std::size_t>(terminator - begin) + 1u;
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(
                                1u, bytes, &worldMenuStringSpan);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    if (worldMenuStringSpan.block == 4u)
                    {
                        try
                        {
                            block4StringAliases.emplace(
                                worldMenuStringSpan.offset,
                                std::make_shared<std::string>(
                                    reinterpret_cast<const char *>(
                                        inflated.data() + cursor),
                                    bytes - 1u));
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                    }
                    cursor += bytes;
                    finishTask();
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::Material)
                {
                    if (task.value == 0u)
                    {
                        finishTask();
                        continue;
                    }
                    if (task.value != INLINE_POINTER &&
                        task.value != SHARED_POINTER)
                    {
                        if (!ValidPriorZonePointer(task.value, 4u))
                            return RetailCensusError::MaterialLayoutUnsupported;
                        finishTask();
                        continue;
                    }
                    finishTask();
                    worldMaterialTopLevel = false;
                    worldMaterialOwnedByFx = false;
                    worldMaterialOwnedByWeapon = false;
                    worldMaterialOwnedByMenu = true;
                    worldMaterialAliasSlot = {
                        task.extra >> 28u,
                        task.extra & 0x0fffffffu,
                        4u,
                    };
                    if (worldMaterialAliasSlot.block == 4u)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.ReserveAlias(
                                    worldMaterialAliasSlot,
                                    ASSET_TYPE_MATERIAL));
                            error != RetailCensusError::None) return error;
                    }
                    worldMaterialHasInsertAlias =
                        task.value == SHARED_POINTER;
                    if (worldMaterialHasInsertAlias)
                    {
                        if (const RetailCensusError error = Plan(
                                4u, 4u, &worldMaterialInsertAliasSlot);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = MapRegistryError(
                                registry.ReserveAlias(
                                    worldMaterialInsertAliasSlot,
                                    ASSET_TYPE_MATERIAL));
                            error != RetailCensusError::None) return error;
                    }
                    if (const RetailCensusError error = Push(0u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, MATERIAL_BYTES, &span);
                        error != RetailCensusError::None) return error;
                    try
                    {
                        result.worldMaterials.emplace_back();
                        result.worldMaterials.back().asset =
                            std::make_shared<Material>();
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    RetailXModelMaterial &material = result.worldMaterials.back();
                    *material.asset = {};
                    material.headerBlock0Offset = span.offset;
                    worldTextureIndex = 0u;
                    stage = RetailCensusStage::WorldXModelMaterial;
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::Sound)
                {
                    if (task.value == 0u)
                    {
                        finishTask();
                        continue;
                    }
                    if (task.value == INLINE_POINTER ||
                        task.value == SHARED_POINTER ||
                        !ValidPriorZonePointer(task.value, 4u))
                    {
                        return RetailCensusError::SoundAliasDependencyUnsupported;
                    }
                    finishTask();
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::MenuDefPointer)
                {
                    finishTask();
                    if (task.value == 0u) continue;
                    if (task.value != INLINE_POINTER &&
                        task.value != SHARED_POINTER)
                    {
                        if (!ValidPriorZonePointer(task.value, 4u))
                            return RetailCensusError::ZoneStreamInvalid;
                        continue;
                    }
                    const RetailCensusError error = task.value == SHARED_POINTER
                        ? prependWorldMenuTasks({
                            {WorldMenuTaskKind::Push0},
                            {WorldMenuTaskKind::InsertPointer},
                            {WorldMenuTaskKind::MenuDef}})
                        : prependWorldMenuTasks({
                            {WorldMenuTaskKind::Push0},
                            {WorldMenuTaskKind::MenuDef}});
                    if (error != RetailCensusError::None) return error;
                    continue;
                }

                if (task.kind == WorldMenuTaskKind::KeyHandler &&
                    task.value == 0u)
                {
                    finishTask();
                    continue;
                }
                if ((task.kind == WorldMenuTaskKind::MenuPointerArray ||
                     task.kind == WorldMenuTaskKind::ItemPointerArray ||
                     task.kind == WorldMenuTaskKind::StringPointerArray) &&
                    task.value == 0u)
                {
                    finishTask();
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::StatementPointerArray)
                {
                    if (task.value > 65536u ||
                        ((task.value == 0u) != (task.extra == 0u)))
                        return RetailCensusError::ZoneStreamInvalid;
                    if (task.value == 0u)
                    {
                        finishTask();
                        continue;
                    }
                }

                std::size_t fixedBytes = 0u;
                switch (task.kind)
                {
                case WorldMenuTaskKind::MenuList: fixedBytes = 12u; break;
                case WorldMenuTaskKind::MenuPointerArray:
                case WorldMenuTaskKind::ItemPointerArray:
                case WorldMenuTaskKind::StatementPointerArray:
                case WorldMenuTaskKind::StringPointerArray:
                    fixedBytes = static_cast<std::size_t>(task.value) * 4u; break;
                case WorldMenuTaskKind::MenuDef: fixedBytes = 284u; break;
                case WorldMenuTaskKind::ItemDef: fixedBytes = 372u; break;
                case WorldMenuTaskKind::KeyHandler: fixedBytes = 12u; break;
                case WorldMenuTaskKind::Expression: fixedBytes = 12u; break;
                case WorldMenuTaskKind::ListBox: fixedBytes = 340u; break;
                case WorldMenuTaskKind::EditField: fixedBytes = 32u; break;
                case WorldMenuTaskKind::Multi: fixedBytes = 392u; break;
                case WorldMenuTaskKind::StringTable: fixedBytes = 16u; break;
                default: return RetailCensusError::ZoneStreamInvalid;
                }
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(
                            4u, fixedBytes, &worldMenuFixedSpan);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(fixedBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                cursor += fixedBytes;
                finishTask();

                if (task.kind == WorldMenuTaskKind::MenuList)
                {
                    const std::int32_t count = ReadS32(record + 4u);
                    const std::uint32_t menus = ReadU32(record + 8u);
                    if (count < 0 || count > 1024 ||
                        ((count == 0) != (menus == 0u)))
                        return RetailCensusError::ZoneStreamInvalid;
                    if (const RetailCensusError error = prependWorldMenuTasks({
                            {WorldMenuTaskKind::Push4},
                            {WorldMenuTaskKind::String, ReadU32(record)},
                            {WorldMenuTaskKind::MenuPointerArray,
                                static_cast<std::uint32_t>(count)},
                            {WorldMenuTaskKind::Pop},
                            {WorldMenuTaskKind::Pop},
                            {WorldMenuTaskKind::Finish}});
                        error != RetailCensusError::None) return error;
                    if (count == 0)
                    {
                        auto iterator = worldMenuTasks.begin();
                        std::advance(iterator, 2);
                        worldMenuTasks.erase(iterator);
                    }
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::MenuPointerArray ||
                    task.kind == WorldMenuTaskKind::ItemPointerArray ||
                    task.kind == WorldMenuTaskKind::StatementPointerArray ||
                    task.kind == WorldMenuTaskKind::StringPointerArray)
                {
                    try
                    {
                        for (std::size_t index = task.value; index-- > 0u;)
                        {
                            const std::uint32_t token = ReadU32(
                                record + index * 4u);
                            if (token == 0u) continue;
                            const WorldMenuTaskKind child =
                                task.kind == WorldMenuTaskKind::StringPointerArray
                                    ? WorldMenuTaskKind::String
                                    : task.kind == WorldMenuTaskKind::MenuPointerArray
                                    ? WorldMenuTaskKind::MenuDefPointer
                                    : task.kind == WorldMenuTaskKind::ItemPointerArray
                                        ? WorldMenuTaskKind::ItemDef
                                        : WorldMenuTaskKind::Expression;
                            worldMenuTasks.push_front({child, token});
                        }
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::StringTable)
                {
                    const std::int32_t columns = ReadS32(record + 4u);
                    const std::int32_t rows = ReadS32(record + 8u);
                    if (columns < 0 || rows < 0 || columns > 65536 ||
                        rows > 65536)
                        return RetailCensusError::ZoneStreamInvalid;
                    const std::uint64_t count64 =
                        static_cast<std::uint64_t>(columns) * rows;
                    if (count64 > 1024u * 1024u ||
                        ((count64 == 0u) != (ReadU32(record + 12u) == 0u)))
                        return RetailCensusError::ZoneStreamInvalid;
                    if (const RetailCensusError error = prependWorldMenuTasks({
                            {WorldMenuTaskKind::String, ReadU32(record)},
                            {WorldMenuTaskKind::StringPointerArray,
                                static_cast<std::uint32_t>(count64)}});
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::MenuDef)
                {
                    const std::int32_t itemCount = ReadS32(record + 164u);
                    if (itemCount < 0 || itemCount > 4096 ||
                        ((itemCount == 0) != (ReadU32(record + 280u) == 0u)))
                        return RetailCensusError::ZoneStreamInvalid;
                    if (const RetailCensusError error = prependWorldMenuTasks({
                        {WorldMenuTaskKind::Push4},
                        {WorldMenuTaskKind::String, ReadU32(record)},
                        {WorldMenuTaskKind::String, ReadU32(record + 52u)},
                        {WorldMenuTaskKind::Material, ReadU32(record + 152u),
                            (worldMenuFixedSpan.block << 28u) |
                                (worldMenuFixedSpan.offset + 152u)},
                        {WorldMenuTaskKind::String, ReadU32(record + 156u)},
                        {WorldMenuTaskKind::String, ReadU32(record + 196u)},
                        {WorldMenuTaskKind::String, ReadU32(record + 200u)},
                        {WorldMenuTaskKind::String, ReadU32(record + 204u)},
                        {WorldMenuTaskKind::KeyHandler, ReadU32(record + 208u)},
                        {WorldMenuTaskKind::StatementPointerArray,
                            static_cast<std::uint32_t>(ReadS32(record + 212u)),
                            ReadU32(record + 216u)},
                        {WorldMenuTaskKind::String, ReadU32(record + 220u)},
                        {WorldMenuTaskKind::String, ReadU32(record + 224u)},
                        {WorldMenuTaskKind::StatementPointerArray,
                            static_cast<std::uint32_t>(ReadS32(record + 264u)),
                            ReadU32(record + 268u)},
                        {WorldMenuTaskKind::StatementPointerArray,
                            static_cast<std::uint32_t>(ReadS32(record + 272u)),
                            ReadU32(record + 276u)},
                        {WorldMenuTaskKind::ItemPointerArray,
                            static_cast<std::uint32_t>(itemCount)},
                        {WorldMenuTaskKind::Pop},
                        {WorldMenuTaskKind::Pop}});
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::KeyHandler)
                {
                    const std::uint32_t next = ReadU32(record + 8u);
                    const RetailCensusError error = next == 0u
                        ? prependWorldMenuTasks({
                            {WorldMenuTaskKind::String,
                                ReadU32(record + 4u)}})
                        : prependWorldMenuTasks({
                            {WorldMenuTaskKind::String,
                                ReadU32(record + 4u)},
                            {WorldMenuTaskKind::KeyHandler, next}});
                    if (error != RetailCensusError::None) return error;
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::Expression)
                {
                    if (ReadS32(record) != 0 && ReadS32(record + 4u) == 2)
                    {
                        if (const RetailCensusError error =
                                prependWorldMenuTasks({{WorldMenuTaskKind::String,
                                    ReadU32(record + 8u)}});
                            error != RetailCensusError::None) return error;
                    }
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::ItemDef)
                {
                    const std::uint32_t type = ReadU32(record + 180u);
                    const std::uint32_t data = ReadU32(record + 300u);
                    WorldMenuTaskKind dataKind = WorldMenuTaskKind::Finish;
                    if (type == 6u) dataKind = WorldMenuTaskKind::ListBox;
                    else if (type == 12u) dataKind = WorldMenuTaskKind::Multi;
                    else if (type == 13u) dataKind = WorldMenuTaskKind::String;
                    else if (type == 0u || type == 4u || type == 9u ||
                             type == 10u || type == 11u || type == 14u ||
                             type == 16u || type == 17u || type == 18u)
                        dataKind = WorldMenuTaskKind::EditField;
                    try
                    {
                        std::vector<WorldMenuTask> children;
                        children.reserve(27u);
                        children.push_back({WorldMenuTaskKind::String,
                            ReadU32(record)});
                        children.push_back({WorldMenuTaskKind::String,
                            ReadU32(record + 52u)});
                        children.push_back({WorldMenuTaskKind::Material,
                            ReadU32(record + 152u),
                            (worldMenuFixedSpan.block << 28u) |
                                (worldMenuFixedSpan.offset + 152u)});
                        for (const std::uint32_t offset : {
                                 224u, 236u, 240u, 244u, 248u, 252u,
                                 256u, 260u, 264u, 268u, 272u})
                        {
                            children.push_back({WorldMenuTaskKind::String,
                                ReadU32(record + offset)});
                        }
                        children.push_back({WorldMenuTaskKind::KeyHandler,
                            ReadU32(record + 276u)});
                        children.push_back({WorldMenuTaskKind::String,
                            ReadU32(record + 280u)});
                        children.push_back({WorldMenuTaskKind::Sound,
                            ReadU32(record + 288u)});
                        if (data != 0u && dataKind != WorldMenuTaskKind::Finish)
                            children.push_back({dataKind, data});
                        for (std::size_t statement = 0u; statement < 8u;
                             ++statement)
                        {
                            const std::size_t offset = 308u + statement * 8u;
                            children.push_back({
                                WorldMenuTaskKind::StatementPointerArray,
                                static_cast<std::uint32_t>(ReadS32(record + offset)),
                                ReadU32(record + offset + 4u)});
                        }
                        for (auto iterator = children.rbegin();
                             iterator != children.rend(); ++iterator)
                            worldMenuTasks.push_front(*iterator);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::ListBox)
                {
                    if (const RetailCensusError error = prependWorldMenuTasks({
                            {WorldMenuTaskKind::String, ReadU32(record + 288u)},
                            {WorldMenuTaskKind::Material,
                                ReadU32(record + 336u),
                                (worldMenuFixedSpan.block << 28u) |
                                    (worldMenuFixedSpan.offset + 336u)}});
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (task.kind == WorldMenuTaskKind::Multi)
                {
                    try
                    {
                        for (std::size_t index = 64u; index-- > 0u;)
                            worldMenuTasks.push_front({WorldMenuTaskKind::String,
                                ReadU32(record + index * 4u)});
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    continue;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldLocalizeEntry)
            {
                const int visit = visitRecord(LOCALIZE_ENTRY_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                worldLocalizeValueReference = ReadU32(record);
                worldLocalizeNameReference = ReadU32(record + 4u);
                if (worldLocalizeNameReference == 0u ||
                    worldLocalizeNameReference == SHARED_POINTER)
                {
                    return RetailCensusError::LocalizeStringInvalid;
                }
                cursor += LOCALIZE_ENTRY_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldLocalizeValue;
                continue;
            }
            if (stage == RetailCensusStage::WorldLocalizeValue ||
                stage == RetailCensusStage::WorldLocalizeName)
            {
                RetailPublishedLocalizeEntry &entry = activeWorldLocalize();
                const bool loadingName =
                    stage == RetailCensusStage::WorldLocalizeName;
                const std::uint32_t reference = loadingName
                    ? worldLocalizeNameReference
                    : worldLocalizeValueReference;
                auto finishString = [&](std::shared_ptr<std::string> value,
                                        std::uint32_t offset) noexcept
                    -> RetailCensusError {
                    if (loadingName)
                    {
                        if (!value)
                            return RetailCensusError::LocalizeStringInvalid;
                        entry.storage->name = std::move(value);
                        entry.nameBlock4Offset = offset;
                        entry.asset->name = entry.storage->name->c_str();
                        stage = RetailCensusStage::WorldLocalizePublish;
                    }
                    else
                    {
                        entry.storage->value = std::move(value);
                        entry.valueBlock4Offset = offset;
                        entry.asset->value = entry.storage->value
                            ? entry.storage->value->c_str() : nullptr;
                        stage = RetailCensusStage::WorldLocalizeName;
                    }
                    return RetailCensusError::None;
                };

                if (reference == 0u)
                {
                    if (const RetailCensusError error =
                            finishString({}, UINT32_MAX);
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (reference == SHARED_POINTER)
                    return RetailCensusError::LocalizeStringInvalid;
                if (reference != INLINE_POINTER)
                {
                    std::shared_ptr<std::string> value;
                    std::uint32_t offset = UINT32_MAX;
                    if (!ResolvePriorZoneStringPayload(reference, value, offset))
                        return RetailCensusError::LocalizeStringInvalid;
                    if (const RetailCensusError error =
                            finishString(std::move(value), offset);
                        error != RetailCensusError::None) return error;
                    continue;
                }

                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >=
                        limits.maxLocalizeStringBytes)
                    {
                        return RetailCensusError::LocalizeStringTooLong;
                    }
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxLocalizeStringBytes)
                {
                    return RetailCensusError::LocalizeStringTooLong;
                }
                if (recordVisited == 0u)
                {
                    if (bytes > limits.maxRetainedLocalizeBytes -
                            retainedLocalizeBytes)
                    {
                        return RetailCensusError::LocalizePayloadLimit;
                    }
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    if (loadingName) entry.nameBlock4Offset = span.offset;
                    else entry.valueBlock4Offset = span.offset;
                }
                const int visitString = visitRecord(bytes);
                if (visitString <= 0) return RetailCensusError::None;
                std::shared_ptr<std::string> value;
                try
                {
                    value = std::make_shared<std::string>(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                entry.payloadBytes += static_cast<std::uint32_t>(bytes);
                retainedLocalizeBytes += bytes;
                const std::uint32_t offset = loadingName
                    ? entry.nameBlock4Offset : entry.valueBlock4Offset;
                if (const RetailCensusError error =
                        finishString(std::move(value), offset);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldLocalizePublish)
            {
                RetailPublishedLocalizeEntry &entry = activeWorldLocalize();
                if (!entry.pointerAlias && entry.serializedReference != 0u)
                {
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (!entry.storage || !entry.storage->name || !entry.asset)
                        return RetailCensusError::LocalizeLayoutUnsupported;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.RegisterAsset(
                                ASSET_TYPE_LOCALIZE,
                                entry.assetIndex,
                                *entry.storage->name,
                                entry.identity));
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                worldLocalizeAliasSlot, entry.identity));
                        error != RetailCensusError::None) return error;
                    if (worldLocalizeHasInsertAlias)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldLocalizeInsertAliasSlot,
                                    entry.identity));
                            error != RetailCensusError::None) return error;
                    }
                    entry.published = true;
                    entry.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    if (const RetailCensusError error = AppendSemanticTrace(
                            kisak::database::SemanticTraceEventKind::AssetPublish,
                            ASSET_TYPE_LOCALIZE,
                            entry.assetIndex,
                            entry.identity,
                            entry.boundaryInflatedOffset,
                            {0u, entry.headerBlock0Offset,
                                LOCALIZE_ENTRY_BYTES},
                            *entry.storage->name,
                            worldLocalizeAliasSlot);
                        error != RetailCensusError::None) return error;
                }
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount =
                    registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        entry.assetIndex + 1u, stage);
                    error != RetailCensusError::None) return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldWeaponDef)
            {
                const int visit = visitRecord(WEAPON_DEF_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailPublishedWeaponDef &entry = activeWorldWeapon();
                WeaponDef &weapon = *entry.asset;

                auto copyScalarRange = [&](void *target,
                                           std::size_t wireOffset,
                                           std::size_t bytes) noexcept {
                    std::memcpy(target, record + wireOffset, bytes);
                };
                copyScalarRange(weapon.hideTags, 216u, 116u);
                copyScalarRange(&weapon.iReticleCenterSize, 548u, 152u);
                copyScalarRange(&weapon.hudIconRatio, 784u, 4u);
                copyScalarRange(&weapon.ammoCounterIconRatio, 792u, 12u);
                copyScalarRange(&weapon.iAmmoIndex, 808u, 4u);
                copyScalarRange(&weapon.iClipIndex, 816u, 16u);
                copyScalarRange(&weapon.iSharedAmmoCapIndex, 836u, 236u);
                copyScalarRange(&weapon.overlayReticle, 1080u, 224u);
                copyScalarRange(&weapon.killIconRatio, 1308u, 8u);
                copyScalarRange(&weapon.dpadIconRatio, 1320u, 20u);
                copyScalarRange(&weapon.altWeaponIndex, 1344u, 68u);
                copyScalarRange(&weapon.projExplosion, 1416u, 4u);
                copyScalarRange(
                    &weapon.projExplosionEffectForceNormalUp, 1424u, 4u);
                copyScalarRange(&weapon.bProjImpactExplode, 1440u, 264u);
                copyScalarRange(weapon.vProjectileColor, 1708u, 24u);
                copyScalarRange(&weapon.fAdsAimPitch, 1740u, 160u);
                copyScalarRange(weapon.accuracyGraphKnotCount, 1924u, 88u);
                copyScalarRange(&weapon.iUseHintStringIndex, 2020u, 16u);
                copyScalarRange(weapon.fOOPosAnimLength, 2040u, 112u);
                copyScalarRange(&weapon.adsDofStart, 2160u, 8u);

                for (std::size_t index = 0u;
                     index < WEAPON_STRING_OFFSETS.size(); ++index)
                {
                    worldWeaponStringReferences[index] =
                        ReadU32(record + WEAPON_STRING_OFFSETS[index]);
                }
                for (std::size_t index = 0u;
                     index < WEAPON_ACCURACY_KNOT_OFFSETS.size(); ++index)
                {
                    worldWeaponAccuracyReferences[index] =
                        ReadU32(record + WEAPON_ACCURACY_KNOT_OFFSETS[index]);
                }
                for (std::size_t index = 0u;
                     index < WEAPON_XMODEL_OFFSETS.size(); ++index)
                {
                    worldWeaponXModelReferences[index] =
                        ReadU32(record + WEAPON_XMODEL_OFFSETS[index]);
                }
                for (std::size_t index = 0u;
                     index < WEAPON_FX_OFFSETS.size(); ++index)
                {
                    worldWeaponFxReferences[index] =
                        ReadU32(record + WEAPON_FX_OFFSETS[index]);
                }
                for (std::size_t index = 0u;
                     index < WEAPON_MATERIAL_OFFSETS.size(); ++index)
                {
                    worldWeaponMaterialReferences[index] =
                        ReadU32(record + WEAPON_MATERIAL_OFFSETS[index]);
                }
                for (std::size_t index = 0u;
                     index < WEAPON_SOUND_OFFSETS.size(); ++index)
                {
                    worldWeaponSoundReferences[index] =
                        ReadU32(record + WEAPON_SOUND_OFFSETS[index]);
                }
                worldWeaponBounceSoundReference = ReadU32(record + 520u);
                if (worldWeaponStringReferences[0u] == 0u)
                    return RetailCensusError::WeaponNameInvalid;

                for (const std::pair<std::uint32_t, std::uint32_t> range : {
                         std::pair{216u, 8u},
                         std::pair{232u, 16u},
                         std::pair{264u, 16u}})
                {
                    for (std::uint32_t index = 0u; index < range.second; ++index)
                    {
                        if (ReadU16(record + range.first + index * 2u) >=
                            scriptStrings.size())
                        {
                            return RetailCensusError::WeaponScriptStringInvalid;
                        }
                    }
                }
                for (std::size_t graph = 0u; graph < 2u; ++graph)
                {
                    if (weapon.accuracyGraphKnotCount[graph] < 0 ||
                        static_cast<std::uint32_t>(
                            weapon.accuracyGraphKnotCount[graph]) >
                            limits.maxWeaponAccuracyKnots)
                    {
                        return RetailCensusError::WeaponAccuracyInvalid;
                    }
                }

                cursor += WEAPON_DEF_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                worldWeaponOperationIndex = 0u;
                if (const RetailCensusError error =
                        ScheduleWorldWeaponOperation(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldWeaponString)
            {
                RetailPublishedWeaponDef &entry = activeWorldWeapon();
                const WorldWeaponOperation operation =
                    WEAPON_OPERATIONS[worldWeaponOperationIndex];
                if (operation.kind != WorldWeaponOperationKind::String)
                    return RetailCensusError::WeaponLayoutUnsupported;
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxWeaponStringBytes)
                        return RetailCensusError::WeaponStringTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxWeaponStringBytes)
                    return RetailCensusError::WeaponStringTooLong;
                if (recordVisited == 0u)
                {
                    if (bytes > limits.maxWeaponPayloadBytes - entry.payloadBytes ||
                        bytes > limits.maxRetainedWeaponBytes - retainedWeaponBytes)
                    {
                        return RetailCensusError::WeaponPayloadLimit;
                    }
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    entry.stringBlock4Offsets[operation.index] = span.offset;
                    entry.payloadBytes += static_cast<std::uint32_t>(bytes);
                    retainedWeaponBytes += bytes;
                }
                const int visitString = visitRecord(bytes);
                if (visitString <= 0) return RetailCensusError::None;
                try
                {
                    entry.storage->strings[operation.index] =
                        std::make_shared<std::string>(
                            reinterpret_cast<const char *>(
                                inflated.data() + cursor),
                            bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                try
                {
                    block4StringAliases.emplace(
                        entry.stringBlock4Offsets[operation.index],
                        entry.storage->strings[operation.index]);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                AssignWeaponString(
                    *entry.asset,
                    operation.index,
                    entry.storage->strings[operation.index]->c_str());
                cursor += bytes;
                ++report.recordsProcessed;
                ++worldWeaponOperationIndex;
                if (const RetailCensusError error =
                        ScheduleWorldWeaponOperation(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldWeaponSoundNameCell)
            {
                const int visit = visitRecord(4u);
                if (visit <= 0) return RetailCensusError::None;
                worldWeaponSoundStringReference = ReadU32(
                    inflated.data() + cursor);
                cursor += 4u;
                ++report.recordsProcessed;
                if (worldWeaponSoundStringReference == 0u)
                {
                    if (!prerequisiteZone)
                        return RetailCensusError::WeaponSoundNameInvalid;
                    if (worldWeaponSoundIsBounce)
                    {
                        (*activeWorldWeapon().storage->bounceSounds)[
                            worldWeaponSoundIndex] = nullptr;
                        ++worldWeaponBounceIndex;
                    }
                    else
                    {
                        AssignWeaponSound(*activeWorldWeapon().asset,
                            worldWeaponSoundIndex, nullptr);
                        ++worldWeaponOperationIndex;
                    }
                    if (const RetailCensusError error =
                            ScheduleWorldWeaponOperation(stage);
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (worldWeaponSoundStringReference == SHARED_POINTER &&
                    prerequisiteZone)
                {
                    if (worldWeaponSoundIsBounce)
                    {
                        (*activeWorldWeapon().storage->bounceSounds)[
                            worldWeaponSoundIndex] = nullptr;
                        ++worldWeaponBounceIndex;
                    }
                    else
                    {
                        AssignWeaponSound(*activeWorldWeapon().asset,
                            worldWeaponSoundIndex, nullptr);
                        ++worldWeaponOperationIndex;
                    }
                    if (const RetailCensusError error =
                            ScheduleWorldWeaponOperation(stage);
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (worldWeaponSoundStringReference == SHARED_POINTER)
                    return RetailCensusError::WeaponSoundNameInvalid;
                if (worldWeaponSoundStringReference == INLINE_POINTER)
                {
                    stage = RetailCensusStage::WorldWeaponSoundName;
                    continue;
                }
                std::shared_ptr<std::string> name;
                std::uint32_t stringOffset = UINT32_MAX;
                const bool soundStringResolved = ResolvePriorZoneStringPayload(
                    worldWeaponSoundStringReference, name, stringOffset);
                if (!soundStringResolved)
                {
                    if (!prerequisiteZone)
                    {
                        return RetailCensusError::WeaponSoundNameInvalid;
                    }
                    if (worldWeaponSoundIsBounce)
                    {
                        (*activeWorldWeapon().storage->bounceSounds)[
                            worldWeaponSoundIndex] = nullptr;
                        ++worldWeaponBounceIndex;
                    }
                    else
                    {
                        AssignWeaponSound(
                            *activeWorldWeapon().asset,
                            worldWeaponSoundIndex, nullptr);
                        ++worldWeaponOperationIndex;
                    }
                    if (const RetailCensusError error =
                            ScheduleWorldWeaponOperation(stage);
                        error != RetailCensusError::None) return error;
                    continue;
                }
                RetailPublishedWeaponDef &entry = activeWorldWeapon();
                if (worldWeaponSoundIsBounce)
                    entry.bounceSoundNameStringBlock4Offsets[
                        worldWeaponSoundIndex] = stringOffset;
                else
                    entry.soundNameStringBlock4Offsets[
                        worldWeaponSoundIndex] = stringOffset;
                if (const RetailCensusError error =
                        FinishWorldWeaponSoundName(name);
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error =
                        ScheduleWorldWeaponOperation(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldWeaponSoundName)
            {
                RetailPublishedWeaponDef &entry = activeWorldWeapon();
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxWeaponStringBytes)
                        return RetailCensusError::WeaponSoundNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u && !prerequisiteZone)
                    return RetailCensusError::WeaponSoundNameInvalid;
                if (bytes > limits.maxWeaponStringBytes)
                    return RetailCensusError::WeaponSoundNameTooLong;
                if (recordVisited == 0u)
                {
                    if (bytes > limits.maxWeaponPayloadBytes - entry.payloadBytes ||
                        bytes > limits.maxRetainedWeaponBytes - retainedWeaponBytes)
                    {
                        return RetailCensusError::WeaponPayloadLimit;
                    }
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    if (worldWeaponSoundIsBounce)
                        entry.bounceSoundNameStringBlock4Offsets[
                            worldWeaponSoundIndex] = span.offset;
                    else
                        entry.soundNameStringBlock4Offsets[
                            worldWeaponSoundIndex] = span.offset;
                    entry.payloadBytes += static_cast<std::uint32_t>(bytes);
                    retainedWeaponBytes += bytes;
                }
                const int visitString = visitRecord(bytes);
                if (visitString <= 0) return RetailCensusError::None;
                std::shared_ptr<std::string> name;
                try
                {
                    name = std::make_shared<std::string>(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                const std::uint32_t stringOffset = worldWeaponSoundIsBounce
                    ? entry.bounceSoundNameStringBlock4Offsets[
                        worldWeaponSoundIndex]
                    : entry.soundNameStringBlock4Offsets[
                        worldWeaponSoundIndex];
                try
                {
                    block4StringAliases.emplace(stringOffset, name);
                    indirectBlock4StringAliases.emplace(stringOffset, name);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error =
                        FinishWorldWeaponSoundName(name);
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error =
                        ScheduleWorldWeaponOperation(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldWeaponBounceSoundCells)
            {
                constexpr std::size_t bytes = 29u * 4u;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                for (std::size_t index = 0u;
                     index < worldWeaponBounceSoundReferences.size(); ++index)
                {
                    worldWeaponBounceSoundReferences[index] = ReadU32(
                        inflated.data() + cursor + index * 4u);
                }
                try
                {
                    activeWorldWeapon().storage->bounceSounds =
                        std::make_shared<std::vector<snd_alias_list_t *>>(
                            29u, nullptr);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                worldWeaponBounceActive = true;
                worldWeaponBounceIndex = 0u;
                if (const RetailCensusError error =
                        ScheduleWorldWeaponOperation(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldWeaponAccuracyKnots)
            {
                RetailPublishedWeaponDef &entry = activeWorldWeapon();
                WeaponDef &weapon = *entry.asset;
                const WorldWeaponOperation operation =
                    WEAPON_OPERATIONS[worldWeaponOperationIndex];
                if (operation.kind != WorldWeaponOperationKind::AccuracyKnots)
                    return RetailCensusError::WeaponLayoutUnsupported;
                const std::uint32_t graphIndex =
                    operation.index < 2u ? 0u : 1u;
                const std::size_t count = static_cast<std::size_t>(
                    weapon.accuracyGraphKnotCount[graphIndex]);
                const std::size_t bytes = count * 8u;
                const int visitKnots = visitRecord(bytes);
                if (visitKnots <= 0) return RetailCensusError::None;
                try
                {
                    entry.storage->accuracyKnots[operation.index] =
                        std::make_shared<std::vector<std::array<float, 2>>>(
                            std::max<std::size_t>(count, 1u));
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                for (std::size_t index = 0u; index < count; ++index)
                {
                    (*entry.storage->accuracyKnots[operation.index])[index] = {{
                        ReadF32(inflated.data() + cursor + index * 8u),
                        ReadF32(inflated.data() + cursor + index * 8u + 4u),
                    }};
                }
                AssignWeaponAccuracyKnots(
                    weapon,
                    operation.index,
                    reinterpret_cast<float (*)[WEAP_ACCURACY_COUNT]>(
                        entry.storage->accuracyKnots[operation.index]->data()));
                cursor += bytes;
                ++report.recordsProcessed;
                ++worldWeaponOperationIndex;
                if (const RetailCensusError error =
                        ScheduleWorldWeaponOperation(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldWeaponPublish)
            {
                RetailPublishedWeaponDef &entry = activeWorldWeapon();
                if (!entry.pointerAlias && entry.serializedReference != 0u)
                {
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (!prerequisiteZone)
                    {
                        if (!entry.storage || !entry.asset ||
                            !entry.storage->strings[0u] ||
                            !ValidPublishedName(*entry.storage->strings[0u]))
                        {
                            return RetailCensusError::WeaponNameInvalid;
                        }
                        if (const RetailCensusError error = MapRegistryError(
                                registry.RegisterAsset(
                                    ASSET_TYPE_WEAPON,
                                    entry.assetIndex,
                                    *entry.storage->strings[0u],
                                    entry.identity));
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldWeaponAliasSlot, entry.identity));
                            error != RetailCensusError::None) return error;
                        if (worldWeaponHasInsertAlias)
                        {
                            if (const RetailCensusError error = MapRegistryError(
                                    registry.PublishAlias(
                                        worldWeaponInsertAliasSlot,
                                        entry.identity));
                                error != RetailCensusError::None) return error;
                        }
                        entry.published = true;
                        entry.boundaryInflatedOffset =
                            static_cast<std::uint32_t>(cursor);
                        if (const RetailCensusError error = AppendSemanticTrace(
                                kisak::database::SemanticTraceEventKind::AssetPublish,
                                ASSET_TYPE_WEAPON,
                                entry.assetIndex,
                                entry.identity,
                                entry.boundaryInflatedOffset,
                                {0u, entry.headerBlock0Offset, WEAPON_DEF_BYTES},
                                *entry.storage->strings[0u],
                                worldWeaponAliasSlot);
                            error != RetailCensusError::None)
                        {
                            return error;
                        }
                    }
                }
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount =
                    registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        entry.assetIndex + 1u, stage);
                    error != RetailCensusError::None) return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundAliasList)
            {
                const int visit = visitRecord(SOUND_ALIAS_LIST_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailPublishedSoundAliasList &entry = activeWorldSound();
                worldSoundListNameReference = ReadU32(record);
                worldSoundHeadReference = ReadU32(record + 4u);
                const std::int32_t count = ReadS32(record + 8u);
                if (worldSoundListNameReference == 0u || count < 0 ||
                    static_cast<std::uint32_t>(count) >
                        limits.maxSoundAliasesPerList ||
                    ((count == 0) != (worldSoundHeadReference == 0u)))
                {
                    return count < 0 || static_cast<std::uint32_t>(count) >
                            limits.maxSoundAliasesPerList
                        ? RetailCensusError::SoundAliasCountLimit
                        : RetailCensusError::SoundAliasLayoutUnsupported;
                }
                entry.asset->count = count;
                cursor += SOUND_ALIAS_LIST_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                worldSoundStringReference = worldSoundListNameReference;
                worldSoundStringBlock4Offset = UINT32_MAX;
                worldSoundStringTarget = WorldSoundStringTarget::ListName;
                stage = RetailCensusStage::WorldSoundAliasString;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundAliasString)
            {
                const std::uint32_t token = worldSoundStringReference;
                std::shared_ptr<std::string> value;
                std::uint32_t stringOffset = worldSoundStringBlock4Offset;
                if (token == 0u)
                {
                    if (worldSoundStringTarget ==
                        WorldSoundStringTarget::ListName)
                    {
                        return RetailCensusError::SoundAliasStringInvalid;
                    }
                }
                else if (token == INLINE_POINTER)
                {
                    const auto begin = inflated.begin() +
                        static_cast<std::ptrdiff_t>(cursor);
                    const auto terminator = std::find(begin, inflated.end(), 0u);
                    if (terminator == inflated.end())
                    {
                        if (inflated.size() - cursor >=
                            limits.maxSoundStringBytes)
                        {
                            return RetailCensusError::SoundAliasStringTooLong;
                        }
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    const std::size_t bytes =
                        static_cast<std::size_t>(terminator - begin) + 1u;
                    if (bytes <= 1u && worldSoundStringTarget ==
                            WorldSoundStringTarget::ListName)
                    {
                        return RetailCensusError::SoundAliasStringInvalid;
                    }
                    if (bytes > limits.maxSoundStringBytes)
                        return RetailCensusError::SoundAliasStringTooLong;
                    if (recordVisited == 0u)
                    {
                        RetailPublishedSoundAliasList &entry =
                            activeWorldSound();
                        if (bytes > limits.maxRetainedSoundBytes -
                                retainedSoundBytes)
                        {
                            return RetailCensusError::SoundAliasPayloadLimit;
                        }
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(
                                1u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        stringOffset = span.offset;
                        worldSoundStringBlock4Offset = span.offset;
                        entry.payloadBytes += static_cast<std::uint32_t>(bytes);
                        retainedSoundBytes += bytes;
                    }
                    const int visitString = visitRecord(bytes);
                    if (visitString <= 0) return RetailCensusError::None;
                    try
                    {
                        value = std::make_shared<std::string>(
                            reinterpret_cast<const char *>(
                                inflated.data() + cursor),
                            bytes - 1u);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    if (worldSoundStringBlock4Offset != UINT32_MAX)
                    {
                        try
                        {
                            block4StringAliases.emplace(
                                worldSoundStringBlock4Offset, value);
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                else
                {
                    bool resolved = false;
                    ZoneSpan nativeTarget;
                    const bool hasNativeTarget = DecodeZoneAliasToken(
                        token, nativeTarget) && nativeTarget.block == 4u;
                    if (hasNativeTarget &&
                        (worldSoundStringTarget ==
                             WorldSoundStringTarget::ListName ||
                         worldSoundStringTarget ==
                             WorldSoundStringTarget::AliasName))
                    {
                        const auto exact = nativeBlock4SoundListAliases.find(
                            nativeTarget.offset);
                        if (exact != nativeBlock4SoundListAliases.end() &&
                            exact->second.second)
                        {
                            stringOffset = exact->second.first;
                            value = exact->second.second;
                            resolved = true;
                        }
                        else if (worldSoundStringTarget ==
                                     WorldSoundStringTarget::ListName)
                        {
                            const std::pair<std::uint32_t,
                                std::shared_ptr<std::string>> *predecessor =
                                nullptr;
                            std::uint32_t predecessorNativeOffset = 0u;
                            for (const auto &[nativeOffset, local] :
                                 nativeBlock4SoundListAliases)
                            {
                                if (nativeOffset >= nativeTarget.offset ||
                                    !local.second ||
                                    (predecessor != nullptr && nativeOffset <=
                                        predecessorNativeOffset))
                                {
                                    continue;
                                }
                                predecessor = &local;
                                predecessorNativeOffset = nativeOffset;
                            }
                            if (predecessor != nullptr)
                            {
                                auto successor =
                                    indirectBlock4StringAliases.end();
                                std::uint32_t successorOffset = UINT32_MAX;
                                for (auto candidate =
                                         indirectBlock4StringAliases.begin();
                                     candidate !=
                                         indirectBlock4StringAliases.end();
                                     ++candidate)
                                {
                                    if (!candidate->second ||
                                        candidate->first <=
                                            predecessor->first ||
                                        candidate->first >= successorOffset)
                                    {
                                        continue;
                                    }
                                    successor = candidate;
                                    successorOffset = candidate->first;
                                }
                                if (successor !=
                                        indirectBlock4StringAliases.end() &&
                                    successorOffset - predecessor->first <=
                                        FX_ELEM_DEF_BYTES)
                                {
                                    stringOffset = successor->first;
                                    value = successor->second;
                                    resolved = true;
                                }
                            }
                        }
                    }
                    const auto indexedToken = zoneStringTokenAliases.find(token);
                    if (!resolved &&
                        indexedToken != zoneStringTokenAliases.end() &&
                        indexedToken->second)
                    {
                        value = indexedToken->second;
                        ZoneSpan target;
                        if (DecodeZoneAliasToken(token, target))
                            stringOffset = target.offset;
                        resolved = true;
                    }
                    else if (token != SHARED_POINTER)
                    {
                        if (!resolved)
                        {
                            resolved = ResolvePriorZoneStringPayload(
                                token, value, stringOffset);
                        }
                    }
                    if (!resolved && prerequisiteZone &&
                        worldSoundStringTarget ==
                            WorldSoundStringTarget::ListName)
                    {
                        resolved = ResolveOrderedWeaponSoundName(
                            token, value, stringOffset);
                    }
                    if (!resolved && prerequisiteZone &&
                        worldSoundStringTarget ==
                            WorldSoundStringTarget::AliasName &&
                        activeWorldSound().storage->aliasName)
                    {
                        ZoneSpan nativeTarget;
                        if (DecodeZoneAliasToken(token, nativeTarget) &&
                            nativeTarget.block == 4u)
                        {
                            const std::int64_t bias =
                                static_cast<std::int64_t>(
                                    activeWorldSound().nameBlock4Offset) -
                                nativeTarget.offset;
                            if (bias >= std::numeric_limits<std::int32_t>::min() &&
                                bias <= std::numeric_limits<std::int32_t>::max() &&
                                (!block4NativeOffsetBiasKnown ||
                                 block4NativeOffsetBias == bias))
                            {
                                block4NativeOffsetBias =
                                    static_cast<std::int32_t>(bias);
                                block4NativeOffsetBiasKnown = true;
                            }
                        }
                        value = activeWorldSound().storage->aliasName;
                        stringOffset = activeWorldSound().nameBlock4Offset;
                    }
                    else if (!resolved)
                    {
                        // A prerequisite traversal may encounter an XString
                        // owned by a dependency family that is not retained by
                        // this slice. Preserve the canonical parent object and
                        // leave only that unresolved dependency null; list and
                        // alias names remain mandatory publication keys.
                        if (!prerequisiteZone ||
                            worldSoundStringTarget ==
                                WorldSoundStringTarget::ListName ||
                            worldSoundStringTarget ==
                                WorldSoundStringTarget::AliasName)
                        {
                            return RetailCensusError::SoundAliasStringInvalid;
                        }
                    }
                }
                if (token != 0u && token != INLINE_POINTER &&
                    token != SHARED_POINTER && value)
                {
                    try
                    {
                        zoneStringTokenAliases.emplace(token, value);
                        ZoneSpan nativeTarget;
                        if (DecodeZoneAliasToken(token, nativeTarget) &&
                            nativeTarget.block == 4u)
                        {
                            nativeBlock4StringAliases.emplace(
                                nativeTarget.offset,
                                std::make_pair(stringOffset, value));
                            if (worldSoundStringTarget ==
                                WorldSoundStringTarget::ListName)
                            {
                                nativeBlock4SoundListAliases.emplace(
                                    nativeTarget.offset,
                                    std::make_pair(stringOffset, value));
                            }
                        }
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                }
                if (const RetailCensusError error = assignWorldSoundString(
                        worldSoundStringTarget,
                        std::move(value),
                        stringOffset);
                    error != RetailCensusError::None) return error;

                if (worldSoundStringTarget ==
                    WorldSoundStringTarget::ListName)
                {
                    if (worldSoundHeadReference == 0u)
                    {
                        stage = RetailCensusStage::WorldSoundAliasPublish;
                        continue;
                    }
                    if (worldSoundHeadReference != INLINE_POINTER)
                        return RetailCensusError::SoundAliasDependencyUnsupported;
                    const std::uint64_t bytes =
                        static_cast<std::uint64_t>(
                            activeWorldSound().asset->count) *
                        SOUND_ALIAS_BYTES;
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    activeWorldSound().aliasesBlock4Offset = span.offset;
                    stage = RetailCensusStage::WorldSoundAliasHeaders;
                    continue;
                }
                if (worldSoundStringTarget >=
                        WorldSoundStringTarget::AliasName &&
                    worldSoundStringTarget <=
                        WorldSoundStringTarget::ChainAliasName)
                {
                    ++worldSoundComponentIndex;
                }
                else if (worldSoundStringTarget ==
                    WorldSoundStringTarget::FileDirectory)
                {
                    worldSoundStringTarget = WorldSoundStringTarget::FileName;
                    worldSoundStringReference = worldSoundFileNameReference;
                    worldSoundStringBlock4Offset = UINT32_MAX;
                    continue;
                }
                else if (worldSoundStringTarget ==
                    WorldSoundStringTarget::FileName)
                {
                    ++worldSoundComponentIndex;
                }
                else if (worldSoundStringTarget ==
                    WorldSoundStringTarget::CurveName)
                {
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    CanonicalSoundAliasListStorage &storage =
                        *activeWorldSound().storage;
                    (*storage.aliases)[worldSoundAliasIndex]
                        .volumeFalloffCurve =
                        storage.curves[worldSoundAliasIndex].get();
                    ++worldSoundComponentIndex;
                }
                else
                {
                    ++worldSoundComponentIndex;
                }
                if (const RetailCensusError error =
                        scheduleWorldSoundComponent(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundAliasHeaders)
            {
                RetailPublishedSoundAliasList &entry = activeWorldSound();
                const std::size_t count =
                    static_cast<std::size_t>(entry.asset->count);
                const std::size_t bytes = count * SOUND_ALIAS_BYTES;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    worldSoundAliases.resize(count);
                    entry.storage->aliases =
                        std::make_shared<std::vector<snd_alias_t>>(count);
                    entry.storage->aliasStrings.resize(count);
                    entry.storage->fileStrings.resize(count);
                    entry.storage->soundFiles.resize(count);
                    entry.storage->soundFileBlock4Offsets.assign(
                        count, UINT32_MAX);
                    entry.storage->loadedSounds.resize(count);
                    entry.storage->loadedSoundNames.resize(count);
                    entry.storage->curves.resize(count);
                    entry.storage->curveNames.resize(count);
                    entry.storage->speakerMaps.resize(count);
                    entry.storage->speakerMapNames.resize(count);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                for (std::size_t index = 0u; index < count; ++index)
                {
                    const std::uint8_t *record = inflated.data() + cursor +
                        index * SOUND_ALIAS_BYTES;
                    WorldSoundAliasState &state = worldSoundAliases[index];
                    for (std::size_t string = 0u; string < 4u; ++string)
                        state.stringReferences[string] =
                            ReadU32(record + string * 4u);
                    state.soundFileReference = ReadU32(record + 16u);
                    state.curveReference = ReadU32(record + 72u);
                    state.speakerMapReference = ReadU32(record + 88u);
                    snd_alias_t &alias = (*entry.storage->aliases)[index];
                    std::memcpy(&alias, record, SOUND_ALIAS_BYTES);
                    alias.aliasName = nullptr;
                    alias.subtitle = nullptr;
                    alias.secondaryAliasName = nullptr;
                    alias.chainAliasName = nullptr;
                    alias.soundFile = nullptr;
                    alias.volumeFalloffCurve = nullptr;
                    alias.speakerMap = nullptr;
                }
                cursor += bytes;
                ++report.recordsProcessed;
                worldSoundAliasIndex = 0u;
                worldSoundComponentIndex = 0u;
                if (const RetailCensusError error =
                        scheduleWorldSoundComponent(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundAliasFile)
            {
                const int visit = visitRecord(SOUND_FILE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                if (record[0u] > 2u || record[1u] > 1u)
                    return RetailCensusError::SoundAliasLayoutUnsupported;
                CanonicalSoundAliasListStorage &storage =
                    *activeWorldSound().storage;
                try
                {
                    storage.soundFiles[worldSoundAliasIndex] =
                        std::make_shared<SoundFile>();
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                SoundFile &file = *storage.soundFiles[worldSoundAliasIndex];
                std::memcpy(&file, record, SOUND_FILE_BYTES);
                (*storage.aliases)[worldSoundAliasIndex].soundFile = &file;
                cursor += SOUND_FILE_BYTES;
                ++report.recordsProcessed;
                if (file.type != 1u)
                {
                    worldSoundFileDirectoryReference = ReadU32(record + 4u);
                    worldSoundFileNameReference = ReadU32(record + 8u);
                    file.u.streamSnd.filename.info.raw = {};
                    worldSoundStringTarget =
                        WorldSoundStringTarget::FileDirectory;
                    worldSoundStringReference =
                        worldSoundFileDirectoryReference;
                    worldSoundStringBlock4Offset = UINT32_MAX;
                    stage = RetailCensusStage::WorldSoundAliasString;
                    continue;
                }
                const std::uint32_t loadedReference = ReadU32(record + 4u);
                file.u.loadSnd = nullptr;
                if (loadedReference == INLINE_POINTER ||
                    loadedReference == SHARED_POINTER)
                {
                    if (const RetailCensusError error = Push(0u);
                        error != RetailCensusError::None) return error;
                    if (loadedReference == SHARED_POINTER)
                    {
                        if (const RetailCensusError error = Push(4u);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = Plan(4u, 4u);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = Pop();
                            error != RetailCensusError::None) return error;
                    }
                    if (const RetailCensusError error = Plan(4u, 44u);
                        error != RetailCensusError::None) return error;
                    stage = RetailCensusStage::WorldSoundLoadedSound;
                    continue;
                }
                ++worldSoundComponentIndex;
                if (const RetailCensusError error =
                        scheduleWorldSoundComponent(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundLoadedSound)
            {
                const int visit = visitRecord(44u);
                if (visit <= 0) return RetailCensusError::None;
                CanonicalSoundAliasListStorage &storage =
                    *activeWorldSound().storage;
                try
                {
                    storage.loadedSounds[worldSoundAliasIndex] =
                        std::make_shared<LoadedSound>();
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                LoadedSound &loaded =
                    *storage.loadedSounds[worldSoundAliasIndex];
                std::memcpy(&loaded, inflated.data() + cursor, 44u);
                worldSoundLoadedNameReference = ReadU32(
                    inflated.data() + cursor);
                worldSoundLoadedDataReference = ReadU32(
                    inflated.data() + cursor + 40u);
                worldSoundLoadedDataBytes = ReadU32(
                    inflated.data() + cursor + 12u);
                loaded.name = nullptr;
                loaded.sound.info.data_ptr = nullptr;
                loaded.sound.info.initial_ptr = nullptr;
                loaded.sound.data = nullptr;
                cursor += 44u;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldSoundLoadedSoundName;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundLoadedSoundName)
            {
                CanonicalSoundAliasListStorage &storage =
                    *activeWorldSound().storage;
                std::shared_ptr<std::string> name;
                if (worldSoundLoadedNameReference == INLINE_POINTER)
                {
                    const auto begin = inflated.begin() +
                        static_cast<std::ptrdiff_t>(cursor);
                    const auto terminator = std::find(begin, inflated.end(), 0u);
                    if (terminator == inflated.end())
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    const std::size_t bytes =
                        static_cast<std::size_t>(terminator - begin) + 1u;
                    if (bytes > limits.maxSoundStringBytes)
                        return RetailCensusError::SoundAliasStringTooLong;
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(1u, bytes);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    try
                    {
                        name = std::make_shared<std::string>(
                            reinterpret_cast<const char *>(
                                inflated.data() + cursor), bytes - 1u);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                else
                {
                    std::uint32_t offset = UINT32_MAX;
                    (void)ResolvePriorZoneStringPayload(
                        worldSoundLoadedNameReference, name, offset);
                }
                storage.loadedSoundNames[worldSoundAliasIndex] = name;
                if (name)
                    storage.loadedSounds[worldSoundAliasIndex]->name =
                        name->c_str();
                if (const RetailCensusError error = Push(0u);
                    error != RetailCensusError::None) return error;
                if (worldSoundLoadedDataReference == INLINE_POINTER ||
                    worldSoundLoadedDataReference == SHARED_POINTER)
                {
                    if (worldSoundLoadedDataReference == SHARED_POINTER)
                    {
                        if (const RetailCensusError error = Push(4u);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = Plan(4u, 4u);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = Pop();
                            error != RetailCensusError::None) return error;
                    }
                    if (const RetailCensusError error = Plan(
                            1u, worldSoundLoadedDataBytes);
                        error != RetailCensusError::None) return error;
                }
                stage = RetailCensusStage::WorldSoundLoadedSoundData;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundLoadedSoundData)
            {
                if (worldSoundLoadedDataReference == INLINE_POINTER ||
                    worldSoundLoadedDataReference == SHARED_POINTER)
                {
                    const int visit = visitRecord(worldSoundLoadedDataBytes);
                    if (visit <= 0) return RetailCensusError::None;
                    cursor += worldSoundLoadedDataBytes;
                    ++report.recordsProcessed;
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                CanonicalSoundAliasListStorage &storage =
                    *activeWorldSound().storage;
                SoundFile &file =
                    *storage.soundFiles[worldSoundAliasIndex];
                file.u.loadSnd =
                    storage.loadedSounds[worldSoundAliasIndex].get();
                ++worldSoundComponentIndex;
                if (const RetailCensusError error =
                        scheduleWorldSoundComponent(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundAliasCurve)
            {
                const int visit = visitRecord(SOUND_CURVE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                CanonicalSoundAliasListStorage &storage =
                    *activeWorldSound().storage;
                try
                {
                    storage.curves[worldSoundAliasIndex] =
                        std::make_shared<SndCurve>();
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                SndCurve &curve = *storage.curves[worldSoundAliasIndex];
                std::memcpy(&curve, inflated.data() + cursor,
                    SOUND_CURVE_BYTES);
                worldSoundCurveNameReference = ReadU32(inflated.data() + cursor);
                curve.filename = nullptr;
                if (curve.knotCount < 0 || curve.knotCount > 8)
                    return RetailCensusError::SoundAliasLayoutUnsupported;
                cursor += SOUND_CURVE_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                worldSoundStringTarget = WorldSoundStringTarget::CurveName;
                worldSoundStringReference = worldSoundCurveNameReference;
                worldSoundStringBlock4Offset = UINT32_MAX;
                stage = RetailCensusStage::WorldSoundAliasString;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundAliasSpeakerMap)
            {
                const int visit = visitRecord(SPEAKER_MAP_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                CanonicalSoundAliasListStorage &storage =
                    *activeWorldSound().storage;
                try
                {
                    storage.speakerMaps[worldSoundAliasIndex] =
                        std::make_shared<SpeakerMap>();
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                SpeakerMap &map = *storage.speakerMaps[worldSoundAliasIndex];
                std::memcpy(&map, inflated.data() + cursor,
                    SPEAKER_MAP_BYTES);
                worldSoundSpeakerNameReference = ReadU32(
                    inflated.data() + cursor + 4u);
                map.name = nullptr;
                (*storage.aliases)[worldSoundAliasIndex].speakerMap = &map;
                cursor += SPEAKER_MAP_BYTES;
                ++report.recordsProcessed;
                worldSoundStringTarget =
                    WorldSoundStringTarget::SpeakerMapName;
                worldSoundStringReference = worldSoundSpeakerNameReference;
                worldSoundStringBlock4Offset = UINT32_MAX;
                stage = RetailCensusStage::WorldSoundAliasString;
                continue;
            }
            if (stage == RetailCensusStage::WorldSoundAliasPublish)
            {
                RetailPublishedSoundAliasList &entry = activeWorldSound();
                if (!entry.pointerAlias)
                {
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (!entry.storage || !entry.storage->aliasName ||
                        !ValidPublishedName(*entry.storage->aliasName))
                    {
                        return RetailCensusError::SoundAliasStringInvalid;
                    }
                    if (entry.asset && entry.asset->count > 0 &&
                        entry.storage->aliases &&
                        !entry.storage->aliases->empty() &&
                        entry.storage->aliasStrings.size() ==
                            entry.storage->aliases->size() &&
                        entry.storage->aliasStrings[0u][0u] &&
                        ValidPublishedName(
                            *entry.storage->aliasStrings[0u][0u]) &&
                        !DatabaseNamesEqual(
                            *entry.storage->aliasName,
                            *entry.storage->aliasStrings[0u][0u]))
                    {
                        // The list and every child alias use the same DB key.
                        // Prefer the child XString when temporary prerequisite
                        // offset accounting made the list-header token land on
                        // an adjacent already-patched string cell.
                        entry.storage->aliasName =
                            entry.storage->aliasStrings[0u][0u];
                        entry.asset->aliasName =
                            entry.storage->aliasName->c_str();
                    }
                    const auto prior = std::find_if(
                        result.worldSoundAliasLists.begin(),
                        result.worldSoundAliasLists.begin() +
                            static_cast<std::ptrdiff_t>(worldSoundIndex),
                        [&](const RetailPublishedSoundAliasList &candidate) {
                            return candidate.published && candidate.asset &&
                                candidate.storage &&
                                candidate.storage->aliasName &&
                                DatabaseNamesEqual(
                                    *candidate.storage->aliasName,
                                    *entry.storage->aliasName);
                        });
                    if (prior != result.worldSoundAliasLists.begin() +
                            static_cast<std::ptrdiff_t>(worldSoundIndex))
                    {
                        // Load_snd_alias_list_Asset writes the pointer returned
                        // by DB_AddXAsset back into the serialized handle. A
                        // repeated name therefore converges on the existing
                        // zone-owned canonical object instead of creating a
                        // second lookup-database entry.
                        entry.identity = prior->identity;
                        entry.storage = prior->storage;
                        entry.asset = prior->asset;
                        entry.databaseAlias = true;
                    }
                    else if (const RetailCensusError error = MapRegistryError(
                                 registry.RegisterAsset(
                                     ASSET_TYPE_SOUND,
                                     entry.assetIndex,
                                     *entry.storage->aliasName,
                                     entry.identity));
                             error != RetailCensusError::None)
                    {
                        return error;
                    }
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                worldSoundAliasSlot, entry.identity));
                        error != RetailCensusError::None) return error;
                    if (worldSoundHasInsertAlias)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldSoundInsertAliasSlot,
                                    entry.identity));
                            error != RetailCensusError::None) return error;
                    }
                    entry.published = true;
                }
                entry.boundaryInflatedOffset =
                    static_cast<std::uint32_t>(cursor);
                ++result.completedAssetCount;
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        entry.assetIndex + 1u, stage);
                    error != RetailCensusError::None) return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldClipMap)
            {
                if (clipMapLoader.Progress() ==
                    RetailClipMapLoadProgress::Running)
                {
                    if (const RetailCensusError error = clipMapLoader.Step(*this);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    if (clipMapLoader.Progress() ==
                        RetailClipMapLoadProgress::Running)
                    {
                        return RetailCensusError::None;
                    }
                }
                if (result.worldClipMaps.empty())
                    return RetailCensusError::ClipMapLayoutUnsupported;
                const RetailPublishedClipMap &entry = result.worldClipMaps.back();
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount = registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        entry.assetIndex + 1u, stage);
                    error != RetailCensusError::None)
                    return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldComWorld)
            {
                if (comWorldLoader.Progress() ==
                    RetailComWorldLoadProgress::Running)
                {
                    if (const RetailCensusError error = comWorldLoader.Step(*this);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    if (comWorldLoader.Progress() ==
                        RetailComWorldLoadProgress::Running)
                    {
                        return RetailCensusError::None;
                    }
                }
                if (result.worldComWorlds.empty())
                    return RetailCensusError::ComWorldLayoutUnsupported;
                const RetailPublishedComWorld &entry =
                    result.worldComWorlds.back();
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount = registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        entry.assetIndex + 1u, stage);
                    error != RetailCensusError::None)
                    return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldLightDef)
            {
                if (lightDefLoader.Progress() ==
                    RetailLightDefLoadProgress::Running)
                {
                    if (const RetailCensusError error =
                            lightDefLoader.Step(*this);
                        error != RetailCensusError::None)
                        return error;
                    if (lightDefLoader.Progress() ==
                        RetailLightDefLoadProgress::Running)
                        return RetailCensusError::None;
                }
                if (result.worldLightDefs.empty())
                    return RetailCensusError::LightDefLayoutUnsupported;
                const RetailPublishedLightDef &entry =
                    result.worldLightDefs.back();
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount =
                    registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount =
                    registry.DefinedAliasCount();
                if (const RetailCensusError error =
                        dispatchSupportedWorldAsset(
                            entry.assetIndex + 1u, stage);
                    error != RetailCensusError::None)
                    return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldXAnimParts)
            {
                const int visit = visitRecord(XANIM_PARTS_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailPublishedXAnimParts &entry = activeWorldXAnim();
                XAnimParts &parts = *entry.asset;
                worldXAnimNameReference = ReadU32(record);
                parts.dataByteCount = ReadU16(record + 4u);
                parts.dataShortCount = ReadU16(record + 6u);
                parts.dataIntCount = ReadU16(record + 8u);
                parts.randomDataByteCount = ReadU16(record + 10u);
                parts.randomDataIntCount = ReadU16(record + 12u);
                parts.numframes = ReadU16(record + 14u);
                if (record[16u] > 1u || record[17u] > 1u ||
                    record[30u] > 1u)
                {
                    return RetailCensusError::XAnimLayoutUnsupported;
                }
                parts.bLoop = record[16u] != 0u;
                parts.bDelta = record[17u] != 0u;
                std::copy_n(record + 18u, 10u, parts.boneCount);
                parts.notifyCount = record[28u];
                parts.assetType = record[29u];
                parts.isDefault = record[30u] != 0u;
                parts.padding = record[31u];
                parts.randomDataShortCount = ReadU32(record + 32u);
                parts.indexCount = ReadU32(record + 36u);
                parts.framerate = ReadF32(record + 40u);
                parts.frequency = ReadF32(record + 44u);
                worldXAnimNamesReference = ReadU32(record + 48u);
                worldXAnimDataByteReference = ReadU32(record + 52u);
                worldXAnimDataShortReference = ReadU32(record + 56u);
                worldXAnimDataIntReference = ReadU32(record + 60u);
                worldXAnimRandomShortReference = ReadU32(record + 64u);
                worldXAnimRandomByteReference = ReadU32(record + 68u);
                worldXAnimRandomIntReference = ReadU32(record + 72u);
                worldXAnimIndicesReference = ReadU32(record + 76u);
                worldXAnimNotifyReference = ReadU32(record + 80u);
                worldXAnimDeltaReference = ReadU32(record + 84u);
                if (worldXAnimNameReference == 0u)
                    return RetailCensusError::XAnimNameInvalid;
                cursor += XANIM_PARTS_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXAnimName;
                continue;
            }
            if (stage == RetailCensusStage::WorldXAnimName)
            {
                RetailPublishedXAnimParts &entry = activeWorldXAnim();
                if (worldXAnimNameReference != INLINE_POINTER)
                {
                    ZoneSpan nameSpan;
                    if (!DecodeZoneAliasToken(worldXAnimNameReference, nameSpan) ||
                        nameSpan.block != 4u)
                    {
                        return RetailCensusError::XAnimNameInvalid;
                    }
                    const auto prior = std::find_if(
                        result.worldXAnimParts.begin(),
                        result.worldXAnimParts.begin() +
                            static_cast<std::ptrdiff_t>(worldXAnimIndex),
                        [&nameSpan](const RetailPublishedXAnimParts &candidate) {
                            return candidate.published && candidate.storage &&
                                candidate.storage->name &&
                                candidate.nameBlock4Offset == nameSpan.offset;
                        });
                    if (prior == result.worldXAnimParts.begin() +
                            static_cast<std::ptrdiff_t>(worldXAnimIndex))
                    {
                        return RetailCensusError::XAnimNameInvalid;
                    }
                    entry.storage->name = prior->storage->name;
                    entry.nameBlock4Offset = nameSpan.offset;
                    entry.asset->name = entry.storage->name->c_str();
                    worldXAnimPhase = WorldXAnimPhase::BoneNames;
                    if (const RetailCensusError error =
                            ScheduleWorldXAnimPayload(stage);
                        error != RetailCensusError::None) return error;
                    continue;
                }
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxXAnimNameBytes)
                        return RetailCensusError::XAnimNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::XAnimNameInvalid;
                if (bytes > limits.maxXAnimNameBytes)
                    return RetailCensusError::XAnimNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    entry.nameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    entry.storage->name = std::make_shared<std::string>(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(*entry.storage->name))
                    return RetailCensusError::XAnimNameInvalid;
                try
                {
                    block4StringAliases.emplace(
                        entry.nameBlock4Offset, entry.storage->name);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                entry.asset->name = entry.storage->name->c_str();
                cursor += bytes;
                ++report.recordsProcessed;
                worldXAnimPhase = WorldXAnimPhase::BoneNames;
                if (const RetailCensusError error =
                        ScheduleWorldXAnimPayload(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldXAnimPayload)
            {
                RetailPublishedXAnimParts &entry = activeWorldXAnim();
                CanonicalXAnimPartsStorage &storage = *entry.storage;
                XAnimParts &parts = *entry.asset;
                std::size_t bytes = 0u;
                switch (worldXAnimPhase)
                {
                case WorldXAnimPhase::BoneNames:
                    bytes = static_cast<std::size_t>(parts.boneCount[9]) * 2u;
                    break;
                case WorldXAnimPhase::Notify:
                    bytes = static_cast<std::size_t>(parts.notifyCount) *
                        XANIM_NOTIFY_BYTES;
                    break;
                case WorldXAnimPhase::DeltaPart:
                    bytes = XANIM_DELTA_PART_BYTES;
                    break;
                case WorldXAnimPhase::DeltaTransHeader:
                    bytes = XANIM_DELTA_TRANS_HEADER_BYTES;
                    break;
                case WorldXAnimPhase::DeltaTransFrames:
                    bytes = worldXAnimDeltaTransSize == 0u
                        ? 12u : XANIM_DELTA_TRANS_FRAMES_BYTES;
                    break;
                case WorldXAnimPhase::DeltaTransIndices:
                    bytes = (static_cast<std::size_t>(worldXAnimDeltaTransSize) + 1u) *
                        (parts.numframes >= 0x100u ? 2u : 1u);
                    break;
                case WorldXAnimPhase::DeltaTransData:
                    bytes = (static_cast<std::size_t>(worldXAnimDeltaTransSize) + 1u) *
                        (worldXAnimDeltaTransSmall ? 3u : 6u);
                    break;
                case WorldXAnimPhase::DeltaQuatHeader:
                    bytes = XANIM_DELTA_QUAT_HEADER_BYTES;
                    break;
                case WorldXAnimPhase::DeltaQuatFrames:
                    bytes = worldXAnimDeltaQuatSize == 0u
                        ? 4u : XANIM_DELTA_QUAT_FRAMES_BYTES;
                    break;
                case WorldXAnimPhase::DeltaQuatIndices:
                    bytes = (static_cast<std::size_t>(worldXAnimDeltaQuatSize) + 1u) *
                        (parts.numframes >= 0x100u ? 2u : 1u);
                    break;
                case WorldXAnimPhase::DeltaQuatData:
                    bytes = (static_cast<std::size_t>(worldXAnimDeltaQuatSize) + 1u) * 4u;
                    break;
                case WorldXAnimPhase::DataByte: bytes = parts.dataByteCount; break;
                case WorldXAnimPhase::DataShort:
                    bytes = static_cast<std::size_t>(parts.dataShortCount) * 2u;
                    break;
                case WorldXAnimPhase::DataInt:
                    bytes = static_cast<std::size_t>(parts.dataIntCount) * 4u;
                    break;
                case WorldXAnimPhase::RandomDataShort:
                    bytes = static_cast<std::size_t>(parts.randomDataShortCount) * 2u;
                    break;
                case WorldXAnimPhase::RandomDataByte:
                    bytes = parts.randomDataByteCount;
                    break;
                case WorldXAnimPhase::RandomDataInt:
                    bytes = static_cast<std::size_t>(parts.randomDataIntCount) * 4u;
                    break;
                case WorldXAnimPhase::Indices:
                    bytes = static_cast<std::size_t>(parts.indexCount) *
                        (parts.numframes >= 0x100u ? 2u : 1u);
                    break;
                case WorldXAnimPhase::Complete:
                    return RetailCensusError::XAnimLayoutUnsupported;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                auto allocateVector = []<typename T>(
                    std::shared_ptr<std::vector<T>> &target,
                    std::size_t count) noexcept -> bool {
                    try
                    {
                        target = std::make_shared<std::vector<T>>(
                            std::max<std::size_t>(count, 1u));
                    }
                    catch (...) { return false; }
                    return true;
                };
                switch (worldXAnimPhase)
                {
                case WorldXAnimPhase::BoneNames:
                {
                    const std::size_t count = parts.boneCount[9];
                    if (!allocateVector(storage.names, count))
                        return RetailCensusError::AllocationFailed;
                    for (std::size_t index = 0u; index < count; ++index)
                    {
                        const std::uint16_t value = ReadU16(record + index * 2u);
                        if (value >= scriptStrings.size())
                            return RetailCensusError::XAnimScriptStringInvalid;
                        (*storage.names)[index] = value;
                    }
                    parts.names = storage.names->data();
                    worldXAnimPhase = WorldXAnimPhase::Notify;
                    break;
                }
                case WorldXAnimPhase::Notify:
                {
                    const std::size_t count = parts.notifyCount;
                    if (!allocateVector(storage.notify, count))
                        return RetailCensusError::AllocationFailed;
                    for (std::size_t index = 0u; index < count; ++index)
                    {
                        const std::uint8_t *item = record + index * XANIM_NOTIFY_BYTES;
                        const std::uint16_t name = ReadU16(item);
                        if (name >= scriptStrings.size())
                            return RetailCensusError::XAnimScriptStringInvalid;
                        (*storage.notify)[index] = {
                            name, ReadU16(item + 2u), ReadF32(item + 4u)};
                    }
                    parts.notify = storage.notify->data();
                    worldXAnimPhase = WorldXAnimPhase::DeltaPart;
                    break;
                }
                case WorldXAnimPhase::DeltaPart:
                    try { storage.deltaPart = std::make_shared<XAnimDeltaPart>(); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    *storage.deltaPart = {};
                    parts.deltaPart = storage.deltaPart.get();
                    worldXAnimDeltaTransReference = ReadU32(record);
                    worldXAnimDeltaQuatReference = ReadU32(record + 4u);
                    worldXAnimPhase = WorldXAnimPhase::DeltaTransHeader;
                    break;
                case WorldXAnimPhase::DeltaTransHeader:
                {
                    worldXAnimDeltaTransSize = ReadU16(record);
                    if (record[2u] > 1u)
                        return RetailCensusError::XAnimDeltaInvalid;
                    worldXAnimDeltaTransSmall = record[2u] != 0u;
                    const std::size_t indexBytes = worldXAnimDeltaTransSize == 0u
                        ? 0u
                        : (static_cast<std::size_t>(worldXAnimDeltaTransSize) + 1u) *
                            (parts.numframes >= 0x100u ? 2u : 1u);
                    storage.deltaTrans = AllocateFlexibleObject<XAnimPartTrans>(
                        sizeof(XAnimPartTrans) + indexBytes);
                    if (!storage.deltaTrans)
                        return RetailCensusError::AllocationFailed;
                    storage.deltaTrans->size = worldXAnimDeltaTransSize;
                    storage.deltaTrans->smallTrans = record[2u];
                    storage.deltaTrans->padding = record[3u];
                    storage.deltaPart->trans = storage.deltaTrans.get();
                    worldXAnimPhase = WorldXAnimPhase::DeltaTransFrames;
                    break;
                }
                case WorldXAnimPhase::DeltaTransFrames:
                    if (worldXAnimDeltaTransSize == 0u)
                    {
                        for (std::size_t index = 0u; index < 3u; ++index)
                            storage.deltaTrans->u.frame0[index] =
                                ReadF32(record + index * 4u);
                        worldXAnimPhase = WorldXAnimPhase::DeltaQuatHeader;
                    }
                    else
                    {
                        for (std::size_t index = 0u; index < 3u; ++index)
                        {
                            storage.deltaTrans->u.frames.mins[index] =
                                ReadF32(record + index * 4u);
                            storage.deltaTrans->u.frames.size[index] =
                                ReadF32(record + 12u + index * 4u);
                        }
                        worldXAnimDeltaTransFramesReference = ReadU32(record + 24u);
                        worldXAnimPhase = WorldXAnimPhase::DeltaTransIndices;
                    }
                    break;
                case WorldXAnimPhase::DeltaTransIndices:
                {
                    const std::size_t count =
                        static_cast<std::size_t>(worldXAnimDeltaTransSize) + 1u;
                    if (parts.numframes >= 0x100u)
                    {
                        auto *target = reinterpret_cast<std::uint16_t *>(
                            &storage.deltaTrans->u.frames.indices);
                        for (std::size_t index = 0u; index < count; ++index)
                            target[index] = ReadU16(record + index * 2u);
                    }
                    else
                    {
                        std::memcpy(
                            &storage.deltaTrans->u.frames.indices,
                            record, count);
                    }
                    worldXAnimPhase = WorldXAnimPhase::DeltaTransData;
                    break;
                }
                case WorldXAnimPhase::DeltaTransData:
                {
                    const std::size_t count =
                        static_cast<std::size_t>(worldXAnimDeltaTransSize) + 1u;
                    if (worldXAnimDeltaTransSmall)
                    {
                        if (!allocateVector(storage.deltaTransByteFrames, count * 3u))
                            return RetailCensusError::AllocationFailed;
                        std::memcpy(storage.deltaTransByteFrames->data(), record, bytes);
                        storage.deltaTrans->u.frames.frames._1 =
                            reinterpret_cast<std::uint8_t (*)[3]>(
                                storage.deltaTransByteFrames->data());
                    }
                    else
                    {
                        if (!allocateVector(storage.deltaTransShortFrames, count * 3u))
                            return RetailCensusError::AllocationFailed;
                        for (std::size_t index = 0u; index < count * 3u; ++index)
                            (*storage.deltaTransShortFrames)[index] =
                                ReadU16(record + index * 2u);
                        storage.deltaTrans->u.frames.frames._2 =
                            reinterpret_cast<std::uint16_t (*)[3]>(
                                storage.deltaTransShortFrames->data());
                    }
                    worldXAnimPhase = WorldXAnimPhase::DeltaQuatHeader;
                    break;
                }
                case WorldXAnimPhase::DeltaQuatHeader:
                {
                    worldXAnimDeltaQuatSize = ReadU16(record);
                    const std::size_t indexBytes = worldXAnimDeltaQuatSize == 0u
                        ? 0u
                        : (static_cast<std::size_t>(worldXAnimDeltaQuatSize) + 1u) *
                            (parts.numframes >= 0x100u ? 2u : 1u);
                    storage.deltaQuat = AllocateFlexibleObject<XAnimDeltaPartQuat>(
                        sizeof(XAnimDeltaPartQuat) + indexBytes);
                    if (!storage.deltaQuat)
                        return RetailCensusError::AllocationFailed;
                    storage.deltaQuat->size = worldXAnimDeltaQuatSize;
                    storage.deltaQuat->padding = ReadU16(record + 2u);
                    storage.deltaPart->quat = storage.deltaQuat.get();
                    worldXAnimPhase = WorldXAnimPhase::DeltaQuatFrames;
                    break;
                }
                case WorldXAnimPhase::DeltaQuatFrames:
                    if (worldXAnimDeltaQuatSize == 0u)
                    {
                        storage.deltaQuat->u.frame0[0] = ReadS16(record);
                        storage.deltaQuat->u.frame0[1] = ReadS16(record + 2u);
                        worldXAnimPhase = WorldXAnimPhase::DataByte;
                    }
                    else
                    {
                        worldXAnimDeltaQuatFramesReference = ReadU32(record);
                        worldXAnimPhase = WorldXAnimPhase::DeltaQuatIndices;
                    }
                    break;
                case WorldXAnimPhase::DeltaQuatIndices:
                {
                    const std::size_t count =
                        static_cast<std::size_t>(worldXAnimDeltaQuatSize) + 1u;
                    if (parts.numframes >= 0x100u)
                    {
                        auto *target = reinterpret_cast<std::uint16_t *>(
                            &storage.deltaQuat->u.frames.indices);
                        for (std::size_t index = 0u; index < count; ++index)
                            target[index] = ReadU16(record + index * 2u);
                    }
                    else
                    {
                        std::memcpy(
                            &storage.deltaQuat->u.frames.indices,
                            record, count);
                    }
                    worldXAnimPhase = WorldXAnimPhase::DeltaQuatData;
                    break;
                }
                case WorldXAnimPhase::DeltaQuatData:
                {
                    const std::size_t count =
                        static_cast<std::size_t>(worldXAnimDeltaQuatSize) + 1u;
                    if (!allocateVector(storage.deltaQuatFrames, count * 2u))
                        return RetailCensusError::AllocationFailed;
                    for (std::size_t index = 0u; index < count * 2u; ++index)
                        (*storage.deltaQuatFrames)[index] =
                            ReadS16(record + index * 2u);
                    storage.deltaQuat->u.frames.frames =
                        reinterpret_cast<std::int16_t (*)[2]>(
                            storage.deltaQuatFrames->data());
                    worldXAnimPhase = WorldXAnimPhase::DataByte;
                    break;
                }
                case WorldXAnimPhase::DataByte:
                    if (!allocateVector(storage.dataByte, parts.dataByteCount))
                        return RetailCensusError::AllocationFailed;
                    std::memcpy(storage.dataByte->data(), record, bytes);
                    parts.dataByte = storage.dataByte->data();
                    worldXAnimPhase = WorldXAnimPhase::DataShort;
                    break;
                case WorldXAnimPhase::DataShort:
                    if (!allocateVector(storage.dataShort, parts.dataShortCount))
                        return RetailCensusError::AllocationFailed;
                    for (std::size_t index = 0u; index < parts.dataShortCount; ++index)
                        (*storage.dataShort)[index] = ReadS16(record + index * 2u);
                    parts.dataShort = storage.dataShort->data();
                    worldXAnimPhase = WorldXAnimPhase::DataInt;
                    break;
                case WorldXAnimPhase::DataInt:
                    if (!allocateVector(storage.dataInt, parts.dataIntCount))
                        return RetailCensusError::AllocationFailed;
                    for (std::size_t index = 0u; index < parts.dataIntCount; ++index)
                        (*storage.dataInt)[index] = ReadS32(record + index * 4u);
                    parts.dataInt = storage.dataInt->data();
                    worldXAnimPhase = WorldXAnimPhase::RandomDataShort;
                    break;
                case WorldXAnimPhase::RandomDataShort:
                    if (!allocateVector(storage.randomDataShort,
                            parts.randomDataShortCount))
                        return RetailCensusError::AllocationFailed;
                    for (std::size_t index = 0u;
                         index < parts.randomDataShortCount; ++index)
                    {
                        (*storage.randomDataShort)[index] =
                            ReadS16(record + index * 2u);
                    }
                    parts.randomDataShort = storage.randomDataShort->data();
                    worldXAnimPhase = WorldXAnimPhase::RandomDataByte;
                    break;
                case WorldXAnimPhase::RandomDataByte:
                    if (!allocateVector(storage.randomDataByte,
                            parts.randomDataByteCount))
                        return RetailCensusError::AllocationFailed;
                    std::memcpy(storage.randomDataByte->data(), record, bytes);
                    parts.randomDataByte = storage.randomDataByte->data();
                    worldXAnimPhase = WorldXAnimPhase::RandomDataInt;
                    break;
                case WorldXAnimPhase::RandomDataInt:
                    if (!allocateVector(storage.randomDataInt,
                            parts.randomDataIntCount))
                        return RetailCensusError::AllocationFailed;
                    for (std::size_t index = 0u;
                         index < parts.randomDataIntCount; ++index)
                    {
                        (*storage.randomDataInt)[index] =
                            ReadS32(record + index * 4u);
                    }
                    parts.randomDataInt = storage.randomDataInt->data();
                    worldXAnimPhase = WorldXAnimPhase::Indices;
                    break;
                case WorldXAnimPhase::Indices:
                    if (parts.numframes >= 0x100u)
                    {
                        if (!allocateVector(storage.shortIndices, parts.indexCount))
                            return RetailCensusError::AllocationFailed;
                        for (std::size_t index = 0u; index < parts.indexCount; ++index)
                            (*storage.shortIndices)[index] =
                                ReadU16(record + index * 2u);
                        parts.indices._2 = storage.shortIndices->data();
                    }
                    else
                    {
                        if (!allocateVector(storage.byteIndices, parts.indexCount))
                            return RetailCensusError::AllocationFailed;
                        std::memcpy(storage.byteIndices->data(), record, bytes);
                        parts.indices._1 = storage.byteIndices->data();
                    }
                    worldXAnimPhase = WorldXAnimPhase::Complete;
                    break;
                case WorldXAnimPhase::Complete:
                    return RetailCensusError::XAnimLayoutUnsupported;
                }
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error =
                        ScheduleWorldXAnimPayload(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldXAnimPublish)
            {
                RetailPublishedXAnimParts &entry = activeWorldXAnim();
                if (!entry.pointerAlias && entry.serializedReference != 0u)
                {
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (!entry.storage || !entry.storage->name || !entry.asset)
                        return RetailCensusError::XAnimLayoutUnsupported;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.RegisterAsset(
                                ASSET_TYPE_XANIM_PARTS,
                                entry.assetIndex,
                                *entry.storage->name,
                                entry.identity));
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                worldXAnimAliasSlot, entry.identity));
                        error != RetailCensusError::None) return error;
                    if (worldXAnimHasInsertAlias)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldXAnimInsertAliasSlot,
                                    entry.identity));
                            error != RetailCensusError::None) return error;
                    }
                    entry.published = true;
                    entry.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    if (const RetailCensusError error = AppendSemanticTrace(
                            kisak::database::SemanticTraceEventKind::AssetPublish,
                            ASSET_TYPE_XANIM_PARTS,
                            entry.assetIndex,
                            entry.identity,
                            entry.boundaryInflatedOffset,
                            {0u, entry.headerBlock0Offset, XANIM_PARTS_BYTES},
                            *entry.storage->name,
                            worldXAnimAliasSlot);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                }
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount =
                    registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                if (const RetailCensusError error = dispatchSupportedWorldAsset(
                        entry.assetIndex + 1u, stage);
                    error != RetailCensusError::None) return error;
                if (complete) return RetailCensusError::None;
                continue;
            }
            if (stage == RetailCensusStage::WorldRawFile)
            {
                const int visit = visitRecord(RAWFILE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailWorldRawFile &rawFile = result.worldRawFiles.back();
                rawFile.nameReference = ReadU32(record);
                rawFile.length = std::bit_cast<std::int32_t>(ReadU32(record + 4u));
                rawFile.bufferReference = ReadU32(record + 8u);
                if (rawFile.nameReference != INLINE_POINTER)
                    return RetailCensusError::RawFileLayoutUnsupported;
                if (rawFile.length < 0)
                    return RetailCensusError::RawFileSizeInvalid;
                if (static_cast<std::uint32_t>(rawFile.length) >
                    limits.maxRawFileBytes)
                {
                    return RetailCensusError::RawFilePayloadLimit;
                }
                if (rawFile.bufferReference == 0u && rawFile.length != 0)
                    return RetailCensusError::RawFileSizeInvalid;
                cursor += RAWFILE_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldRawFileName;
                continue;
            }
            if (stage == RetailCensusStage::WorldRawFileName)
            {
                RetailWorldRawFile &rawFile = result.worldRawFiles.back();
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxRawFileNameBytes)
                        return RetailCensusError::RawFileNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::RawFileNameInvalid;
                if (bytes > limits.maxRawFileNameBytes)
                    return RetailCensusError::RawFileNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    rawFile.nameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    rawFile.name.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(rawFile.name))
                    return RetailCensusError::RawFileNameInvalid;
                cursor += bytes;
                ++report.recordsProcessed;
                if (rawFile.bufferReference == 0u)
                {
                    stage = RetailCensusStage::WorldRawFilePublish;
                    continue;
                }
                const std::uint64_t payloadBytes =
                    static_cast<std::uint64_t>(rawFile.length) + 1u;
                if (payloadBytes > limits.maxRawFileBytes + 1ull ||
                    retainedRawFileBytes + payloadBytes >
                        limits.maxRetainedRawFileBytes)
                {
                    return RetailCensusError::RawFilePayloadLimit;
                }
                ZoneSpan span;
                if (const RetailCensusError error = Plan(1u, payloadBytes, &span);
                    error != RetailCensusError::None) return error;
                rawFile.bufferBlock4Offset = span.offset;
                stage = RetailCensusStage::WorldRawFileBuffer;
                continue;
            }
            if (stage == RetailCensusStage::WorldRawFileBuffer)
            {
                RetailWorldRawFile &rawFile = result.worldRawFiles.back();
                const std::size_t payloadBytes =
                    static_cast<std::size_t>(rawFile.length) + 1u;
                const int visit = visitRecord(payloadBytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    rawFile.bufferStorage =
                        std::make_shared<std::vector<char>>(payloadBytes);
                    std::memcpy(rawFile.bufferStorage->data(),
                        inflated.data() + cursor, payloadBytes);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                retainedRawFileBytes += payloadBytes;
                cursor += payloadBytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldRawFilePublish;
                continue;
            }
            if (stage == RetailCensusStage::WorldRawFilePublish)
            {
                RetailWorldRawFile &rawFile = result.worldRawFiles.back();
                try
                {
                    rawFile.nameStorage =
                        std::make_shared<std::string>(rawFile.name);
                    rawFile.asset = std::make_shared<RawFile>();
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                rawFile.asset->name = rawFile.nameStorage->c_str();
                rawFile.asset->len = rawFile.length;
                rawFile.asset->buffer = rawFile.bufferStorage
                    ? rawFile.bufferStorage->data()
                    : nullptr;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_RAW_FILE, rawFile.assetIndex,
                            rawFile.name, rawFile.identity));
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.PublishAlias(
                            worldRawFileAliasSlot, rawFile.identity));
                    error != RetailCensusError::None) return error;
                rawFile.published = true;
                rawFile.boundaryInflatedOffset =
                    static_cast<std::uint32_t>(cursor);
                if (const RetailCensusError error = AppendSemanticTrace(
                        kisak::database::SemanticTraceEventKind::AssetPublish,
                        ASSET_TYPE_RAW_FILE,
                        rawFile.assetIndex,
                        rawFile.identity,
                        rawFile.boundaryInflatedOffset,
                        {0u, rawFile.headerBlock0Offset, RAWFILE_BYTES},
                        rawFile.name,
                        worldRawFileAliasSlot);
                    error != RetailCensusError::None)
                {
                    return error;
                }
                ++result.completedAssetCount;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount =
                    registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                result.worldNextAssetIndex = rawFile.assetIndex + 1u;
                result.nextBodyIndex = result.worldNextAssetIndex;
                if (result.nextBodyIndex < worldAssetTypes.size())
                {
                    result.nextBodyType = worldAssetTypes[result.nextBodyIndex];
                    result.nextBodyReference =
                        worldAssetReferences[result.nextBodyIndex];
                    if (result.nextBodyType == ASSET_TYPE_RAW_FILE &&
                        result.nextBodyReference == INLINE_POINTER)
                    {
                        if (const RetailCensusError error = BeginWorldRawFile(
                                result.nextBodyIndex, stage);
                            error != RetailCensusError::None)
                        {
                            return error;
                        }
                        continue;
                    }
                    if (const RetailCensusError error =
                            dispatchSupportedWorldAsset(
                                result.nextBodyIndex, stage);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    if (!complete) continue;
                    result.stoppedAfterCanonicalRawFile = true;
                }
                else
                {
                    stage = RetailCensusStage::AssetBoundary;
                    complete = true;
                }
                return RetailCensusError::None;
            }
            if (stage == RetailCensusStage::WorldXModel)
            {
                const int visit = visitRecord(XMODEL_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                RetailWorldXModel &model = activeWorldXModel();
                if (ReadU32(record) != INLINE_POINTER)
                    return RetailCensusError::XModelLayoutUnsupported;
                model.numBones = record[4u];
                model.numRootBones = record[5u];
                model.surfaceCount = record[6u];
                model.lodRampType = record[7u];
                model.boneNamesReference = ReadU32(record + 8u);
                model.parentListReference = ReadU32(record + 12u);
                model.quatsReference = ReadU32(record + 16u);
                model.transReference = ReadU32(record + 20u);
                model.partClassificationReference = ReadU32(record + 24u);
                model.baseMatReference = ReadU32(record + 28u);
                model.surfacesReference = ReadU32(record + 32u);
                model.materialHandlesReference = ReadU32(record + 36u);
                for (std::size_t index = 0u; index < model.lods.size(); ++index)
                {
                    const std::uint8_t *lodRecord = record + 40u + index * 28u;
                    RetailXModelLod &lod = model.lods[index];
                    lod.distance = ReadF32(lodRecord);
                    lod.surfaceCount = ReadU16(lodRecord + 4u);
                    lod.surfaceIndex = ReadU16(lodRecord + 6u);
                    for (std::size_t bit = 0u; bit < lod.partBits.size(); ++bit)
                        lod.partBits[bit] = ReadU32(lodRecord + 8u + bit * 4u);
                    lod.lod = lodRecord[24u];
                    lod.smcIndexPlusOne = lodRecord[25u];
                    lod.smcAllocBits = lodRecord[26u];
                    if (lodRecord[27u] != 0u)
                        return RetailCensusError::XModelLayoutUnsupported;
                }
                model.collisionSurfacesReference = ReadU32(record + 152u);
                model.collisionSurfaceCount = ReadU32(record + 156u);
                model.contents = ReadU32(record + 160u);
                model.boneInfoReference = ReadU32(record + 164u);
                model.radius = ReadF32(record + 168u);
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    model.mins[axis] = ReadF32(record + 172u + axis * 4u);
                    model.maxs[axis] = ReadF32(record + 184u + axis * 4u);
                }
                model.lodCount = ReadS16(record + 196u);
                model.collisionLod = ReadS16(record + 198u);
                model.memoryUsage = ReadU32(record + 204u);
                model.flags = record[208u];
                model.bad = record[209u] != 0u;
                model.physPresetReference = ReadU32(record + 212u);
                model.physGeomsReference = ReadU32(record + 216u);

                XModel &canonical = *model.asset;
                canonical.numBones = model.numBones;
                canonical.numRootBones = model.numRootBones;
                canonical.numsurfs = model.surfaceCount;
                canonical.lodRampType = model.lodRampType;
                for (std::size_t index = 0u; index < model.lods.size(); ++index)
                {
                    canonical.lodInfo[index].dist = model.lods[index].distance;
                    canonical.lodInfo[index].numsurfs =
                        model.lods[index].surfaceCount;
                    canonical.lodInfo[index].surfIndex =
                        model.lods[index].surfaceIndex;
                    for (std::size_t bit = 0u; bit < 4u; ++bit)
                    {
                        canonical.lodInfo[index].partBits[bit] =
                            static_cast<int>(model.lods[index].partBits[bit]);
                    }
                    canonical.lodInfo[index].lod = model.lods[index].lod;
                    canonical.lodInfo[index].smcIndexPlusOne =
                        model.lods[index].smcIndexPlusOne;
                    canonical.lodInfo[index].smcAllocBits =
                        model.lods[index].smcAllocBits;
                    canonical.lodInfo[index].unused = 0u;
                }
                canonical.numCollSurfs =
                    static_cast<int>(model.collisionSurfaceCount);
                canonical.contents = static_cast<int>(model.contents);
                canonical.radius = model.radius;
                std::copy(model.mins.begin(), model.mins.end(), canonical.mins);
                std::copy(model.maxs.begin(), model.maxs.end(), canonical.maxs);
                canonical.numLods = model.lodCount;
                canonical.collLod = model.collisionLod;
                canonical.memUsage = static_cast<int>(model.memoryUsage);
                canonical.flags = model.flags;
                canonical.bad = model.bad;

                const std::uint32_t childBones =
                    static_cast<std::uint32_t>(model.numBones) -
                    static_cast<std::uint32_t>(model.numRootBones);
                const bool emptyBuiltin = model.numBones == 0u &&
                    model.numRootBones == 0u && model.surfaceCount == 0u &&
                    model.lodCount == 0 && model.collisionLod == 0 &&
                    model.collisionSurfaceCount == 0u;
                if (model.numRootBones > model.numBones || model.lodRampType > 1u ||
                    (!emptyBuiltin && (model.lodCount <= 0 || model.lodCount > 4 ||
                     model.collisionLod < -1 ||
                     model.collisionLod >= model.lodCount)) ||
                    model.collisionSurfaceCount > limits.maxXModelCollisionSurfaces ||
                    record[209u] > 1u)
                {
                    return RetailCensusError::XModelCountInvalid;
                }
                if (!std::isfinite(model.radius) || model.radius < 0.0f)
                    return RetailCensusError::XModelBoundsInvalid;
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    if (!std::isfinite(model.mins[axis]) ||
                        !std::isfinite(model.maxs[axis]) ||
                        model.mins[axis] > model.maxs[axis])
                    {
                        return RetailCensusError::XModelBoundsInvalid;
                    }
                }
                for (std::int32_t index = 0; index < model.lodCount; ++index)
                {
                    const RetailXModelLod &lod = model.lods[static_cast<std::size_t>(index)];
                    if (!std::isfinite(lod.distance) || lod.distance < 0.0f ||
                        static_cast<std::uint32_t>(lod.surfaceIndex) + lod.surfaceCount >
                            model.surfaceCount)
                    {
                        return RetailCensusError::XModelCountInvalid;
                    }
                }
                const auto pointerMatchesCount = [](std::uint32_t token,
                                                     std::uint32_t count) noexcept {
                    return (count == 0u) == (token == 0u);
                };
                if (!pointerMatchesCount(model.boneNamesReference, model.numBones) ||
                    !pointerMatchesCount(model.parentListReference, childBones) ||
                    !pointerMatchesCount(model.quatsReference, childBones) ||
                    !pointerMatchesCount(model.transReference, childBones) ||
                    !pointerMatchesCount(model.partClassificationReference, model.numBones) ||
                    !pointerMatchesCount(model.baseMatReference, model.numBones) ||
                    !pointerMatchesCount(model.surfacesReference, model.surfaceCount) ||
                    !pointerMatchesCount(model.materialHandlesReference, model.surfaceCount) ||
                    !pointerMatchesCount(
                        model.collisionSurfacesReference, model.collisionSurfaceCount) ||
                    !pointerMatchesCount(model.boneInfoReference, model.numBones))
                {
                    return RetailCensusError::XModelLayoutUnsupported;
                }
                model.headerTraversed = true;
                cursor += XMODEL_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXModelName;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelName)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxXModelNameBytes)
                        return RetailCensusError::XModelNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::XModelNameInvalid;
                if (bytes > limits.maxXModelNameBytes)
                    return RetailCensusError::XModelNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    model.nameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.name.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                    model.canonicalName =
                        std::make_shared<std::string>(model.name);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(model.name))
                    return RetailCensusError::XModelNameInvalid;
                if (model.lodCount == 0 && !model.name.starts_with(','))
                    return RetailCensusError::XModelCountInvalid;
                model.asset->name = model.canonicalName->c_str();
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelBoneNames;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelBoneNames)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.numBones == 0u)
                {
                    stage = RetailCensusStage::WorldXModelParentList;
                    continue;
                }
                if (model.boneNamesReference != INLINE_POINTER &&
                    model.boneNamesReference != SHARED_POINTER)
                {
                    if (const RetailCensusError error =
                            ResolvePriorWorldXModelBoneNames(model);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    ++report.recordsProcessed;
                    stage = RetailCensusStage::WorldXModelParentList;
                    continue;
                }
                const std::size_t bytes = static_cast<std::size_t>(model.numBones) * 2u;
                if (!Available(bytes))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(2u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    model.boneNamesBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                std::vector<std::uint16_t> indices;
                try
                {
                    indices.resize(model.numBones);
                    for (std::size_t index = 0u; index < model.numBones; ++index)
                        indices[index] = ReadU16(
                            inflated.data() + cursor + index * 2u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (const RetailCensusError error = AssignWorldXModelBoneNames(
                        model, std::span<const std::uint16_t>(indices));
                    error != RetailCensusError::None)
                {
                    return error;
                }
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelParentList;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelParentList)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const std::uint32_t childBones = model.numBones - model.numRootBones;
                if (childBones == 0u)
                {
                    stage = RetailCensusStage::WorldXModelQuats;
                    continue;
                }
                if (model.parentListReference != INLINE_POINTER &&
                    model.parentListReference != SHARED_POINTER)
                {
                    if (const RetailCensusError error =
                            ResolvePriorWorldXModelArray(
                                model.parentListReference, childBones, 1u,
                                &RetailWorldXModel::parentList,
                                &RetailWorldXModel::parentListBlock4Offset,
                                model.parentList,
                                model.parentListBlock4Offset,
                                RetailCensusError::XModelArrayAliasInvalid);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    ++report.recordsProcessed;
                    stage = RetailCensusStage::WorldXModelQuats;
                    continue;
                }
                if (!Available(childBones))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, childBones, &span);
                        error != RetailCensusError::None) return error;
                    model.parentListBlock4Offset = span.offset;
                }
                const int visit = visitRecord(childBones);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.parentList.assign(
                        inflated.begin() + static_cast<std::ptrdiff_t>(cursor),
                        inflated.begin() + static_cast<std::ptrdiff_t>(cursor + childBones));
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += childBones;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelQuats;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelQuats)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const std::uint32_t childBones = model.numBones - model.numRootBones;
                if (childBones == 0u)
                {
                    stage = RetailCensusStage::WorldXModelTrans;
                    continue;
                }
                const std::size_t count =
                    static_cast<std::size_t>(childBones) * 4u;
                if (model.quatsReference != INLINE_POINTER &&
                    model.quatsReference != SHARED_POINTER)
                {
                    if (const RetailCensusError error =
                            ResolvePriorWorldXModelArray(
                                model.quatsReference, count, 2u,
                                &RetailWorldXModel::quats,
                                &RetailWorldXModel::quatsBlock4Offset,
                                model.quats, model.quatsBlock4Offset,
                                RetailCensusError::XModelArrayAliasInvalid);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    ++report.recordsProcessed;
                    stage = RetailCensusStage::WorldXModelTrans;
                    continue;
                }
                const std::size_t bytes = static_cast<std::size_t>(childBones) * 8u;
                if (!Available(bytes))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(2u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    model.quatsBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.quats.resize(count);
                    for (std::size_t index = 0u; index < count; ++index)
                        model.quats[index] = ReadS16(
                            inflated.data() + cursor + index * 2u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelTrans;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelTrans)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const std::uint32_t childBones = model.numBones - model.numRootBones;
                if (childBones == 0u)
                {
                    stage = RetailCensusStage::WorldXModelPartClassification;
                    continue;
                }
                const std::size_t count = static_cast<std::size_t>(childBones) * 4u;
                if (model.transReference != INLINE_POINTER &&
                    model.transReference != SHARED_POINTER)
                {
                    if (const RetailCensusError error =
                            ResolvePriorWorldXModelArray(
                                model.transReference, count, 4u,
                                &RetailWorldXModel::trans,
                                &RetailWorldXModel::transBlock4Offset,
                                model.trans, model.transBlock4Offset,
                                RetailCensusError::XModelArrayAliasInvalid);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    ++report.recordsProcessed;
                    stage = RetailCensusStage::WorldXModelPartClassification;
                    continue;
                }
                const std::size_t bytes = count * 4u;
                if (!Available(bytes))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(4u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    model.transBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.trans.resize(count);
                    for (std::size_t index = 0u; index < count; ++index)
                    {
                        const float value = ReadF32(
                            inflated.data() + cursor + index * 4u);
                        if (!std::isfinite(value))
                            return RetailCensusError::XModelBoundsInvalid;
                        model.trans[index] = value;
                    }
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelPartClassification;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelPartClassification)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.numBones == 0u)
                {
                    stage = RetailCensusStage::WorldXModelBaseMat;
                    continue;
                }
                if (model.partClassificationReference != INLINE_POINTER &&
                    model.partClassificationReference != SHARED_POINTER)
                {
                    if (const RetailCensusError error =
                            ResolvePriorWorldXModelArray(
                                model.partClassificationReference,
                                model.numBones, 1u,
                                &RetailWorldXModel::partClassification,
                                &RetailWorldXModel::partClassificationBlock4Offset,
                                model.partClassification,
                                model.partClassificationBlock4Offset,
                                RetailCensusError::XModelArrayAliasInvalid);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    ++report.recordsProcessed;
                    stage = RetailCensusStage::WorldXModelBaseMat;
                    continue;
                }
                if (!Available(model.numBones))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, model.numBones, &span);
                        error != RetailCensusError::None) return error;
                    model.partClassificationBlock4Offset = span.offset;
                }
                const int visit = visitRecord(model.numBones);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.partClassification.assign(
                        inflated.begin() + static_cast<std::ptrdiff_t>(cursor),
                        inflated.begin() + static_cast<std::ptrdiff_t>(
                            cursor + model.numBones));
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += model.numBones;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelBaseMat;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelBaseMat)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.numBones != 0u)
                {
                    const std::size_t floatCount =
                        static_cast<std::size_t>(model.numBones) * 8u;
                    if (model.baseMatReference != INLINE_POINTER &&
                        model.baseMatReference != SHARED_POINTER)
                    {
                        if (const RetailCensusError error =
                                ResolvePriorWorldXModelArray(
                                    model.baseMatReference, floatCount, 4u,
                                    &RetailWorldXModel::baseMat,
                                    &RetailWorldXModel::baseMatBlock4Offset,
                                    model.baseMat,
                                    model.baseMatBlock4Offset,
                                    RetailCensusError::XModelArrayAliasInvalid);
                            error != RetailCensusError::None)
                        {
                            return error;
                        }
                        ++report.recordsProcessed;
                        model.skeletonPrefixTraversed = true;
                    }
                    else
                    {
                        const std::size_t bytes =
                            static_cast<std::size_t>(model.numBones) *
                            DOBJ_ANIM_MAT_BYTES;
                        if (!Available(bytes))
                        {
                            blocked = true;
                            return RetailCensusError::None;
                        }
                        if (recordVisited == 0u)
                        {
                            ZoneSpan span;
                            if (const RetailCensusError error =
                                    Plan(4u, bytes, &span);
                                error != RetailCensusError::None) return error;
                            model.baseMatBlock4Offset = span.offset;
                        }
                        const int visit = visitRecord(bytes);
                        if (visit <= 0) return RetailCensusError::None;
                        try
                        {
                            model.baseMat.resize(floatCount);
                            for (std::size_t index = 0u;
                                 index < floatCount; ++index)
                            {
                                const float value = ReadF32(
                                    inflated.data() + cursor + index * 4u);
                                if (!std::isfinite(value))
                                    return RetailCensusError::XModelBoundsInvalid;
                                model.baseMat[index] = value;
                            }
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                        cursor += bytes;
                        ++report.recordsProcessed;
                    }
                }
                model.skeletonPrefixTraversed = true;
                if (model.surfacesReference != 0u)
                {
                    const bool traverseFirstSurfaceDependencies =
                        worldXModelIndex == 0u &&
                        (mode == RetailCensusMode::WorldXSurfacePrefix ||
                         mode == RetailCensusMode::WorldXModelDependencies ||
                         mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                         mode == RetailCensusMode::WorldSecondXModelPrefix ||
                         mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                         mode == RetailCensusMode::WorldSecondXModelDependencies ||
                         mode == RetailCensusMode::WorldXModelLoader);
                    const bool traverseSecondSurfaceDependencies =
                        worldXModelIndex != 0u &&
                        (mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                         mode == RetailCensusMode::WorldSecondXModelDependencies ||
                         mode == RetailCensusMode::WorldXModelLoader);
                    const bool traverseSurfaceDependencies =
                        traverseFirstSurfaceDependencies ||
                        traverseSecondSurfaceDependencies;
                    if (!traverseSurfaceDependencies)
                    {
                        finishWorldXModel("Load_XSurfaceArray", true, true);
                        return RetailCensusError::None;
                    }
                    stage = RetailCensusStage::WorldXModelSurfaceHeaders;
                    continue;
                }
                if (mode == RetailCensusMode::WorldXModelLoader)
                {
                    model.surfaceHeadersTraversed = true;
                    model.surfaceDependenciesTraversed = true;
                    model.materialHandlesTraversed = true;
                    model.materialsTraversed = true;
                    stage = RetailCensusStage::WorldXModelCollisionSurfaces;
                    continue;
                }
                finishWorldXModel("Load_MaterialHandleArray", true, false);
                return RetailCensusError::None;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceHeaders)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const std::size_t bytes =
                    static_cast<std::size_t>(model.surfaceCount) * XSURFACE_BYTES;
                if (!Available(bytes))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(4u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = addSurfacePayload(bytes);
                        error != RetailCensusError::None) return error;
                    model.surfacesBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.surfaces.resize(model.surfaceCount);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                std::uint64_t totalVertices = 0u;
                std::uint64_t totalTriangles = 0u;
                std::uint64_t totalVertLists = 0u;
                for (std::size_t index = 0u; index < model.surfaces.size(); ++index)
                {
                    const std::uint8_t *record =
                        inflated.data() + cursor + index * XSURFACE_BYTES;
                    RetailXSurface &surface = model.surfaces[index];
                    surface.index = static_cast<std::uint32_t>(index);
                    surface.tileMode = record[0u];
                    if (record[1u] > 1u)
                        return RetailCensusError::XSurfaceLayoutUnsupported;
                    surface.deformed = record[1u] != 0u;
                    surface.vertCount = ReadU16(record + 2u);
                    surface.triCount = ReadU16(record + 4u);
                    surface.zoneHandle = record[6u];
                    surface.baseTriIndex = ReadU16(record + 8u);
                    surface.baseVertIndex = ReadU16(record + 10u);
                    surface.triIndicesReference = ReadU32(record + 12u);
                    for (std::size_t weight = 0u; weight < 4u; ++weight)
                    {
                        surface.blendVertCounts[weight] =
                            ReadS16(record + 16u + weight * 2u);
                        if (surface.blendVertCounts[weight] < 0)
                            return RetailCensusError::XSurfaceCountInvalid;
                    }
                    surface.vertsBlendReference = ReadU32(record + 24u);
                    surface.vertsReference = ReadU32(record + 28u);
                    surface.vertListCount = ReadU32(record + 32u);
                    surface.vertListReference = ReadU32(record + 36u);
                    for (std::size_t part = 0u; part < 4u; ++part)
                        surface.partBits[part] = ReadU32(record + 40u + part * 4u);

                    const std::uint32_t blendVertices =
                        static_cast<std::uint32_t>(surface.blendVertCounts[0]) +
                        static_cast<std::uint32_t>(surface.blendVertCounts[1]) +
                        static_cast<std::uint32_t>(surface.blendVertCounts[2]) +
                        static_cast<std::uint32_t>(surface.blendVertCounts[3]);
                    surface.blendWordCount =
                        static_cast<std::uint32_t>(surface.blendVertCounts[0]) +
                        3u * static_cast<std::uint32_t>(surface.blendVertCounts[1]) +
                        5u * static_cast<std::uint32_t>(surface.blendVertCounts[2]) +
                        7u * static_cast<std::uint32_t>(surface.blendVertCounts[3]);
                    const auto pointerMatchesCount = [](std::uint32_t token,
                                                         std::uint32_t count) noexcept {
                        return (count == 0u) == (token == 0u);
                    };
                    if (blendVertices > surface.vertCount ||
                        !pointerMatchesCount(
                            surface.vertsBlendReference, surface.blendWordCount) ||
                        !pointerMatchesCount(surface.vertsReference, surface.vertCount) ||
                        !pointerMatchesCount(
                            surface.triIndicesReference, surface.triCount) ||
                        !pointerMatchesCount(
                            surface.vertListReference, surface.vertListCount) ||
                        static_cast<std::uint32_t>(surface.baseVertIndex) +
                            surface.vertCount > 65536u ||
                        static_cast<std::uint32_t>(surface.baseTriIndex) +
                            surface.triCount > 65536u)
                    {
                        return RetailCensusError::XSurfaceLayoutUnsupported;
                    }
                    totalVertices += surface.vertCount;
                    totalTriangles += surface.triCount;
                    totalVertLists += surface.vertListCount;
                    if (totalVertices > limits.maxXModelSurfaceVertices ||
                        totalTriangles > limits.maxXModelSurfaceTriangles ||
                        totalVertLists > limits.maxXModelRigidVertLists)
                    {
                        return RetailCensusError::XSurfaceCountInvalid;
                    }
                }
                model.totalVertices = static_cast<std::uint32_t>(totalVertices);
                model.totalTriangles = static_cast<std::uint32_t>(totalTriangles);
                model.totalRigidVertLists = static_cast<std::uint32_t>(totalVertLists);
                model.surfaceHeadersTraversed = true;
                cursor += bytes;
                ++report.recordsProcessed;
                worldSurfaceIndex = 0u;
                stage = RetailCensusStage::WorldXModelSurfaceBlendInfo;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceBlendInfo)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (worldSurfaceIndex >= model.surfaces.size())
                {
                    model.surfaceDependenciesTraversed = true;
                    stage = RetailCensusStage::WorldXModelMaterialHandles;
                    continue;
                }
                RetailXSurface &surface = model.surfaces[worldSurfaceIndex];
                if (surface.vertsBlendReference == INLINE_POINTER)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(surface.blendWordCount) * 2u;
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(2u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = addSurfacePayload(bytes);
                            error != RetailCensusError::None) return error;
                        surface.blendInfoBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                if (const RetailCensusError error = Push(7u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXModelSurfaceVertices;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceVertices)
            {
                RetailWorldXModel &model = activeWorldXModel();
                RetailXSurface &surface = model.surfaces[worldSurfaceIndex];
                if (surface.vertsReference == INLINE_POINTER)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(surface.vertCount) *
                        GFX_PACKED_VERTEX_BYTES;
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(16u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = addSurfacePayload(bytes);
                            error != RetailCensusError::None) return error;
                        surface.verticesBlock7Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    surface.verticesHash = Fnv1a32(
                        std::span<const std::uint8_t>(inflated.data() + cursor, bytes));
                    const RetailXModelLod &firstLod = model.lods[0];
                    const bool inFirstLod =
                        worldSurfaceIndex >= firstLod.surfaceIndex &&
                        worldSurfaceIndex - firstLod.surfaceIndex < firstLod.surfaceCount;
                    const std::uint64_t retainedSurfaceBytes =
                        static_cast<std::uint64_t>(bytes) +
                        static_cast<std::uint64_t>(surface.triCount) * 6u;
                    if (inFirstLod &&
                        (mode == RetailCensusMode::WorldXModelDependencies ||
                         mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                         mode == RetailCensusMode::WorldSecondXModelPrefix ||
                         (mode == RetailCensusMode::WorldSecondXSurfacePrefix &&
                          worldXModelIndex == 0u) ||
                         (mode == RetailCensusMode::WorldSecondXModelDependencies &&
                          worldXModelIndex == 0u) ||
                         mode == RetailCensusMode::WorldXModelLoader) &&
                        surface.triIndicesReference == INLINE_POINTER &&
                        surface.vertCount <= MAX_RETAINED_RENDER_VERTICES &&
                        surface.triCount <= MAX_RETAINED_RENDER_TRIANGLES &&
                        retainedLodVertices <=
                            MAX_RETAINED_LOD_VERTICES - surface.vertCount &&
                        retainedLodTriangles <=
                            MAX_RETAINED_LOD_TRIANGLES - surface.triCount &&
                        retainedSurfaceBytes <=
                            limits.maxRetainedXModelRendererBytes &&
                        retainedRendererPayloadBytes <=
                            limits.maxRetainedXModelRendererBytes -
                                retainedSurfaceBytes)
                    {
                        try
                        {
                            surface.retainedPackedVertices.assign(
                                inflated.data() + cursor,
                                inflated.data() + cursor + bytes);
                            retainedLodVertices += surface.vertCount;
                            retainedLodTriangles += surface.triCount;
                            retainedRendererPayloadBytes += retainedSurfaceBytes;
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXModelSurfaceVertLists;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceVertLists)
            {
                RetailXSurface &surface =
                    activeWorldXModel().surfaces[worldSurfaceIndex];
                if (surface.vertListReference == INLINE_POINTER)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(surface.vertListCount) *
                        RIGID_VERT_LIST_BYTES;
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = addSurfacePayload(bytes);
                            error != RetailCensusError::None) return error;
                        surface.vertListsBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    try
                    {
                        surface.rigidVertLists.resize(surface.vertListCount);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    std::uint64_t listedVertices = 0u;
                    std::uint64_t listedTriangles = 0u;
                    for (std::size_t index = 0u;
                         index < surface.rigidVertLists.size(); ++index)
                    {
                        const std::uint8_t *record = inflated.data() + cursor +
                            index * RIGID_VERT_LIST_BYTES;
                        RetailXRigidVertList &list = surface.rigidVertLists[index];
                        list.boneOffset = ReadU16(record);
                        list.vertCount = ReadU16(record + 2u);
                        list.triOffset = ReadU16(record + 4u);
                        list.triCount = ReadU16(record + 6u);
                        list.collisionTree.reference = ReadU32(record + 8u);
                        listedVertices += list.vertCount;
                        listedTriangles += list.triCount;
                        if (static_cast<std::uint32_t>(list.triOffset) +
                                list.triCount > surface.triCount ||
                            listedVertices > surface.vertCount ||
                            listedTriangles > surface.triCount)
                        {
                            return RetailCensusError::XSurfaceCountInvalid;
                        }
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                    worldRigidVertListIndex = 0u;
                    stage = RetailCensusStage::WorldXModelSurfaceCollisionTree;
                    continue;
                }
                if (const RetailCensusError error = Push(8u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXModelSurfaceIndices;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceCollisionTree)
            {
                RetailWorldXModel &model = activeWorldXModel();
                RetailXSurface &surface = model.surfaces[worldSurfaceIndex];
                if (worldRigidVertListIndex >= surface.rigidVertLists.size())
                {
                    if (const RetailCensusError error = Push(8u);
                        error != RetailCensusError::None) return error;
                    stage = RetailCensusStage::WorldXModelSurfaceIndices;
                    continue;
                }
                RetailXSurfaceCollisionTree &tree =
                    surface.rigidVertLists[worldRigidVertListIndex].collisionTree;
                if (tree.reference != INLINE_POINTER)
                {
                    ++worldRigidVertListIndex;
                    continue;
                }
                if (!Available(SURFACE_COLLISION_TREE_BYTES))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, SURFACE_COLLISION_TREE_BYTES, &span);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = addSurfacePayload(
                            SURFACE_COLLISION_TREE_BYTES);
                        error != RetailCensusError::None) return error;
                    tree.headerBlock4Offset = span.offset;
                }
                const int visit = visitRecord(SURFACE_COLLISION_TREE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    tree.translation[axis] = ReadF32(record + axis * 4u);
                    tree.scale[axis] = ReadF32(record + 12u + axis * 4u);
                    if (!std::isfinite(tree.translation[axis]) ||
                        std::isnan(tree.scale[axis]) ||
                        tree.scale[axis] <= 0.0f)
                    {
                        return RetailCensusError::XSurfaceCollisionInvalid;
                    }
                }
                tree.nodeCount = ReadU32(record + 24u);
                tree.nodesReference = ReadU32(record + 28u);
                tree.leafCount = ReadU32(record + 32u);
                tree.leafsReference = ReadU32(record + 36u);
                if ((tree.nodeCount == 0u) != (tree.nodesReference == 0u) ||
                    (tree.leafCount == 0u) != (tree.leafsReference == 0u) ||
                    static_cast<std::uint64_t>(
                        model.totalCollisionNodes) +
                            tree.nodeCount > limits.maxXModelCollisionNodes ||
                    static_cast<std::uint64_t>(
                        model.totalCollisionLeaves) +
                            tree.leafCount > limits.maxXModelCollisionLeaves)
                {
                    return RetailCensusError::XSurfaceCollisionInvalid;
                }
                model.totalCollisionNodes += tree.nodeCount;
                model.totalCollisionLeaves += tree.leafCount;
                cursor += SURFACE_COLLISION_TREE_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelSurfaceCollisionNodes;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceCollisionNodes)
            {
                RetailXSurfaceCollisionTree &tree =
                    activeWorldXModel().surfaces[worldSurfaceIndex]
                        .rigidVertLists[worldRigidVertListIndex].collisionTree;
                if (tree.nodesReference != 0u)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(tree.nodeCount) *
                        SURFACE_COLLISION_NODE_BYTES;
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(16u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = addSurfacePayload(bytes);
                            error != RetailCensusError::None) return error;
                        tree.nodesBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    tree.nodesHash = Fnv1a32(
                        std::span<const std::uint8_t>(inflated.data() + cursor, bytes));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                stage = RetailCensusStage::WorldXModelSurfaceCollisionLeaves;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceCollisionLeaves)
            {
                RetailXSurfaceCollisionTree &tree =
                    activeWorldXModel().surfaces[worldSurfaceIndex]
                        .rigidVertLists[worldRigidVertListIndex].collisionTree;
                if (tree.leafsReference != 0u)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(tree.leafCount) *
                        SURFACE_COLLISION_LEAF_BYTES;
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(2u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = addSurfacePayload(bytes);
                            error != RetailCensusError::None) return error;
                        tree.leafsBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    tree.leafsHash = Fnv1a32(
                        std::span<const std::uint8_t>(inflated.data() + cursor, bytes));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                tree.traversed = true;
                ++worldRigidVertListIndex;
                stage = RetailCensusStage::WorldXModelSurfaceCollisionTree;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelSurfaceIndices)
            {
                RetailXSurface &surface =
                    activeWorldXModel().surfaces[worldSurfaceIndex];
                if (surface.triIndicesReference == INLINE_POINTER)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(surface.triCount) * 6u;
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(16u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        if (const RetailCensusError error = addSurfacePayload(bytes);
                            error != RetailCensusError::None) return error;
                        surface.indicesBlock8Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    surface.indicesHash = Fnv1a32(
                        std::span<const std::uint8_t>(inflated.data() + cursor, bytes));
                    if (!surface.retainedPackedVertices.empty())
                    {
                        try
                        {
                            surface.retainedPackedIndices.assign(
                                inflated.data() + cursor,
                                inflated.data() + cursor + bytes);
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                        surface.renderPayloadRetained = true;
                        activeWorldXModel().rendererPayloadAvailable = true;
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                surface.dependenciesTraversed = true;
                ++worldSurfaceIndex;
                stage = RetailCensusStage::WorldXModelSurfaceBlendInfo;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelMaterialHandles)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.materialHandlesReference == 0u)
                {
                    model.materialHandlesTraversed = true;
                    if (mode != RetailCensusMode::WorldXModelDependencies &&
                        mode != RetailCensusMode::WorldPostXModelTechniqueSet &&
                        mode != RetailCensusMode::WorldSecondXModelPrefix &&
                        !(mode == RetailCensusMode::WorldSecondXSurfacePrefix &&
                          worldXModelIndex == 0u) &&
                        mode != RetailCensusMode::WorldSecondXModelDependencies &&
                        mode != RetailCensusMode::WorldXModelLoader)
                    {
                        finishWorldXModel("Load_XModelCollSurfArray", true, false);
                        return RetailCensusError::None;
                    }
                    model.materialsTraversed = true;
                    stage = RetailCensusStage::WorldXModelCollisionSurfaces;
                    continue;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(model.surfaceCount) * 4u;
                if (!Available(bytes))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(4u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = addSurfacePayload(bytes);
                        error != RetailCensusError::None) return error;
                    model.materialHandlesBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.materialReferences.resize(model.surfaceCount);
                    for (std::size_t index = 0u;
                         index < model.materialReferences.size(); ++index)
                    {
                        model.materialReferences[index] =
                            ReadU32(inflated.data() + cursor + index * 4u);
                    }
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                model.materialHandlesTraversed = true;
                try
                {
                    model.materialIdentities.assign(model.surfaceCount, 0u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (mode == RetailCensusMode::WorldXModelDependencies ||
                    mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                    mode == RetailCensusMode::WorldSecondXModelPrefix ||
                    (mode == RetailCensusMode::WorldSecondXSurfacePrefix &&
                     worldXModelIndex == 0u) ||
                    mode == RetailCensusMode::WorldSecondXModelDependencies ||
                    mode == RetailCensusMode::WorldXModelLoader)
                {
                    worldMaterialIndex = 0u;
                    if (const RetailCensusError error = advanceWorldMaterials(stage);
                        error != RetailCensusError::None)
                    {
                        if (mode == RetailCensusMode::WorldXModelLoader &&
                            error == RetailCensusError::XModelMaterialAliasInvalid &&
                            hasCompletedWorldMaterialTechnique())
                        {
                            activeWorldXModel().stoppedBeforeMaterialDependency = true;
                            finishWorldXModel(
                                "Load_Material(alias)", true, false);
                            return RetailCensusError::None;
                        }
                        return error;
                    }
                    continue;
                }
                const bool hasInlineMaterial = std::any_of(
                    model.materialReferences.begin(), model.materialReferences.end(),
                    [](std::uint32_t token) {
                        return token == INLINE_POINTER || token == SHARED_POINTER;
                    });
                if (hasInlineMaterial)
                {
                    model.stoppedBeforeMaterialDependency = true;
                    finishWorldXModel("Load_Material", true, false);
                    return RetailCensusError::None;
                }
                finishWorldXModel("Load_XModelCollSurfArray", true, false);
                return RetailCensusError::None;
            }
            if (stage == RetailCensusStage::WorldXModelMaterial)
            {
                RetailXModelMaterial &material = activeWorldMaterial();
                const int visit = visitRecord(MATERIAL_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                material.textureCount = record[58u];
                material.constantCount = record[59u];
                material.stateBitsCount = record[60u];
                material.techniqueSetReference = ReadU32(record + 64u);
                worldMaterialNameReference = ReadU32(record);
                worldMaterialNameResolved = true;
                const std::uint32_t textureToken = ReadU32(record + 68u);
                const std::uint32_t constantToken = ReadU32(record + 72u);
                const std::uint32_t stateBitsToken = ReadU32(record + 76u);
                const auto pointerMatchesCount = [](std::uint32_t token,
                                                     std::uint32_t count) noexcept {
                    return (count == 0u && token == 0u) ||
                        (count != 0u && token == INLINE_POINTER);
                };
                if (worldMaterialNameReference == 0u ||
                    worldMaterialNameReference == SHARED_POINTER ||
                    (worldMaterialNameReference != INLINE_POINTER &&
                     !ValidPriorZonePointer(worldMaterialNameReference)) ||
                    record[63u] != 0u ||
                    material.textureCount > limits.maxMaterialTextures ||
                    material.constantCount > limits.maxMaterialConstants ||
                    material.stateBitsCount > limits.maxMaterialStateBits ||
                    !pointerMatchesCount(textureToken, material.textureCount) ||
                    !pointerMatchesCount(constantToken, material.constantCount) ||
                    !pointerMatchesCount(stateBitsToken, material.stateBitsCount))
                {
                    if (worldMaterialOwnedByFx)
                        return RetailCensusError::FxMaterialUnsupported;
                    if (material.textureCount > limits.maxMaterialTextures)
                        return RetailCensusError::MaterialTextureCountLimit;
                    return RetailCensusError::MaterialLayoutUnsupported;
                }
                if (material.techniqueSetReference != 0u &&
                    ResolveRegistryAlias(
                        material.techniqueSetReference,
                        ASSET_TYPE_TECHNIQUE_SET,
                        material.techniqueSetIdentity) != ZoneRegistryError::None)
                {
                    if (!prerequisiteZone || !ValidPriorZonePointer(
                            material.techniqueSetReference, 4u))
                    {
                        return RetailCensusError::MaterialTechniqueSetInvalid;
                    }
                    material.techniqueSetIdentity = 0u;
                }
                Material &canonical = *material.asset;
                canonical.info.gameFlags = record[4u];
                canonical.info.sortKey = record[5u];
                canonical.info.textureAtlasRowCount = record[6u];
                canonical.info.textureAtlasColumnCount = record[7u];
                std::memcpy(&canonical.info.drawSurf.packed, record + 8u, 8u);
                canonical.info.surfaceTypeBits = ReadU32(record + 16u);
                canonical.info.hashIndex = ReadU16(record + 20u);
                canonical.info.padding = ReadU16(record + 22u);
                std::memcpy(canonical.stateBitsEntry, record + 24u, 34u);
                canonical.textureCount = material.textureCount;
                canonical.constantCount = material.constantCount;
                canonical.stateBitsCount = material.stateBitsCount;
                canonical.stateFlags = record[61u];
                canonical.cameraRegion = record[62u];
                canonical.padding = record[63u];
                for (std::size_t index = 0u;
                     material.techniqueSetReference != 0u && index < 34u;
                     ++index)
                {
                    const std::uint8_t entry = record[24u + index];
                    if (entry != 0xffu && entry >= material.stateBitsCount)
                        return RetailCensusError::MaterialStateBitsUnsupported;
                }
                cursor += MATERIAL_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXModelMaterialName;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelMaterialName)
            {
                RetailXModelMaterial &material = activeWorldMaterial();
                if (worldMaterialNameReference != INLINE_POINTER)
                {
                    std::shared_ptr<std::string> name;
                    std::uint32_t offset = UINT32_MAX;
                    if (!ResolvePriorZoneStringPayload(
                            worldMaterialNameReference, name, offset) || !name ||
                        !ValidPublishedName(*name))
                    {
                        if (!prerequisiteZone ||
                            (!worldMaterialOwnedByMenu &&
                             !worldMaterialOwnedByWeapon))
                            return RetailCensusError::MaterialNameInvalid;
                        worldMaterialNameResolved = false;
                    }
                    else
                    {
                        try
                        {
                            material.canonicalName = std::move(name);
                            material.name = *material.canonicalName;
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                        material.nameBlock4Offset = offset;
                        material.asset->info.name =
                            material.canonicalName->c_str();
                    }
                    stage = RetailCensusStage::WorldXModelMaterialTextures;
                    continue;
                }
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxMaterialNameBytes)
                        return RetailCensusError::MaterialNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::MaterialNameInvalid;
                if (bytes > limits.maxMaterialNameBytes)
                    return RetailCensusError::MaterialNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    material.nameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    material.name.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                    material.canonicalName =
                        std::make_shared<std::string>(material.name);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(material.name))
                    return RetailCensusError::MaterialNameInvalid;
                material.asset->info.name = material.canonicalName->c_str();
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::WorldXModelMaterialTextures;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelMaterialTextures)
            {
                RetailXModelMaterial &material = activeWorldMaterial();
                const std::size_t bytes =
                    static_cast<std::size_t>(material.textureCount) *
                    MATERIAL_TEXTURE_BYTES;
                if (bytes != 0u)
                {
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        material.textureTableBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    try
                    {
                        material.textures.resize(material.textureCount);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    for (std::size_t index = 0u;
                         index < material.textures.size(); ++index)
                    {
                        const std::uint8_t *record = inflated.data() + cursor +
                            index * MATERIAL_TEXTURE_BYTES;
                        RetailXModelMaterialTexture &texture =
                            material.textures[index];
                        texture.nameHash = ReadU32(record);
                        texture.nameStart = record[4u];
                        texture.nameEnd = record[5u];
                        texture.samplerState = record[6u];
                        texture.semantic = record[7u];
                        texture.imageReference = ReadU32(record + 8u);
                        if (texture.nameStart == 0u || texture.nameEnd == 0u ||
                            texture.semantic == 11u ||
                            texture.imageReference == 0u)
                        {
                            return RetailCensusError::MaterialTextureLayoutUnsupported;
                        }
                    }
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                worldTextureIndex = 0u;
                if (const RetailCensusError error = advanceWorldTextures(stage);
                    error != RetailCensusError::None)
                {
                    if (!worldMaterialTopLevel &&
                        mode == RetailCensusMode::WorldXModelLoader &&
                        error == RetailCensusError::XModelImageAliasInvalid &&
                        hasCompletedWorldMaterialTechnique())
                    {
                        activeWorldXModel().stoppedBeforeMaterialDependency = true;
                        finishWorldXModel(
                            "Load_GfxImage(alias)", true, false);
                        return RetailCensusError::None;
                    }
                    return error;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelImage)
            {
                RetailXModelImage &image = activeWorldMaterial().images.back();
                const int visit = visitRecord(GFX_IMAGE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                image.mapType = ReadU32(record);
                image.textureReference = ReadU32(record + 4u);
                image.nameReference = ReadU32(record + 32u);
                image.width = ReadU16(record + 24u);
                image.height = ReadU16(record + 26u);
                image.depth = ReadU16(record + 28u);
                const bool emptyBuiltin = image.mapType == 0u &&
                    image.textureReference == 0u && image.width == 0u &&
                    image.height == 0u && image.depth == 0u;
                const bool bounded2d = image.mapType == 3u &&
                    (image.textureReference == INLINE_POINTER ||
                     image.textureReference == SHARED_POINTER) &&
                    image.width != 0u && image.height != 0u && image.depth == 1u;
                if ((!emptyBuiltin && !bounded2d) || record[10u] > 1u ||
                    image.nameReference == 0u ||
                    image.nameReference == SHARED_POINTER)
                {
                    return RetailCensusError::ImageLayoutUnsupported;
                }
                GfxImage &canonical = *image.asset;
                canonical.mapType = static_cast<MapType>(image.mapType);
                canonical.texture.basemap = nullptr;
                canonical.picmip.platform[0u] = record[8u];
                canonical.picmip.platform[1u] = record[9u];
                canonical.noPicmip = record[10u] != 0u;
                canonical.semantic = record[11u];
                canonical.track = record[12u];
                canonical.cardMemory.platform[0u] = ReadS32(record + 16u);
                canonical.cardMemory.platform[1u] = ReadS32(record + 20u);
                canonical.width = image.width;
                canonical.height = image.height;
                canonical.depth = image.depth;
                canonical.category = record[30u];
                canonical.delayLoadPixels = record[31u] != 0u;
                cursor += GFX_IMAGE_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXModelImageName;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelImageName)
            {
                RetailXModelMaterial &material = activeWorldMaterial();
                RetailXModelImage &image = material.images.back();
                auto scheduleTexture = [&]() noexcept -> RetailCensusError {
                    if (image.textureReference != 0u)
                    {
                        if (image.textureReference == SHARED_POINTER)
                        {
                            ZoneSpan insert;
                            if (const RetailCensusError error = Plan(
                                    4u, 4u, &insert);
                                error != RetailCensusError::None) return error;
                            image.textureInsertPointerBlock4Offset = insert.offset;
                        }
                        if (const RetailCensusError error = Push(0u);
                            error != RetailCensusError::None) return error;
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(
                                4u, GFX_IMAGE_LOAD_DEF_BYTES, &span);
                            error != RetailCensusError::None) return error;
                        image.loadDefBlock0Offset = span.offset;
                        stage = RetailCensusStage::WorldXModelImageLoadDef;
                    }
                    return RetailCensusError::None;
                };
                if (image.nameReference != INLINE_POINTER)
                {
                    std::shared_ptr<std::string> name;
                    std::uint32_t offset = UINT32_MAX;
                    if (!ResolvePriorZoneStringPayload(
                            image.nameReference, name, offset) || !name ||
                        !ValidPublishedName(*name))
                    {
                        if (!prerequisiteZone ||
                            (!worldMaterialOwnedByMenu &&
                             !worldMaterialOwnedByWeapon) ||
                            !ValidPriorZonePointer(image.nameReference))
                        {
                            return RetailCensusError::ImageNameInvalid;
                        }
                        worldImageNameResolved = false;
                    }
                    else
                    {
                        image.canonicalName = std::move(name);
                        image.name = *image.canonicalName;
                        image.nameBlock4Offset = offset;
                        image.asset->name = image.canonicalName->c_str();
                        if (image.mapType == 0u && !image.name.starts_with(','))
                            return RetailCensusError::ImageNameInvalid;
                    }
                    if (const RetailCensusError error = scheduleTexture();
                        error != RetailCensusError::None) return error;
                    if (image.textureReference != 0u) continue;
                }
                else
                {
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxImageNameBytes)
                        return RetailCensusError::ImageNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::ImageNameInvalid;
                if (bytes > limits.maxImageNameBytes)
                    return RetailCensusError::ImageNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    image.nameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    image.name.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                    image.canonicalName =
                        std::make_shared<std::string>(image.name);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(image.name))
                    return RetailCensusError::ImageNameInvalid;
                image.asset->name = image.canonicalName->c_str();
                // Map-type zero names are engine-owned placeholders. Retail
                // data uses both ",$..." built-ins and comma-prefixed names
                // such as ",spotlight_lensflare"; neither has an inline loaddef.
                if (image.mapType == 0u && !image.name.starts_with(','))
                    return RetailCensusError::ImageNameInvalid;
                cursor += bytes;
                ++report.recordsProcessed;
                    if (const RetailCensusError error = scheduleTexture();
                        error != RetailCensusError::None) return error;
                    if (image.textureReference != 0u) continue;
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (worldImageNameResolved)
                {
                    if (const RetailCensusError error = MapRegistryError(
                            registry.RegisterAsset(
                                ASSET_TYPE_IMAGE,
                                (worldImageAliasSlot.block << 28u) |
                                    worldImageAliasSlot.offset,
                                image.name, image.identity));
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                worldImageAliasSlot, image.identity));
                        error != RetailCensusError::None) return error;
                    if (worldImageHasInsertAlias)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldImageInsertAliasSlot, image.identity));
                            error != RetailCensusError::None) return error;
                    }
                    image.published = true;
                    image.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    try { result.worldImages.push_back(image); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                worldImageHasInsertAlias = false;
                if (const RetailCensusError error =
                        retainResolvedWorldImage(image);
                    error != RetailCensusError::None) return error;
                RetailXModelMaterialTexture &texture =
                    material.textures[image.textureIndex];
                texture.imageIdentity = worldImageNameResolved
                    ? image.identity : 0u;
                texture.resolved = worldImageNameResolved;
                ++worldTextureIndex;
                if (const RetailCensusError error = advanceWorldTextures(stage);
                    error != RetailCensusError::None)
                {
                    if (!worldMaterialTopLevel &&
                        mode == RetailCensusMode::WorldXModelLoader &&
                        error == RetailCensusError::XModelImageAliasInvalid &&
                        hasCompletedWorldMaterialTechnique())
                    {
                        activeWorldXModel().stoppedBeforeMaterialDependency = true;
                        finishWorldXModel(
                            "Load_GfxImage(alias)", true, false);
                        return RetailCensusError::None;
                    }
                    return error;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelImageLoadDef)
            {
                RetailXModelImage &image = activeWorldMaterial().images.back();
                const int visit = visitRecord(GFX_IMAGE_LOAD_DEF_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::int32_t width = ReadS16(record + 2u);
                const std::int32_t height = ReadS16(record + 4u);
                const std::int32_t depth = ReadS16(record + 6u);
                const std::int32_t resourceBytes = ReadS32(record + 12u);
                if (!SupportedImageFormat(ReadU32(record + 8u)) ||
                    width != image.width || height != image.height ||
                    depth != image.depth || resourceBytes < 0)
                {
                    return resourceBytes < 0
                        ? RetailCensusError::ImageResourceSizeInvalid
                        : RetailCensusError::ImageLayoutUnsupported;
                }
                if (static_cast<std::uint32_t>(resourceBytes) >
                    limits.maxImageResourceBytes)
                {
                    return RetailCensusError::ImageResourceSizeLimit;
                }
                image.format = ReadU32(record + 8u);
                image.resourceBytes = static_cast<std::uint32_t>(resourceBytes);
                image.loadDefTraversed = true;
                worldImageResourceBytes = image.resourceBytes;
                cursor += GFX_IMAGE_LOAD_DEF_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(
                        1u, worldImageResourceBytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldXModelImageResource;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelImageResource)
            {
                const int visit = visitRecord(worldImageResourceBytes);
                if (visit <= 0) return RetailCensusError::None;
                RetailXModelMaterial &material =
                    activeWorldMaterial();
                RetailXModelImage &image = material.images.back();
                cursor += worldImageResourceBytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (worldImageNameResolved)
                {
                    if (const RetailCensusError error = MapRegistryError(
                            registry.RegisterAsset(
                                ASSET_TYPE_IMAGE,
                                (worldImageAliasSlot.block << 28u) |
                                    worldImageAliasSlot.offset,
                                image.name, image.identity));
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.PublishAlias(
                                worldImageAliasSlot, image.identity));
                        error != RetailCensusError::None) return error;
                    if (worldImageHasInsertAlias)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldImageInsertAliasSlot, image.identity));
                            error != RetailCensusError::None) return error;
                    }
                    image.published = true;
                    image.boundaryInflatedOffset =
                        static_cast<std::uint32_t>(cursor);
                    try { result.worldImages.push_back(image); }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                worldImageHasInsertAlias = false;
                if (const RetailCensusError error =
                        retainResolvedWorldImage(image);
                    error != RetailCensusError::None) return error;
                RetailXModelMaterialTexture &texture =
                    material.textures[image.textureIndex];
                texture.imageIdentity = worldImageNameResolved
                    ? image.identity : 0u;
                texture.resolved = worldImageNameResolved;
                ++worldTextureIndex;
                if (const RetailCensusError error = advanceWorldTextures(stage);
                    error != RetailCensusError::None)
                {
                    if (!worldMaterialTopLevel &&
                        mode == RetailCensusMode::WorldXModelLoader &&
                        error == RetailCensusError::XModelImageAliasInvalid &&
                        hasCompletedWorldMaterialTechnique())
                    {
                        activeWorldXModel().stoppedBeforeMaterialDependency = true;
                        finishWorldXModel(
                            "Load_GfxImage(alias)", true, false);
                        return RetailCensusError::None;
                    }
                    return error;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelMaterialConstants)
            {
                RetailXModelMaterial &material = activeWorldMaterial();
                const std::size_t bytes =
                    static_cast<std::size_t>(material.constantCount) *
                    MATERIAL_CONSTANT_BYTES;
                if (bytes != 0u)
                {
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(16u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        material.constantTableBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    for (std::size_t index = 0u;
                         index < material.constantCount; ++index)
                    {
                        const std::uint8_t *record = inflated.data() + cursor +
                            index * MATERIAL_CONSTANT_BYTES;
                        for (std::size_t component = 0u; component < 4u; ++component)
                        {
                            if (!std::isfinite(ReadF32(record + 16u + component * 4u)))
                                return RetailCensusError::MaterialLayoutUnsupported;
                        }
                    }
                    material.constantsHash = Fnv1a32(
                        std::span<const std::uint8_t>(
                            inflated.data() + cursor, bytes));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                stage = RetailCensusStage::WorldXModelMaterialStateBits;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelMaterialStateBits)
            {
                RetailXModelMaterial &material = activeWorldMaterial();
                const std::size_t bytes =
                    static_cast<std::size_t>(material.stateBitsCount) *
                    GFX_STATE_BITS_BYTES;
                if (bytes != 0u)
                {
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        material.stateBitsTableBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    material.stateBitsHash = Fnv1a32(
                        std::span<const std::uint8_t>(
                            inflated.data() + cursor, bytes));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (worldMaterialNameResolved)
                {
                    const std::uint32_t materialSource =
                        worldMaterialAliasSlot.block == 4u
                            ? (worldMaterialAliasSlot.block << 28u) |
                                worldMaterialAliasSlot.offset
                            : 0x80000000u |
                                worldMaterialPublicationSerial++;
                    if (const RetailCensusError error = MapRegistryError(
                            registry.RegisterAsset(
                                ASSET_TYPE_MATERIAL,
                                materialSource,
                                material.name, material.identity));
                        error != RetailCensusError::None) return error;
                    if (worldMaterialAliasSlot.block == 4u)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldMaterialAliasSlot,
                                    material.identity));
                            error != RetailCensusError::None) return error;
                    }
                    if (worldMaterialHasInsertAlias)
                    {
                        if (const RetailCensusError error = MapRegistryError(
                                registry.PublishAlias(
                                    worldMaterialInsertAliasSlot,
                                    material.identity));
                            error != RetailCensusError::None) return error;
                    }
                    material.published = true;
                    if (const RetailCensusError error =
                            retainResolvedWorldMaterial(material);
                        error != RetailCensusError::None) return error;
                }
                if (worldMaterialTopLevel)
                {
                    const std::uint32_t assetIndex =
                        worldMaterialTopLevelAssetIndex;
                    material.handleIndex = 0u;
                    worldMaterialTopLevel = false;
                    worldMaterialHasInsertAlias = false;
                    material.published = true;
                    if (const RetailCensusError error = AppendSemanticTrace(
                            kisak::database::SemanticTraceEventKind::AssetPublish,
                            ASSET_TYPE_MATERIAL,
                            assetIndex,
                            material.identity,
                            static_cast<std::uint32_t>(cursor),
                            {0u, material.headerBlock0Offset, MATERIAL_BYTES},
                            material.name,
                            worldMaterialAliasSlot);
                        error != RetailCensusError::None) return error;
                    ++result.completedAssetCount;
                    if (const RetailCensusError error =
                            dispatchSupportedWorldAsset(assetIndex + 1u, stage);
                        error != RetailCensusError::None) return error;
                    if (complete) return RetailCensusError::None;
                    continue;
                }
                if (worldMaterialOwnedByFx)
                {
                    RetailWorldFxElemDef &elem = activeWorldFxElem();
                    if (material.handleIndex >= elem.visualIdentities.size())
                        return RetailCensusError::FxElemVisualInvalid;
                    elem.visualIdentities[material.handleIndex] = material.identity;
                    try
                    {
                        block4MaterialVisualAliases.insert_or_assign(
                            worldMaterialAliasSlot.offset, material.identity);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    worldFxVisualIndex = material.handleIndex + 1u;
                    worldMaterialOwnedByFx = false;
                    worldMaterialHasInsertAlias = false;
                    stage = RetailCensusStage::WorldFxElemVisuals;
                    continue;
                }
                if (worldMaterialOwnedByWeapon)
                {
                    RetailPublishedWeaponDef &weapon = activeWorldWeapon();
                    AssignWeaponMaterial(
                        *weapon.asset, material.handleIndex,
                        material.asset.get());
                    worldMaterialOwnedByWeapon = false;
                    worldMaterialHasInsertAlias = false;
                    ++worldWeaponOperationIndex;
                    if (const RetailCensusError error =
                            ScheduleWorldWeaponOperation(stage);
                        error != RetailCensusError::None) return error;
                    continue;
                }
                if (worldMaterialOwnedByMenu)
                {
                    worldMaterialOwnedByMenu = false;
                    worldMaterialHasInsertAlias = false;
                    stage = RetailCensusStage::WorldMenuTasks;
                    continue;
                }
                RetailWorldXModel &model = activeWorldXModel();
                model.materialIdentities[material.handleIndex] = material.identity;
                worldMaterialHasInsertAlias = false;
                ++worldMaterialIndex;
                if (const RetailCensusError error = advanceWorldMaterials(stage);
                    error != RetailCensusError::None)
                {
                    if (mode == RetailCensusMode::WorldXModelLoader &&
                        error == RetailCensusError::XModelMaterialAliasInvalid &&
                        hasCompletedWorldMaterialTechnique())
                    {
                        activeWorldXModel().stoppedBeforeMaterialDependency = true;
                        finishWorldXModel(
                            "Load_Material(alias)", true, false);
                        return RetailCensusError::None;
                    }
                    return error;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelCollisionSurfaces)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.collisionSurfaceCount == 0u)
                {
                    model.collisionSurfacesTraversed = true;
                    stage = RetailCensusStage::WorldXModelBoneInfo;
                    continue;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(model.collisionSurfaceCount) *
                    XMODEL_COLLISION_SURFACE_BYTES;
                if (!Available(bytes))
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (bytes > limits.maxXModelCollisionPayloadBytes)
                    return RetailCensusError::XModelCollisionPayloadLimit;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(4u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    model.collisionSurfacesBlock4Offset = span.offset;
                    model.collisionPayloadBytes = static_cast<std::uint32_t>(bytes);
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    model.collisionSurfaces.resize(model.collisionSurfaceCount);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                std::uint64_t triangleCount = 0u;
                for (std::size_t index = 0u;
                     index < model.collisionSurfaces.size(); ++index)
                {
                    const std::uint8_t *record = inflated.data() + cursor +
                        index * XMODEL_COLLISION_SURFACE_BYTES;
                    RetailXModelCollisionSurface &surface =
                        model.collisionSurfaces[index];
                    surface.index = static_cast<std::uint32_t>(index);
                    surface.trianglesReference = ReadU32(record);
                    surface.triangleCount = ReadU32(record + 4u);
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                    {
                        surface.mins[axis] = ReadF32(record + 8u + axis * 4u);
                        surface.maxs[axis] = ReadF32(record + 20u + axis * 4u);
                        if (!std::isfinite(surface.mins[axis]) ||
                            !std::isfinite(surface.maxs[axis]) ||
                            surface.mins[axis] > surface.maxs[axis])
                        {
                            return RetailCensusError::XModelCollisionInvalid;
                        }
                    }
                    surface.boneIndex = ReadS32(record + 32u);
                    surface.contents = ReadS32(record + 36u);
                    surface.surfaceFlags = ReadS32(record + 40u);
                    if ((surface.triangleCount == 0u) !=
                            (surface.trianglesReference == 0u) ||
                        surface.boneIndex < 0 ||
                        surface.boneIndex >= model.numBones ||
                        (surface.triangleCount != 0u &&
                         surface.trianglesReference != INLINE_POINTER))
                    {
                        return RetailCensusError::XModelCollisionInvalid;
                    }
                    triangleCount += surface.triangleCount;
                    if (triangleCount > limits.maxXModelCollisionTriangles)
                        return RetailCensusError::XModelCollisionPayloadLimit;
                }
                model.collisionTriangleCount =
                    static_cast<std::uint32_t>(triangleCount);
                cursor += bytes;
                ++report.recordsProcessed;
                worldCollisionSurfaceIndex = 0u;
                stage = RetailCensusStage::WorldXModelCollisionTriangles;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelCollisionTriangles)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (worldCollisionSurfaceIndex >= model.collisionSurfaces.size())
                {
                    model.collisionSurfacesTraversed = true;
                    stage = RetailCensusStage::WorldXModelBoneInfo;
                    continue;
                }
                RetailXModelCollisionSurface &surface =
                    model.collisionSurfaces[worldCollisionSurfaceIndex];
                if (surface.triangleCount != 0u)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(surface.triangleCount) *
                        XMODEL_COLLISION_TRIANGLE_BYTES;
                    const std::uint64_t total =
                        static_cast<std::uint64_t>(model.collisionPayloadBytes) + bytes;
                    if (total > limits.maxXModelCollisionPayloadBytes ||
                        total > std::numeric_limits<std::uint32_t>::max())
                    {
                        return RetailCensusError::XModelCollisionPayloadLimit;
                    }
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        surface.trianglesBlock4Offset = span.offset;
                        model.collisionPayloadBytes =
                            static_cast<std::uint32_t>(total);
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    const std::size_t floatCount = bytes / sizeof(float);
                    for (std::size_t index = 0u; index < floatCount; ++index)
                    {
                        if (!std::isfinite(ReadF32(
                                inflated.data() + cursor + index * 4u)))
                        {
                            return RetailCensusError::XModelCollisionInvalid;
                        }
                    }
                    surface.trianglesHash = Fnv1a32(
                        std::span<const std::uint8_t>(
                            inflated.data() + cursor, bytes));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                surface.traversed = true;
                ++worldCollisionSurfaceIndex;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelBoneInfo)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.numBones != 0u)
                {
                    const std::size_t bytes =
                        static_cast<std::size_t>(model.numBones) * XBONE_INFO_BYTES;
                    if (model.boneInfoReference != INLINE_POINTER &&
                        model.boneInfoReference != SHARED_POINTER)
                    {
                        if (const RetailCensusError error =
                                ResolvePriorWorldXModelArray(
                                    model.boneInfoReference, bytes, 4u,
                                    &RetailWorldXModel::boneInfoData,
                                    &RetailWorldXModel::boneInfoBlock4Offset,
                                    model.boneInfoData,
                                    model.boneInfoBlock4Offset,
                                    RetailCensusError::XModelArrayAliasInvalid);
                            error != RetailCensusError::None)
                        {
                            return error;
                        }
                        model.boneInfoHash = Fnv1a32(
                            std::span<const std::uint8_t>(model.boneInfoData));
                        ++report.recordsProcessed;
                        model.boneInfoTraversed = true;
                        stage = RetailCensusStage::WorldXModelPhysPreset;
                        continue;
                    }
                    if (!Available(bytes))
                    {
                        blocked = true;
                        return RetailCensusError::None;
                    }
                    if (recordVisited == 0u)
                    {
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(4u, bytes, &span);
                            error != RetailCensusError::None) return error;
                        model.boneInfoBlock4Offset = span.offset;
                    }
                    const int visit = visitRecord(bytes);
                    if (visit <= 0) return RetailCensusError::None;
                    try
                    {
                        model.boneInfoData.assign(
                            inflated.begin() + static_cast<std::ptrdiff_t>(cursor),
                            inflated.begin() + static_cast<std::ptrdiff_t>(
                                cursor + bytes));
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    for (std::size_t bone = 0u; bone < model.numBones; ++bone)
                    {
                        const std::uint8_t *record = inflated.data() + cursor +
                            bone * XBONE_INFO_BYTES;
                        for (std::size_t value = 0u; value < 10u; ++value)
                        {
                            if (!std::isfinite(ReadF32(record + value * 4u)))
                                return RetailCensusError::XModelBoneInfoInvalid;
                        }
                        for (std::size_t axis = 0u; axis < 3u; ++axis)
                        {
                            if (ReadF32(record + axis * 4u) >
                                ReadF32(record + 12u + axis * 4u))
                            {
                                return RetailCensusError::XModelBoneInfoInvalid;
                            }
                        }
                        if (ReadF32(record + 36u) < 0.0f)
                            return RetailCensusError::XModelBoneInfoInvalid;
                    }
                    model.boneInfoHash = Fnv1a32(
                        std::span<const std::uint8_t>(model.boneInfoData));
                    cursor += bytes;
                    ++report.recordsProcessed;
                }
                model.boneInfoTraversed = true;
                stage = RetailCensusStage::WorldXModelPhysPreset;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelPhysPreset)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.physPresetReference != 0u)
                {
                    if (model.physPresetReference == INLINE_POINTER ||
                        model.physPresetReference == SHARED_POINTER)
                    {
                        if (const RetailCensusError error = Push(0u);
                            error != RetailCensusError::None) return error;
                        ZoneSpan span;
                        if (const RetailCensusError error = Plan(
                                4u, PHYS_PRESET_BYTES, &span);
                            error != RetailCensusError::None) return error;
                        model.physPreset.headerBlock0Offset = span.offset;
                        worldPhysPresetHasInsertAlias =
                            model.physPresetReference == SHARED_POINTER;
                        if (worldPhysPresetHasInsertAlias)
                        {
                            if (const RetailCensusError error = Push(4u);
                                error != RetailCensusError::None) return error;
                            if (const RetailCensusError error = Plan(
                                    4u, 4u, &worldPhysPresetInsertAliasSlot);
                                error != RetailCensusError::None) return error;
                            if (const RetailCensusError error = Pop();
                                error != RetailCensusError::None) return error;
                            model.physPreset.insertPointerBlock4Offset =
                                worldPhysPresetInsertAliasSlot.offset;
                            if (const RetailCensusError error = MapRegistryError(
                                    registry.ReserveAlias(
                                        worldPhysPresetInsertAliasSlot,
                                        ASSET_TYPE_PHYS_PRESET));
                                error != RetailCensusError::None)
                            {
                                return error;
                            }
                        }
                        stage = RetailCensusStage::WorldPhysPreset;
                        continue;
                    }
                    std::uint32_t identity = 0u;
                    if (ResolveRegistryAlias(
                            model.physPresetReference,
                            ASSET_TYPE_PHYS_PRESET, identity) !=
                        ZoneRegistryError::None)
                    {
                        if (!prerequisiteZone || !ValidPriorZonePointer(
                                model.physPresetReference, 4u))
                        {
                            return RetailCensusError::PhysPresetAliasInvalid;
                        }
                        model.physPresetTraversed = true;
                        stage = RetailCensusStage::WorldXModelPhysGeoms;
                        continue;
                    }
                    model.physPresetIdentity = identity;
                }
                model.physPresetTraversed = true;
                stage = RetailCensusStage::WorldXModelPhysGeoms;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysPreset)
            {
                RetailXModelPhysPreset &preset = activeWorldXModel().physPreset;
                const int visit = visitRecord(PHYS_PRESET_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                preset.nameReference = ReadU32(record);
                preset.type = ReadS32(record + 4u);
                preset.mass = ReadF32(record + 8u);
                preset.bounce = ReadF32(record + 12u);
                preset.friction = ReadF32(record + 16u);
                preset.bulletForceScale = ReadF32(record + 20u);
                preset.explosiveForceScale = ReadF32(record + 24u);
                preset.soundAliasPrefixReference = ReadU32(record + 28u);
                preset.piecesSpreadFraction = ReadF32(record + 32u);
                preset.piecesUpwardVelocity = ReadF32(record + 36u);
                preset.tempDefaultToCylinder = record[40u] != 0u;
                if (preset.nameReference != INLINE_POINTER ||
                    (preset.soundAliasPrefixReference != 0u &&
                     preset.soundAliasPrefixReference != INLINE_POINTER &&
                     !ValidPriorZonePointer(
                         preset.soundAliasPrefixReference, 1u)) ||
                    record[40u] > 1u || record[41u] != 0u ||
                    record[42u] != 0u || record[43u] != 0u)
                {
                    return RetailCensusError::PhysPresetLayoutUnsupported;
                }
                if (preset.soundAliasPrefixReference != 0u &&
                    preset.soundAliasPrefixReference != INLINE_POINTER)
                {
                    ZoneSpan target;
                    if (!DecodeZoneAliasToken(
                            preset.soundAliasPrefixReference, target) ||
                        target.block != 4u)
                        return RetailCensusError::PhysPresetAliasInvalid;
                    const auto prior = std::find_if(
                        result.worldXModels.begin(), result.worldXModels.end(),
                        [&](const RetailWorldXModel &entry) {
                            return entry.physPreset.published &&
                                entry.physPreset.soundAliasPrefixBlock4Offset ==
                                    target.offset;
                        });
                    if (prior == result.worldXModels.end())
                    {
                        if (!prerequisiteZone)
                            return RetailCensusError::PhysPresetAliasInvalid;
                        preset.soundAliasPrefix.clear();
                    }
                    else
                    {
                        try
                        {
                            preset.soundAliasPrefix =
                                prior->physPreset.soundAliasPrefix;
                        }
                        catch (...) { return RetailCensusError::AllocationFailed; }
                    }
                }
                const std::array<float, 7> values{{
                    preset.mass,
                    preset.bounce,
                    preset.friction,
                    preset.bulletForceScale,
                    preset.explosiveForceScale,
                    preset.piecesSpreadFraction,
                    preset.piecesUpwardVelocity,
                }};
                if (!std::all_of(values.begin(), values.end(),
                        [](float value) { return std::isfinite(value); }))
                {
                    return RetailCensusError::PhysPresetValuesInvalid;
                }
                cursor += PHYS_PRESET_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::WorldPhysPresetName;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysPresetName)
            {
                RetailXModelPhysPreset &preset = activeWorldXModel().physPreset;
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxPhysPresetNameBytes)
                        return RetailCensusError::PhysPresetNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::PhysPresetNameInvalid;
                if (bytes > limits.maxPhysPresetNameBytes)
                    return RetailCensusError::PhysPresetNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    preset.nameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    preset.name.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(preset.name))
                    return RetailCensusError::PhysPresetNameInvalid;
                cursor += bytes;
                ++report.recordsProcessed;
                if (preset.soundAliasPrefixReference != INLINE_POINTER)
                {
                    if (preset.soundAliasPrefixReference == 0u)
                        preset.soundAliasPrefix.clear();
                    if (const RetailCensusError error =
                            publishWorldPhysPreset(stage);
                        error != RetailCensusError::None) return error;
                }
                else
                {
                    stage = RetailCensusStage::WorldPhysPresetSoundAlias;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysPresetSoundAlias)
            {
                RetailWorldXModel &model = activeWorldXModel();
                RetailXModelPhysPreset &preset = model.physPreset;
                const auto begin = inflated.begin() +
                    static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >=
                        limits.maxPhysPresetSoundAliasBytes)
                    {
                        return RetailCensusError::PhysPresetSoundAliasTooLong;
                    }
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxPhysPresetSoundAliasBytes)
                    return RetailCensusError::PhysPresetSoundAliasTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    preset.soundAliasPrefixBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    preset.soundAliasPrefix.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor),
                        bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!preset.soundAliasPrefix.empty() &&
                    !ValidPublishedName(preset.soundAliasPrefix))
                {
                    return RetailCensusError::PhysPresetSoundAliasInvalid;
                }
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error =
                        publishWorldPhysPreset(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelPhysGeoms)
            {
                RetailWorldXModel &model = activeWorldXModel();
                if (model.physGeomsReference == 0u)
                {
                    model.physGeomsTraversed = true;
                    stage = RetailCensusStage::WorldXModelPublish;
                    continue;
                }
                if (model.physGeomsReference != INLINE_POINTER)
                {
                    if (!ValidPriorZonePointer(model.physGeomsReference, 4u))
                        return RetailCensusError::PhysGeomLayoutUnsupported;
                    model.physGeomsTraversed = true;
                    stage = RetailCensusStage::WorldXModelPublish;
                    continue;
                }
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, PHYS_GEOM_LIST_BYTES, &span);
                    error != RetailCensusError::None) return error;
                model.physGeomHeaderBlock4Offset = span.offset;
                model.physGeomPayloadBytes = 0u;
                worldPhysGeomInfos.clear();
                worldPhysGeomSidePlaneReferences.clear();
                worldPhysGeomIndex = 0u;
                stage = RetailCensusStage::WorldPhysGeomList;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysGeomList)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const int visit = visitRecord(PHYS_GEOM_LIST_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                model.physGeomCount = ReadU32(record);
                worldPhysGeomInfosReference = ReadU32(record + 4u);
                if (model.physGeomCount > limits.maxXModelPhysGeoms)
                    return RetailCensusError::PhysGeomCountLimit;
                if ((model.physGeomCount == 0u) !=
                    (worldPhysGeomInfosReference == 0u) ||
                    (model.physGeomCount != 0u &&
                     worldPhysGeomInfosReference != INLINE_POINTER))
                {
                    return RetailCensusError::PhysGeomLayoutUnsupported;
                }
                for (std::size_t index = 0u; index < 9u; ++index)
                {
                    if (!std::isfinite(ReadF32(record + 8u + index * 4u)))
                        return RetailCensusError::PhysGeomValuesInvalid;
                }
                cursor += PHYS_GEOM_LIST_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error =
                        addPhysGeomPayload(PHYS_GEOM_LIST_BYTES);
                    error != RetailCensusError::None) return error;
                if (model.physGeomCount == 0u)
                {
                    model.physGeomsTraversed = true;
                    stage = RetailCensusStage::WorldXModelPublish;
                    continue;
                }
                const std::uint64_t bytes =
                    static_cast<std::uint64_t>(model.physGeomCount) *
                    PHYS_GEOM_INFO_BYTES;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, bytes, &span);
                    error != RetailCensusError::None) return error;
                model.physGeomInfosBlock4Offset = span.offset;
                stage = RetailCensusStage::WorldPhysGeomInfos;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysGeomInfos)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const std::size_t bytes =
                    static_cast<std::size_t>(model.physGeomCount) *
                    PHYS_GEOM_INFO_BYTES;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try { worldPhysGeomInfos.resize(model.physGeomCount); }
                catch (...) { return RetailCensusError::AllocationFailed; }
                for (std::size_t index = 0u; index < model.physGeomCount; ++index)
                {
                    const std::uint8_t *record = inflated.data() + cursor +
                        index * PHYS_GEOM_INFO_BYTES;
                    WorldPhysGeomInfoState &geom = worldPhysGeomInfos[index];
                    geom.brushReference = ReadU32(record);
                    geom.type = ReadS32(record + 4u);
                    if (geom.type < 0 || geom.type >= 6 ||
                        (geom.brushReference == SHARED_POINTER))
                        return RetailCensusError::PhysGeomLayoutUnsupported;
                    for (std::size_t value = 0u; value < 15u; ++value)
                    {
                        if (!std::isfinite(ReadF32(record + 8u + value * 4u)))
                            return RetailCensusError::PhysGeomValuesInvalid;
                    }
                }
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = addPhysGeomPayload(bytes);
                    error != RetailCensusError::None) return error;
                worldPhysGeomIndex = 0u;
                if (const RetailCensusError error =
                        scheduleWorldPhysGeomBrush(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysGeomBrush)
            {
                RetailWorldXModel &model = activeWorldXModel();
                const int visit = visitRecord(BRUSH_WRAPPER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                {
                    const float minimum = ReadF32(record + axis * 4u);
                    const float maximum = ReadF32(record + 16u + axis * 4u);
                    if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
                        minimum > maximum)
                        return RetailCensusError::PhysGeomBrushInvalid;
                }
                worldPhysGeomSideCount = ReadU32(record + 28u);
                worldPhysGeomSidesReference = ReadU32(record + 32u);
                worldPhysGeomAdjacentReference = ReadU32(record + 48u);
                const std::int32_t edgeCount = ReadS32(record + 72u);
                worldPhysGeomPlanesReference = ReadU32(record + 76u);
                if (worldPhysGeomSideCount > limits.maxPhysGeomBrushSides ||
                    edgeCount < 0 ||
                    static_cast<std::uint32_t>(edgeCount) >
                        limits.maxPhysGeomBrushEdges)
                    return RetailCensusError::PhysGeomCountLimit;
                worldPhysGeomEdgeCount = static_cast<std::uint32_t>(edgeCount);
                if ((worldPhysGeomSideCount == 0u) !=
                        (worldPhysGeomSidesReference == 0u) ||
                    (worldPhysGeomEdgeCount == 0u) !=
                        (worldPhysGeomAdjacentReference == 0u) ||
                    (worldPhysGeomSideCount == 0u) !=
                        (worldPhysGeomPlanesReference == 0u) ||
                    (worldPhysGeomSidesReference != 0u &&
                        worldPhysGeomSidesReference != INLINE_POINTER) ||
                    (worldPhysGeomAdjacentReference != 0u &&
                        worldPhysGeomAdjacentReference != INLINE_POINTER))
                    return RetailCensusError::PhysGeomBrushInvalid;
                cursor += BRUSH_WRAPPER_BYTES;
                ++report.recordsProcessed;
                ++model.physGeomBrushCount;
                model.physGeomBrushSideCount += worldPhysGeomSideCount;
                model.physGeomEdgeCount += worldPhysGeomEdgeCount;
                if (const RetailCensusError error =
                        addPhysGeomPayload(BRUSH_WRAPPER_BYTES);
                    error != RetailCensusError::None) return error;
                if (worldPhysGeomSideCount != 0u)
                {
                    if (const RetailCensusError error = Plan(
                            4u, static_cast<std::uint64_t>(worldPhysGeomSideCount) *
                                BRUSH_SIDE_BYTES);
                        error != RetailCensusError::None) return error;
                    stage = RetailCensusStage::WorldPhysGeomBrushSides;
                }
                else
                {
                    stage = RetailCensusStage::WorldPhysGeomBrushAdjacent;
                }
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysGeomBrushSides)
            {
                const std::size_t bytes =
                    static_cast<std::size_t>(worldPhysGeomSideCount) *
                    BRUSH_SIDE_BYTES;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try { worldPhysGeomSidePlaneReferences.resize(worldPhysGeomSideCount); }
                catch (...) { return RetailCensusError::AllocationFailed; }
                for (std::size_t index = 0u; index < worldPhysGeomSideCount; ++index)
                {
                    const std::uint8_t *record = inflated.data() + cursor +
                        index * BRUSH_SIDE_BYTES;
                    worldPhysGeomSidePlaneReferences[index] = ReadU32(record);
                    if (record[11u] != 0u)
                        return RetailCensusError::PhysGeomBrushInvalid;
                }
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = addPhysGeomPayload(bytes);
                    error != RetailCensusError::None) return error;
                worldPhysGeomSideIndex = 0u;
                stage = RetailCensusStage::WorldPhysGeomBrushSidePlane;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysGeomBrushSidePlane)
            {
                while (worldPhysGeomSideIndex <
                    worldPhysGeomSidePlaneReferences.size())
                {
                    const std::uint32_t token =
                        worldPhysGeomSidePlaneReferences[worldPhysGeomSideIndex];
                    if (token == 0u) { ++worldPhysGeomSideIndex; continue; }
                    if (token != INLINE_POINTER)
                    {
                        if (!ValidPriorZonePointer(token, 4u))
                            return RetailCensusError::PhysGeomBrushInvalid;
                        ++worldPhysGeomSideIndex;
                        continue;
                    }
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(
                                4u, COLLISION_PLANE_BYTES);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(COLLISION_PLANE_BYTES);
                    if (visit <= 0) return RetailCensusError::None;
                    const std::uint8_t *record = inflated.data() + cursor;
                    for (std::size_t value = 0u; value < 4u; ++value)
                        if (!std::isfinite(ReadF32(record + value * 4u)))
                            return RetailCensusError::PhysGeomValuesInvalid;
                    if (record[16u] > 3u || record[17u] > 7u ||
                        record[18u] != 0u || record[19u] != 0u)
                        return RetailCensusError::PhysGeomBrushInvalid;
                    cursor += COLLISION_PLANE_BYTES;
                    ++report.recordsProcessed;
                    ++activeWorldXModel().physGeomPlaneCount;
                    if (const RetailCensusError error =
                            addPhysGeomPayload(COLLISION_PLANE_BYTES);
                        error != RetailCensusError::None) return error;
                    ++worldPhysGeomSideIndex;
                }
                stage = RetailCensusStage::WorldPhysGeomBrushAdjacent;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysGeomBrushAdjacent)
            {
                if (worldPhysGeomEdgeCount != 0u)
                {
                    if (recordVisited == 0u)
                    {
                        if (const RetailCensusError error = Plan(
                                1u, worldPhysGeomEdgeCount);
                            error != RetailCensusError::None) return error;
                    }
                    const int visit = visitRecord(worldPhysGeomEdgeCount);
                    if (visit <= 0) return RetailCensusError::None;
                    cursor += worldPhysGeomEdgeCount;
                    ++report.recordsProcessed;
                    if (const RetailCensusError error =
                            addPhysGeomPayload(worldPhysGeomEdgeCount);
                        error != RetailCensusError::None) return error;
                }
                if (worldPhysGeomPlanesReference == INLINE_POINTER)
                {
                    if (const RetailCensusError error = Plan(
                            4u, static_cast<std::uint64_t>(worldPhysGeomSideCount) *
                                COLLISION_PLANE_BYTES);
                        error != RetailCensusError::None) return error;
                    stage = RetailCensusStage::WorldPhysGeomBrushPlanes;
                    continue;
                }
                if (worldPhysGeomPlanesReference != 0u &&
                    !ValidPriorZonePointer(worldPhysGeomPlanesReference, 4u))
                    return RetailCensusError::PhysGeomBrushInvalid;
                ++worldPhysGeomIndex;
                if (const RetailCensusError error =
                        scheduleWorldPhysGeomBrush(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldPhysGeomBrushPlanes)
            {
                const std::size_t bytes =
                    static_cast<std::size_t>(worldPhysGeomSideCount) *
                    COLLISION_PLANE_BYTES;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                for (std::size_t index = 0u; index < worldPhysGeomSideCount; ++index)
                {
                    const std::uint8_t *record = inflated.data() + cursor +
                        index * COLLISION_PLANE_BYTES;
                    for (std::size_t value = 0u; value < 4u; ++value)
                        if (!std::isfinite(ReadF32(record + value * 4u)))
                            return RetailCensusError::PhysGeomValuesInvalid;
                    if (record[16u] > 3u || record[17u] > 7u ||
                        record[18u] != 0u || record[19u] != 0u)
                        return RetailCensusError::PhysGeomBrushInvalid;
                }
                cursor += bytes;
                ++report.recordsProcessed;
                activeWorldXModel().physGeomPlaneCount += worldPhysGeomSideCount;
                if (const RetailCensusError error = addPhysGeomPayload(bytes);
                    error != RetailCensusError::None) return error;
                ++worldPhysGeomIndex;
                if (const RetailCensusError error =
                        scheduleWorldPhysGeomBrush(stage);
                    error != RetailCensusError::None) return error;
                continue;
            }
            if (stage == RetailCensusStage::WorldXModelPublish)
            {
                RetailWorldXModel &model = activeWorldXModel();
                XModel &canonical = *model.asset;
                canonical.boneNames = model.boneNameScriptStringIndices.empty()
                    ? nullptr
                    : model.boneNameScriptStringIndices.data();
                canonical.parentList = model.parentList.empty()
                    ? nullptr : model.parentList.data();
                canonical.quats = model.quats.empty() ? nullptr : model.quats.data();
                canonical.trans = model.trans.empty() ? nullptr : model.trans.data();
                canonical.partClassification = model.partClassification.empty()
                    ? nullptr : model.partClassification.data();
                canonical.baseMat = model.baseMat.empty()
                    ? nullptr
                    : reinterpret_cast<DObjAnimMat *>(model.baseMat.data());
                canonical.boneInfo = model.boneInfoData.empty()
                    ? nullptr
                    : reinterpret_cast<XBoneInfo *>(model.boneInfoData.data());
                if (!model.materialIdentities.empty())
                {
                    try
                    {
                        model.canonicalMaterialHandles =
                            std::make_shared<std::vector<Material *>>(
                                model.materialIdentities.size(), nullptr);
                    }
                    catch (...) { return RetailCensusError::AllocationFailed; }
                    for (std::size_t index = 0u;
                         index < model.materialIdentities.size(); ++index)
                    {
                        const RetailXModelMaterial *material =
                            findPublishedWorldMaterial(
                                model.materialIdentities[index]);
                        if (material == nullptr || !material->asset)
                        {
                            if (prerequisiteZone) continue;
                            return RetailCensusError::XModelMaterialAliasInvalid;
                        }
                        (*model.canonicalMaterialHandles)[index] =
                            material->asset.get();
                    }
                    canonical.materialHandles =
                        model.canonicalMaterialHandles->data();
                }
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_XMODEL, model.registrySourceIndex,
                            model.name, model.identity));
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.PublishAlias(
                            worldXModelAliasSlot, model.identity));
                    error != RetailCensusError::None) return error;
                model.published = true;
                model.boundaryInflatedOffset = static_cast<std::uint32_t>(cursor);
                if (worldXModelNested)
                {
                    RetailWorldFxElemDef &elem = result.worldFxEffects[worldFxIndex]
                        .elemDefs[worldFxElemIndex];
                    if (worldXModelNestedFxVisualIndex >=
                        elem.visualIdentities.size())
                    {
                        return RetailCensusError::FxElemVisualInvalid;
                    }
                    elem.visualIdentities[worldXModelNestedFxVisualIndex] =
                        model.identity;
                    ++worldFxVisualIndex;
                    worldXModelNested = false;
                    result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                    result.block4CursorAtBoundary = arenas.Cursor(4u);
                    result.registryAssetCount = registry.AssetCount();
                    result.registryAliasCount = registry.AliasCount();
                    result.registryDefinedAliasCount =
                        registry.DefinedAliasCount();
                    stage = RetailCensusStage::WorldFxElemVisuals;
                    continue;
                }
                if (const RetailCensusError error = AppendSemanticTrace(
                        kisak::database::SemanticTraceEventKind::AssetPublish,
                        ASSET_TYPE_XMODEL,
                        model.assetIndex,
                        model.identity,
                        model.boundaryInflatedOffset,
                        {0u, model.headerBlock0Offset, XMODEL_BYTES},
                        model.name,
                        worldXModelAliasSlot);
                    error != RetailCensusError::None)
                {
                    return error;
                }
                ++result.completedAssetCount;
                result.worldNextAssetIndex = model.assetIndex + 1u;
                result.nextBodyIndex = result.worldNextAssetIndex;
                if (result.nextBodyIndex < worldAssetTypes.size())
                {
                    result.nextBodyType = worldAssetTypes[result.nextBodyIndex];
                    result.nextBodyReference =
                        worldAssetReferences[result.nextBodyIndex];
                }
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.worldRegistryAliasCount = registry.AliasCount();
                result.worldRegistryDefinedAliasCount = registry.DefinedAliasCount();
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                result.stoppedBeforeWorldXModelDependency = false;
                result.stoppedBeforeDifferentWorldAssetType = false;
                result.unsupportedOperation = nullptr;
                if (mode == RetailCensusMode::WorldSecondXModelDependencies &&
                    worldXModelIndex != 0u)
                {
                    stage = RetailCensusStage::AssetBoundary;
                    complete = true;
                    return RetailCensusError::None;
                }
                if (mode == RetailCensusMode::WorldXModelLoader)
                {
                    if (const RetailCensusError error =
                            dispatchSupportedWorldAsset(
                                result.nextBodyIndex, stage);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    if (complete) return RetailCensusError::None;
                    continue;
                }
                if ((mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                     mode == RetailCensusMode::WorldSecondXModelPrefix ||
                     mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                     mode == RetailCensusMode::WorldSecondXModelDependencies) &&
                    result.nextBodyIndex < worldAssetTypes.size() &&
                    result.nextBodyType == ASSET_TYPE_TECHNIQUE_SET &&
                    result.nextBodyReference == INLINE_POINTER)
                {
                    worldBodyIndex = result.nextBodyIndex;
                    if (const RetailCensusError error = BeginWorldTechniqueSet(stage);
                        error != RetailCensusError::None)
                    {
                        return error;
                    }
                    continue;
                }
                if (mode == RetailCensusMode::WorldPostXModelTechniqueSet ||
                    mode == RetailCensusMode::WorldSecondXModelPrefix ||
                    mode == RetailCensusMode::WorldSecondXSurfacePrefix ||
                    mode == RetailCensusMode::WorldSecondXModelDependencies)
                    return RetailCensusError::PostXModelAssetUnsupported;
                stage = RetailCensusStage::AssetBoundary;
                complete = true;
                return RetailCensusError::None;
            }
            if (stage == RetailCensusStage::TechniqueSet)
            {
                const int visit = visitRecord(TECHNIQUE_SET_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t nameToken = ReadU32(record);
                if (nameToken != INLINE_POINTER || record[5] != 0u ||
                    record[6] != 0u || record[7] != 0u || ReadU32(record + 8u) != 0u)
                    return RetailCensusError::TechniqueSetLayoutUnsupported;
                for (std::uint32_t index = 0u; index < techniqueTokens.size(); ++index)
                    techniqueTokens[index] = ReadU32(record + 12u + index * 4u);
                cursor += TECHNIQUE_SET_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::TechniqueSetName;
                continue;
            }
            if (stage == RetailCensusStage::TechniqueSetName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxTechniqueNameBytes)
                        return RetailCensusError::TechniqueSetNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxTechniqueNameBytes)
                    return RetailCensusError::TechniqueSetNameTooLong;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                if (bytes <= 1u) return RetailCensusError::TechniqueSetNameInvalid;
                try
                {
                    result.techniqueSetName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                std::uint32_t populated = 0u;
                for (std::uint32_t index = 0u; index < techniqueTokens.size(); ++index)
                {
                    const std::uint32_t token = techniqueTokens[index];
                    if (token == 0u) continue;
                    if (token != INLINE_POINTER)
                        return RetailCensusError::TechniqueReferenceUnsupported;
                    if (populated++ == 0u) result.firstTechniqueSlot = index;
                }
                if (populated != 1u) return RetailCensusError::TechniqueReferenceUnsupported;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, TECHNIQUE_HEADER_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.techniqueBlock4Offset = span.offset;
                stage = RetailCensusStage::Technique;
                continue;
            }
            if (stage == RetailCensusStage::Technique)
            {
                const int visit = visitRecord(TECHNIQUE_HEADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t techniqueNameToken = ReadU32(record);
                const std::uint32_t passCount =
                    static_cast<std::uint32_t>(record[6]) |
                    static_cast<std::uint32_t>(record[7]) << 8u;
                if (techniqueNameToken != INLINE_POINTER || passCount == 0u)
                    return RetailCensusError::TechniqueLayoutUnsupported;
                if (passCount > limits.maxTechniquePasses)
                    return RetailCensusError::TechniquePassCountLimit;
                result.techniquePassCount = passCount;
                materialPassBytes = passCount * MATERIAL_PASS_BYTES;
                cursor += TECHNIQUE_HEADER_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(1u, materialPassBytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::MaterialPasses;
                continue;
            }
            if (stage == RetailCensusStage::MaterialPasses)
            {
                const int visit = visitRecord(materialPassBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                pixelShaderToken = ReadU32(record + 8u);
                materialArgumentToken = ReadU32(record + 16u);
                const std::uint32_t argumentCount = static_cast<std::uint32_t>(record[12]) +
                    static_cast<std::uint32_t>(record[13]) +
                    static_cast<std::uint32_t>(record[14]);
                if (result.techniquePassCount != 1u ||
                    ReadU32(record) != INLINE_POINTER ||
                    ReadU32(record + 4u) != INLINE_POINTER ||
                    pixelShaderToken != INLINE_POINTER ||
                    materialArgumentToken != INLINE_POINTER ||
                    record[12] > 64u || record[13] > 64u || record[14] > 64u)
                    return RetailCensusError::MaterialPassUnsupported;
                if (argumentCount == 0u || argumentCount > UINT32_MAX / MATERIAL_ARGUMENT_BYTES)
                    return RetailCensusError::ShaderArgumentLayoutUnsupported;
                result.shaderArgumentCount = argumentCount;
                materialArgumentBytes = argumentCount * MATERIAL_ARGUMENT_BYTES;
                cursor += materialPassBytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, VERTEX_DECLARATION_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.vertexDeclarationBlock4Offset = span.offset;
                stage = RetailCensusStage::VertexDeclaration;
                continue;
            }
            if (stage == RetailCensusStage::VertexDeclaration)
            {
                const int visit = visitRecord(VERTEX_DECLARATION_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                if (record[0] == 0u || record[0] > 16u || record[1] > 1u ||
                    record[2] != 0u || record[3] != 0u ||
                    std::any_of(record + 36u, record + 100u,
                        [](std::uint8_t byte) { return byte != 0u; }))
                    return RetailCensusError::VertexDeclarationUnsupported;
                result.vertexStreamCount = record[0];
                std::copy(record + 4u, record + 36u,
                    result.vertexStreamRouting.begin());
                result.vertexStreamRoutingHash = Fnv1a32(
                    std::span<const std::uint8_t>(record + 4u, 32u));
                result.vertexDeclarationPrepared = true;
                cursor += VERTEX_DECLARATION_BYTES;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, VERTEX_SHADER_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.vertexShaderBlock4Offset = span.offset;
                stage = RetailCensusStage::VertexShader;
                continue;
            }
            if (stage == RetailCensusStage::VertexShader)
            {
                const int visit = visitRecord(VERTEX_SHADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t vertexShaderNameToken = ReadU32(record);
                const std::uint32_t runtimeShader = ReadU32(record + 4u);
                const std::uint32_t programToken = ReadU32(record + 8u);
                const std::uint32_t programDwords = ReadU16(record + 12u);
                const std::uint32_t loadForRenderer = ReadU16(record + 14u);
                if (vertexShaderNameToken != INLINE_POINTER || runtimeShader != 0u ||
                    programToken != INLINE_POINTER || loadForRenderer != 0u)
                    return RetailCensusError::VertexShaderLayoutUnsupported;
                if (programDwords == 0u) return RetailCensusError::ShaderProgramSizeInvalid;
                if (programDwords > limits.maxShaderProgramDwords ||
                    programDwords > UINT32_MAX / 4u)
                    return RetailCensusError::ShaderProgramSizeLimit;
                result.vertexShaderProgramDwords = programDwords;
                vertexShaderProgramBytes = programDwords * 4u;
                cursor += VERTEX_SHADER_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::VertexShaderName;
                continue;
            }
            if (stage == RetailCensusStage::VertexShaderName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxShaderNameBytes)
                        return RetailCensusError::VertexShaderNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxShaderNameBytes)
                    return RetailCensusError::VertexShaderNameTooLong;
                ZoneSpan nameSpan;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes, &nameSpan);
                        error != RetailCensusError::None) return error;
                    vertexShaderNameBlock4Offset = nameSpan.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                if (bytes <= 1u) return RetailCensusError::VertexShaderNameInvalid;
                try
                {
                    result.vertexShaderName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, vertexShaderProgramBytes, &span);
                    error != RetailCensusError::None) return error;
                result.vertexShaderProgramBlock4Offset = span.offset;
                stage = RetailCensusStage::VertexShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::VertexShaderProgram)
            {
                const int visit = visitRecord(vertexShaderProgramBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const auto program = std::span<const std::uint8_t>(
                    record, vertexShaderProgramBytes);
                const kisak::web::ShaderDecodeError decodeError =
                    kisak::web::DecodeD3D9Shader(program,
                        {limits.maxShaderProgramDwords, 4096u, 64u,
                            limits.maxShaderNameBytes}, vertexContract);
                if (decodeError != kisak::web::ShaderDecodeError::None)
                    return decodeError == kisak::web::ShaderDecodeError::InvalidVersion
                        ? RetailCensusError::ShaderProgramSignatureInvalid
                        : RetailCensusError::ShaderContractInvalid;
                if (vertexContract.stage != kisak::web::ShaderStage::Vertex)
                    return RetailCensusError::ShaderProgramSignatureInvalid;
                result.vertexShaderProgramHash = vertexContract.programHash;
                result.vertexShaderInstructionCount = vertexContract.instructionCount;
                result.vertexShaderConstantCount = static_cast<std::uint32_t>(
                    vertexContract.constants.size());
                cursor += vertexShaderProgramBytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, PIXEL_SHADER_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.pixelShaderBlock4Offset = span.offset;
                stage = RetailCensusStage::PixelShader;
                continue;
            }
            if (stage == RetailCensusStage::PixelShader)
            {
                const int visit = visitRecord(PIXEL_SHADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t nameToken = ReadU32(record);
                const std::uint32_t runtimeShader = ReadU32(record + 4u);
                const std::uint32_t programToken = ReadU32(record + 8u);
                const std::uint32_t programDwords = ReadU16(record + 12u);
                const std::uint32_t loadForRenderer = ReadU16(record + 14u);
                const std::uint32_t expectedNameToken = 0x40000001u +
                    vertexShaderNameBlock4Offset;
                if (pixelShaderToken != INLINE_POINTER || nameToken != expectedNameToken ||
                    runtimeShader != 0u || programToken != INLINE_POINTER ||
                    loadForRenderer != 0u)
                    return RetailCensusError::PixelShaderLayoutUnsupported;
                if (programDwords == 0u) return RetailCensusError::ShaderProgramSizeInvalid;
                if (programDwords > limits.maxShaderProgramDwords ||
                    programDwords > UINT32_MAX / 4u)
                    return RetailCensusError::ShaderProgramSizeLimit;
                result.pixelShaderName = result.vertexShaderName;
                result.pixelShaderProgramDwords = programDwords;
                pixelShaderProgramBytes = programDwords * 4u;
                cursor += PIXEL_SHADER_BYTES;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, pixelShaderProgramBytes, &span);
                    error != RetailCensusError::None) return error;
                result.pixelShaderProgramBlock4Offset = span.offset;
                stage = RetailCensusStage::PixelShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::PixelShaderProgram)
            {
                const int visit = visitRecord(pixelShaderProgramBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const kisak::web::ShaderDecodeError decodeError =
                    kisak::web::DecodeD3D9Shader(
                        std::span<const std::uint8_t>(record, pixelShaderProgramBytes),
                        {limits.maxShaderProgramDwords, 4096u, 64u,
                            limits.maxShaderNameBytes}, pixelContract);
                if (decodeError != kisak::web::ShaderDecodeError::None ||
                    pixelContract.stage != kisak::web::ShaderStage::Pixel)
                    return RetailCensusError::ShaderContractInvalid;
                result.pixelShaderProgramHash = pixelContract.programHash;
                result.pixelShaderInstructionCount = pixelContract.instructionCount;
                result.pixelShaderConstantCount = static_cast<std::uint32_t>(
                    pixelContract.constants.size());
                kisak::web::WebGL2ShaderSubstitution substitution;
                if (!kisak::web::SelectWebGL2ShaderSubstitution(
                        vertexContract, pixelContract,
                        result.vertexStreamRoutingHash, substitution))
                    return RetailCensusError::ShaderSubstitutionUnsupported;
                try { result.shaderSubstitutionId = substitution.id; }
                catch (...) { return RetailCensusError::AllocationFailed; }
                result.vertexGlslHash = substitution.vertexSourceHash;
                result.fragmentGlslHash = substitution.fragmentSourceHash;
                result.shaderCompatibilitySelected = true;
                cursor += pixelShaderProgramBytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, materialArgumentBytes, &span);
                    error != RetailCensusError::None) return error;
                result.shaderArgumentsBlock4Offset = span.offset;
                stage = RetailCensusStage::ShaderArguments;
                continue;
            }
            if (stage == RetailCensusStage::ShaderArguments)
            {
                const int visit = visitRecord(materialArgumentBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                for (std::uint32_t index = 0u; index < result.shaderArgumentCount; ++index)
                {
                    const std::uint16_t type = static_cast<std::uint16_t>(record[index * 8u]) |
                        static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(record[index * 8u + 1u]) << 8u);
                    if (type != 2u && type != 3u)
                        return RetailCensusError::ShaderArgumentLayoutUnsupported;
                }
                result.shaderArgumentHash = Fnv1a32(
                    std::span<const std::uint8_t>(record, materialArgumentBytes));
                cursor += materialArgumentBytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::TechniqueName;
                continue;
            }
            if (stage == RetailCensusStage::TechniqueName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxTechniqueNameBytes)
                        return RetailCensusError::TechniqueSetNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::TechniqueNameInvalid;
                if (bytes > limits.maxTechniqueNameBytes)
                    return RetailCensusError::TechniqueSetNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan nameSpan;
                    if (const RetailCensusError error = Plan(1u, bytes, &nameSpan);
                        error != RetailCensusError::None) return error;
                    result.techniqueNameBlock4Offset = nameSpan.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    result.techniqueName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_TECHNIQUE_SET, 0u,
                            result.techniqueSetName,
                            result.compatibilityTechniqueSetIdentity));
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.PublishAlias(
                            topLevelAliasSlots[0],
                            result.compatibilityTechniqueSetIdentity));
                    error != RetailCensusError::None) return error;
                result.completedAssetCount = 1u;
                result.techniqueSetPublished = true;
                result.stoppedBeforeAssetBody = false;
                result.stoppedBeforeShaderCreation = false;
                result.unsupportedOperation = nullptr;
                if (const RetailCensusError error = Push(0u);
                    error != RetailCensusError::None) return error;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, TECHNIQUE_SET_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.materialTechniqueSetBlock0Offset = span.offset;
                stage = RetailCensusStage::SecondTechniqueSet;
                continue;
            }
            if (stage == RetailCensusStage::SecondTechniqueSet)
            {
                const int visit = visitRecord(TECHNIQUE_SET_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                if (ReadU32(record) != INLINE_POINTER || record[4] != 0u ||
                    record[5] != 0u || record[6] != 0u || record[7] != 0u ||
                    ReadU32(record + 8u) != 0u)
                    return RetailCensusError::TechniqueSetLayoutUnsupported;
                for (std::uint32_t index = 0u; index < secondTechniqueTokens.size(); ++index)
                    secondTechniqueTokens[index] = ReadU32(record + 12u + index * 4u);
                cursor += TECHNIQUE_SET_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::SecondTechniqueSetName;
                continue;
            }
            if (stage == RetailCensusStage::SecondTechniqueSetName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxTechniqueNameBytes)
                        return RetailCensusError::TechniqueSetNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::TechniqueSetNameInvalid;
                if (bytes > limits.maxTechniqueNameBytes)
                    return RetailCensusError::TechniqueSetNameTooLong;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    result.materialTechniqueSetName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(result.materialTechniqueSetName))
                    return RetailCensusError::TechniqueSetNameInvalid;
                cursor += bytes;
                ++report.recordsProcessed;
                std::uint32_t populated = 0u;
                for (std::uint32_t index = 0u; index < secondTechniqueTokens.size(); ++index)
                {
                    if (secondTechniqueTokens[index] == 0u) continue;
                    if (index != result.firstTechniqueSlot ||
                        secondTechniqueTokens[index] != INLINE_POINTER)
                        return RetailCensusError::TechniqueReferenceUnsupported;
                    ++populated;
                }
                if (populated != 1u) return RetailCensusError::TechniqueReferenceUnsupported;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, TECHNIQUE_HEADER_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.materialTechniqueBlock4Offset = span.offset;
                stage = RetailCensusStage::SecondTechnique;
                continue;
            }
            if (stage == RetailCensusStage::SecondTechnique)
            {
                const int visit = visitRecord(TECHNIQUE_HEADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                secondTechniqueNameToken = ReadU32(record);
                ZoneSpan primaryNameSpan{4u, result.techniqueNameBlock4Offset, 4u};
                std::uint32_t expectedNameToken = 0u;
                if (!EncodeZoneAliasToken(primaryNameSpan, expectedNameToken) ||
                    secondTechniqueNameToken != expectedNameToken ||
                    ReadU16(record + 6u) != 1u)
                    return RetailCensusError::TechniqueAliasInvalid;
                secondMaterialPassBytes = MATERIAL_PASS_BYTES;
                cursor += TECHNIQUE_HEADER_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(1u, secondMaterialPassBytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::SecondMaterialPasses;
                continue;
            }
            if (stage == RetailCensusStage::SecondMaterialPasses)
            {
                const int visit = visitRecord(secondMaterialPassBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                ZoneSpan primaryDeclSpan{4u, result.vertexDeclarationBlock4Offset, 4u};
                std::uint32_t expectedDeclToken = 0u;
                if (!EncodeZoneAliasToken(primaryDeclSpan, expectedDeclToken) ||
                    ReadU32(record) != expectedDeclToken ||
                    ReadU32(record + 4u) != INLINE_POINTER ||
                    ReadU32(record + 8u) != INLINE_POINTER ||
                    ReadU32(record + 16u) != INLINE_POINTER)
                    return RetailCensusError::MaterialPassUnsupported;
                secondArgumentCount = static_cast<std::uint32_t>(record[12]) +
                    static_cast<std::uint32_t>(record[13]) +
                    static_cast<std::uint32_t>(record[14]);
                if (secondArgumentCount == 0u || secondArgumentCount > 192u ||
                    secondArgumentCount > UINT32_MAX / MATERIAL_ARGUMENT_BYTES)
                    return RetailCensusError::ShaderArgumentLayoutUnsupported;
                secondArgumentBytes = secondArgumentCount * MATERIAL_ARGUMENT_BYTES;
                cursor += secondMaterialPassBytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(4u, VERTEX_SHADER_BYTES);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::SecondVertexShader;
                continue;
            }
            if (stage == RetailCensusStage::SecondVertexShader)
            {
                const int visit = visitRecord(VERTEX_SHADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                secondVertexShaderNameToken = ReadU32(record);
                ZoneSpan primaryShaderNameSpan{4u, vertexShaderNameBlock4Offset, 4u};
                std::uint32_t expectedNameToken = 0u;
                const std::uint32_t dwords = ReadU16(record + 12u);
                if (!EncodeZoneAliasToken(primaryShaderNameSpan, expectedNameToken) ||
                    secondVertexShaderNameToken != expectedNameToken ||
                    ReadU32(record + 4u) != 0u || ReadU32(record + 8u) != INLINE_POINTER ||
                    dwords == 0u || ReadU16(record + 14u) != 1u)
                    return RetailCensusError::VertexShaderLayoutUnsupported;
                if (dwords > limits.maxShaderProgramDwords || dwords > UINT32_MAX / 4u)
                    return RetailCensusError::ShaderProgramSizeLimit;
                secondVertexProgramBytes = dwords * 4u;
                cursor += VERTEX_SHADER_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(4u, secondVertexProgramBytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::SecondVertexShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::SecondVertexShaderProgram)
            {
                const int visit = visitRecord(secondVertexProgramBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t version = ReadU32(record);
                if ((version & 0xffff0000u) != 0xfffe0000u ||
                    (version & 0xffffu) == 0u ||
                    ReadU32(record + secondVertexProgramBytes - 4u) != 0x0000ffffu)
                    return RetailCensusError::ShaderContractInvalid;
                cursor += secondVertexProgramBytes;
                ++report.recordsProcessed;
                if (const RetailCensusError planError = Plan(4u, PIXEL_SHADER_BYTES);
                    planError != RetailCensusError::None) return planError;
                stage = RetailCensusStage::SecondPixelShader;
                continue;
            }
            if (stage == RetailCensusStage::SecondPixelShader)
            {
                const int visit = visitRecord(PIXEL_SHADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                secondPixelShaderNameToken = ReadU32(record);
                const std::uint32_t dwords = ReadU16(record + 12u);
                if (secondPixelShaderNameToken != secondVertexShaderNameToken ||
                    ReadU32(record + 4u) != 0u || ReadU32(record + 8u) != INLINE_POINTER ||
                    dwords == 0u || ReadU16(record + 14u) != 1u)
                    return RetailCensusError::PixelShaderLayoutUnsupported;
                if (dwords > limits.maxShaderProgramDwords || dwords > UINT32_MAX / 4u)
                    return RetailCensusError::ShaderProgramSizeLimit;
                secondPixelProgramBytes = dwords * 4u;
                cursor += PIXEL_SHADER_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(4u, secondPixelProgramBytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::SecondPixelShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::SecondPixelShaderProgram)
            {
                const int visit = visitRecord(secondPixelProgramBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t version = ReadU32(record);
                if ((version & 0xffff0000u) != 0xffff0000u ||
                    (version & 0xffffu) == 0u ||
                    ReadU32(record + secondPixelProgramBytes - 4u) != 0x0000ffffu)
                    return RetailCensusError::ShaderContractInvalid;
                cursor += secondPixelProgramBytes;
                ++report.recordsProcessed;
                if (const RetailCensusError planError = Plan(4u, secondArgumentBytes);
                    planError != RetailCensusError::None) return planError;
                stage = RetailCensusStage::SecondShaderArguments;
                continue;
            }
            if (stage == RetailCensusStage::SecondShaderArguments)
            {
                const int visit = visitRecord(secondArgumentBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                for (std::uint32_t index = 0u; index < secondArgumentCount; ++index)
                {
                    const std::uint16_t type = ReadU16(record + index * MATERIAL_ARGUMENT_BYTES);
                    if (type != 2u && type != 3u)
                        return RetailCensusError::ShaderArgumentLayoutUnsupported;
                }
                cursor += secondArgumentBytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::SecondTechniqueName;
                continue;
            }
            if (stage == RetailCensusStage::SecondTechniqueName)
            {
                ZoneSpan primaryNameSpan{4u, result.techniqueNameBlock4Offset, 4u};
                std::uint32_t expectedNameToken = 0u;
                if (!EncodeZoneAliasToken(primaryNameSpan, expectedNameToken) ||
                    secondTechniqueNameToken != expectedNameToken)
                    return RetailCensusError::TechniqueAliasInvalid;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_TECHNIQUE_SET, 1u,
                            result.materialTechniqueSetName,
                            result.materialTechniqueSetIdentity));
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.PublishAlias(
                            topLevelAliasSlots[1],
                            result.materialTechniqueSetIdentity));
                    error != RetailCensusError::None) return error;
                result.completedAssetCount = 2u;
                result.materialTechniqueSetPublished = true;
                if (const RetailCensusError error = Push(0u);
                    error != RetailCensusError::None) return error;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, MATERIAL_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.materialBlock0Offset = span.offset;
                result.materialAssetIndex = 2u;
                stage = RetailCensusStage::Material;
                continue;
            }
            if (stage == RetailCensusStage::Material)
            {
                const int visit = visitRecord(MATERIAL_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                materialTechniqueSetToken = ReadU32(record + 64u);
                materialTextureTableToken = ReadU32(record + 68u);
                materialStateBitsToken = ReadU32(record + 76u);
                const std::uint32_t textureCount = record[58u];
                const std::uint32_t constantCount = record[59u];
                const std::uint32_t stateBitsCount = record[60u];
                if (ReadU32(record) != INLINE_POINTER || textureCount == 0u ||
                    textureCount > limits.maxMaterialTextures || textureCount != 1u ||
                    constantCount != 0u || stateBitsCount != 1u ||
                    materialTextureTableToken != INLINE_POINTER ||
                    ReadU32(record + 72u) != 0u ||
                    materialStateBitsToken != INLINE_POINTER)
                    return textureCount > limits.maxMaterialTextures
                        ? RetailCensusError::MaterialTextureCountLimit
                        : RetailCensusError::MaterialLayoutUnsupported;
                std::uint32_t techniqueIdentity = 0u;
                if (ResolveRegistryAlias(
                        materialTechniqueSetToken,
                        ASSET_TYPE_TECHNIQUE_SET,
                        techniqueIdentity) != ZoneRegistryError::None ||
                    techniqueIdentity != result.materialTechniqueSetIdentity)
                    return RetailCensusError::MaterialTechniqueSetInvalid;
                result.materialTextureCount = textureCount;
                materialStateBitsBytes = stateBitsCount * GFX_STATE_BITS_BYTES;
                cursor += MATERIAL_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::MaterialName;
                continue;
            }
            if (stage == RetailCensusStage::MaterialName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxMaterialNameBytes)
                        return RetailCensusError::MaterialNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::MaterialNameInvalid;
                if (bytes > limits.maxMaterialNameBytes)
                    return RetailCensusError::MaterialNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    result.materialNameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    result.materialName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(result.materialName))
                    return RetailCensusError::MaterialNameInvalid;
                cursor += bytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u,
                        static_cast<std::uint64_t>(result.materialTextureCount) * MATERIAL_TEXTURE_BYTES,
                        &span);
                    error != RetailCensusError::None) return error;
                result.materialTextureTableBlock4Offset = span.offset;
                stage = RetailCensusStage::MaterialTextureTable;
                continue;
            }
            if (stage == RetailCensusStage::MaterialTextureTable)
            {
                const std::uint32_t bytes = result.materialTextureCount * MATERIAL_TEXTURE_BYTES;
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                if (record[4u] == 0u || record[5u] == 0u || record[7u] == 11u ||
                    ReadU32(record + 8u) != INLINE_POINTER)
                    return RetailCensusError::MaterialTextureLayoutUnsupported;
                materialImageAliasSlot = {
                    4u,
                    result.materialTextureTableBlock4Offset + 8u,
                    4u,
                };
                if (const RetailCensusError error = MapRegistryError(
                        registry.ReserveAlias(materialImageAliasSlot, ASSET_TYPE_IMAGE));
                    error != RetailCensusError::None) return error;
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(0u);
                    error != RetailCensusError::None) return error;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, GFX_IMAGE_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.imageBlock0Offset = span.offset;
                stage = RetailCensusStage::Image;
                continue;
            }
            if (stage == RetailCensusStage::Image)
            {
                const int visit = visitRecord(GFX_IMAGE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                imageTextureToken = ReadU32(record + 4u);
                const std::uint32_t width = ReadU16(record + 24u);
                const std::uint32_t height = ReadU16(record + 26u);
                const std::uint32_t depth = ReadU16(record + 28u);
                if (ReadU32(record) != 3u ||
                    (imageTextureToken != INLINE_POINTER && imageTextureToken != SHARED_POINTER) ||
                    record[10u] > 1u || width == 0u || height == 0u || depth != 1u ||
                    ReadU32(record + 32u) != INLINE_POINTER)
                    return RetailCensusError::ImageLayoutUnsupported;
                result.imageWidth = width;
                result.imageHeight = height;
                result.imageDepth = depth;
                cursor += GFX_IMAGE_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::ImageName;
                continue;
            }
            if (stage == RetailCensusStage::ImageName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxImageNameBytes)
                        return RetailCensusError::ImageNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::ImageNameInvalid;
                if (bytes > limits.maxImageNameBytes)
                    return RetailCensusError::ImageNameTooLong;
                if (recordVisited == 0u)
                {
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(1u, bytes, &span);
                        error != RetailCensusError::None) return error;
                    result.imageNameBlock4Offset = span.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    result.imageName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                    result.imagePath = "images/";
                    result.imagePath += result.imageName;
                    result.imagePath += ".iwi";
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                if (!ValidPublishedName(result.imageName) ||
                    !ValidPublishedName(result.imagePath))
                    return RetailCensusError::ImageNameInvalid;
                cursor += bytes;
                ++report.recordsProcessed;
                if (imageTextureToken == SHARED_POINTER)
                {
                    ZoneSpan insert;
                    if (const RetailCensusError error = Plan(
                            4u, 4u, &insert);
                        error != RetailCensusError::None) return error;
                    result.imageTextureInsertPointerBlock4Offset = insert.offset;
                }
                if (const RetailCensusError error = Push(0u);
                    error != RetailCensusError::None) return error;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, GFX_IMAGE_LOAD_DEF_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.imageLoadDefBlock0Offset = span.offset;
                stage = RetailCensusStage::ImageLoadDef;
                continue;
            }
            if (stage == RetailCensusStage::ImageLoadDef)
            {
                const int visit = visitRecord(GFX_IMAGE_LOAD_DEF_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::int32_t width = static_cast<std::int16_t>(ReadU16(record + 2u));
                const std::int32_t height = static_cast<std::int16_t>(ReadU16(record + 4u));
                const std::int32_t depth = static_cast<std::int16_t>(ReadU16(record + 6u));
                const std::int32_t resourceBytes = ReadS32(record + 12u);
                if (record[0u] != 1u || record[1u] != 2u ||
                    !SupportedImageFormat(ReadU32(record + 8u)) ||
                    width != static_cast<std::int32_t>(result.imageWidth) ||
                    height != static_cast<std::int32_t>(result.imageHeight) ||
                    depth != static_cast<std::int32_t>(result.imageDepth) || resourceBytes < 0)
                    return resourceBytes < 0
                        ? RetailCensusError::ImageResourceSizeInvalid
                        : RetailCensusError::ImageLayoutUnsupported;
                if (static_cast<std::uint32_t>(resourceBytes) > limits.maxImageResourceBytes)
                    return RetailCensusError::ImageResourceSizeLimit;
                result.imageFormat = ReadU32(record + 8u);
                result.imageResourceBytes = static_cast<std::uint32_t>(resourceBytes);
                imageResourceBytes = result.imageResourceBytes;
                cursor += GFX_IMAGE_LOAD_DEF_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(1u, imageResourceBytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::ImageResource;
                continue;
            }
            if (stage == RetailCensusStage::ImageResource)
            {
                const int visit = visitRecord(imageResourceBytes);
                if (visit <= 0) return RetailCensusError::None;
                cursor += imageResourceBytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_IMAGE, result.materialAssetIndex,
                            result.imageName, result.imageIdentity));
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.PublishAlias(materialImageAliasSlot, result.imageIdentity));
                    error != RetailCensusError::None) return error;
                result.imagePublished = true;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, materialStateBitsBytes, &span);
                    error != RetailCensusError::None) return error;
                result.materialStateBitsBlock4Offset = span.offset;
                stage = RetailCensusStage::MaterialStateBits;
                continue;
            }
            if (stage == RetailCensusStage::MaterialStateBits)
            {
                const int visit = visitRecord(materialStateBitsBytes);
                if (visit <= 0) return RetailCensusError::None;
                cursor += materialStateBitsBytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.RegisterAsset(
                            ASSET_TYPE_MATERIAL, result.materialAssetIndex,
                            result.materialName, result.materialIdentity));
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = MapRegistryError(
                        registry.PublishAlias(
                            topLevelAliasSlots[2], result.materialIdentity));
                    error != RetailCensusError::None) return error;
                result.materialPublished = true;
                result.materialImageResolved = result.imagePublished;
                result.completedAssetCount = 3u;
                result.registryAssetCount = registry.AssetCount();
                result.registryAliasCount = registry.AliasCount();
                result.registryDefinedAliasCount = registry.DefinedAliasCount();
                if (result.registryAssetCount != 4u ||
                    result.registryAliasCount != 4u ||
                    result.registryDefinedAliasCount != 4u)
                    return RetailCensusError::AssetRegistryInvalid;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                stage = RetailCensusStage::AssetBoundary;
                complete = true;
                return RetailCensusError::None;
            }
            return RetailCensusError::InvalidArgument;
        }
        return RetailCensusError::None;
    }
};

RetailFastfileCensusJob::RetailFastfileCensusJob() noexcept = default;
RetailFastfileCensusJob::~RetailFastfileCensusJob() = default;
RetailFastfileCensusJob::RetailFastfileCensusJob(RetailFastfileCensusJob &&) noexcept = default;
RetailFastfileCensusJob &RetailFastfileCensusJob::operator=(RetailFastfileCensusJob &&) noexcept = default;

RetailCensusError RetailFastfileCensusJob::BeginStreaming(const RetailCensusLimits &limits) noexcept
{
    return BeginStreaming(RetailCensusMode::CodePostGfxMaterial, limits);
}

RetailCensusError RetailFastfileCensusJob::BeginStreaming(
    RetailCensusMode mode,
    const RetailCensusLimits &limits,
    RetailSoundAliasLookup soundLookup) noexcept
{
    Reset();
    if (!ValidLimits(limits)) return RetailCensusError::InvalidArgument;
    try { impl_ = std::make_unique<Impl>(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    impl_->limits = limits;
    impl_->prerequisiteZone = mode == RetailCensusMode::PrerequisiteZone;
    impl_->mode = impl_->prerequisiteZone
        ? RetailCensusMode::WorldXModelLoader : mode;
    impl_->soundLookup = soundLookup;
    const SourceStreamError sourceError = impl_->source.Initialize({
        limits.maxFileBytes, limits.maxSourceChunkBytes});
    if (sourceError != SourceStreamError::None)
    {
        impl_.reset();
        return MapSourceError(sourceError);
    }
    try { impl_->inflated.reserve(limits.maxInflatedPrefixBytes); }
    catch (...) { impl_.reset(); return RetailCensusError::AllocationFailed; }
    progress_ = RetailCensusProgress::Running;
    stage_ = RetailCensusStage::Prefix;
    return RetailCensusError::None;
}

RetailCensusError RetailFastfileCensusJob::FeedSource(
    std::span<const std::uint8_t> bytes, bool final) noexcept
{
    if (!impl_ || progress_ != RetailCensusProgress::Running)
        return RetailCensusError::InvalidArgument;
    return MapSourceError(impl_->source.Feed(bytes, final));
}

RetailCensusStepReport RetailFastfileCensusJob::Step(
    const RetailCensusStepBudget &budget) noexcept
{
    RetailCensusStepReport report{progress_, stage_, failure_};
    if (!impl_ || progress_ != RetailCensusProgress::Running) return report;
    auto fail = [&](RetailCensusError error) {
        if (impl_->streamInitialized)
        {
            inflateEnd(&impl_->stream);
            impl_->streamInitialized = false;
        }
        failure_ = error;
        progress_ = RetailCensusProgress::Failed;
        stage_ = RetailCensusStage::Failed;
        report.progress = progress_;
        report.stage = stage_;
        report.error = error;
    };
    if (budget.maxBytes == 0u || budget.maxBytes > RETAIL_CENSUS_MAX_STEP_BYTES ||
        budget.maxRecords == 0u || budget.maxRecords > RETAIL_CENSUS_MAX_STEP_RECORDS)
    {
        fail(RetailCensusError::InvalidStepBudget);
        return report;
    }

    while (report.sourceBytesConsumed < budget.maxBytes)
    {
        if (impl_->prefixCount < PREFIX_BYTES)
        {
            const std::uint32_t remaining = PREFIX_BYTES - impl_->prefixCount;
            const auto input = impl_->source.Peek(std::min(
                remaining, budget.maxBytes - report.sourceBytesConsumed));
            if (input.empty())
            {
                if (impl_->source.FinalReceived()) fail(RetailCensusError::PrefixTruncated);
                else report.needsSource = true;
                break;
            }
            std::memcpy(impl_->prefix.data() + impl_->prefixCount, input.data(), input.size());
            const auto consumed = static_cast<std::uint32_t>(input.size());
            if (const auto error = MapSourceError(impl_->source.Consume(consumed));
                error != RetailCensusError::None) { fail(error); break; }
            impl_->prefixCount += consumed;
            report.sourceBytesConsumed += consumed;
            if (impl_->prefixCount != PREFIX_BYTES) continue;
            if (const auto error = impl_->ValidatePrefix(); error != RetailCensusError::None)
            { fail(error); break; }
            impl_->stream = {};
            const int initResult = inflateInit(&impl_->stream);
            if (initResult != Z_OK)
            {
                fail(initResult == Z_MEM_ERROR
                    ? RetailCensusError::AllocationFailed
                    : RetailCensusError::InflateInit);
                break;
            }
            impl_->streamInitialized = true;
            stage_ = RetailCensusStage::XFile;
        }

        bool blocked = false;
        bool complete = false;
        if (const auto error = impl_->Parse(budget, report, stage_, blocked, complete);
            error != RetailCensusError::None)
        { fail(error); break; }
        if (complete)
        {
            impl_->result.sourceBytesConsumed = impl_->source.TotalBytesConsumed();
            impl_->result.sourceFeedCount = impl_->source.FeedCount();
            if (const RetailCensusError traceError =
                    impl_->EnsureBoundarySemanticTrace();
                traceError != RetailCensusError::None)
            {
                fail(traceError);
                break;
            }
            impl_->result.semanticTraceHash =
                kisak::database::SemanticTraceHash(
                    impl_->result.semanticTrace);
            impl_->result.semanticTraceContractHash =
                kisak::database::SemanticTraceContractHash(
                    impl_->result.semanticTrace);
            if (impl_->streamInitialized)
            {
                inflateEnd(&impl_->stream);
                impl_->streamInitialized = false;
            }
            progress_ = RetailCensusProgress::Succeeded;
            resultAvailable_ = true;
            report.progress = progress_;
            report.stage = stage_;
            break;
        }
        if (!blocked || report.inflatedBytesProduced >= budget.maxBytes ||
            impl_->inflated.size() == impl_->limits.maxInflatedPrefixBytes)
        {
            if (blocked && impl_->inflated.size() == impl_->limits.maxInflatedPrefixBytes)
                fail(RetailCensusError::InflatedPrefixLimit);
            break;
        }
        const auto input = impl_->source.Peek(
            budget.maxBytes - report.sourceBytesConsumed);
        if (input.empty())
        {
            if (impl_->source.FinalReceived()) fail(RetailCensusError::InflateTruncated);
            else report.needsSource = true;
            break;
        }
        const std::uint32_t outputCapacity = std::min<std::uint32_t>(
            budget.maxBytes - report.inflatedBytesProduced,
            impl_->limits.maxInflatedPrefixBytes -
                static_cast<std::uint32_t>(impl_->inflated.size()));
        if (outputCapacity == 0u) { fail(RetailCensusError::InflatedPrefixLimit); break; }
        const std::size_t oldSize = impl_->inflated.size();
        try { impl_->inflated.resize(oldSize + outputCapacity); }
        catch (...) { fail(RetailCensusError::AllocationFailed); break; }
        impl_->stream.next_in = const_cast<Bytef *>(
            reinterpret_cast<const Bytef *>(input.data()));
        impl_->stream.avail_in = static_cast<uInt>(input.size());
        impl_->stream.next_out = reinterpret_cast<Bytef *>(impl_->inflated.data() + oldSize);
        impl_->stream.avail_out = outputCapacity;
        const uInt inputBefore = impl_->stream.avail_in;
        const uInt outputBefore = impl_->stream.avail_out;
        const int inflateResult = inflate(&impl_->stream, Z_NO_FLUSH);
        const std::uint32_t consumed = inputBefore - impl_->stream.avail_in;
        const std::uint32_t produced = outputBefore - impl_->stream.avail_out;
        impl_->inflated.resize(oldSize + produced);
        if (consumed != 0u)
        {
            if (const auto error = MapSourceError(impl_->source.Consume(consumed));
                error != RetailCensusError::None) { fail(error); break; }
        }
        report.sourceBytesConsumed += consumed;
        report.inflatedBytesProduced += produced;
        if (inflateResult == Z_STREAM_END)
        {
            inflateEnd(&impl_->stream);
            impl_->streamInitialized = false;
            impl_->streamEnded = true;
        }
        else if (inflateResult == Z_MEM_ERROR) { fail(RetailCensusError::AllocationFailed); break; }
        else if (inflateResult != Z_OK && inflateResult != Z_BUF_ERROR)
        { fail(RetailCensusError::InflateData); break; }
        if (consumed == 0u && produced == 0u)
        {
            if (impl_->source.FinalReceived()) fail(RetailCensusError::InflateTruncated);
            else report.needsSource = true;
            break;
        }
        if (impl_->streamEnded && blocked)
        {
            bool parseBlocked = false;
            bool parseComplete = false;
            if (const auto error = impl_->Parse(
                    budget, report, stage_, parseBlocked, parseComplete);
                error != RetailCensusError::None) fail(error);
            else if (parseComplete)
            {
                impl_->result.sourceBytesConsumed = impl_->source.TotalBytesConsumed();
                impl_->result.sourceFeedCount = impl_->source.FeedCount();
                if (const RetailCensusError traceError =
                        impl_->EnsureBoundarySemanticTrace();
                    traceError != RetailCensusError::None)
                {
                    fail(traceError);
                }
                else
                {
                    impl_->result.semanticTraceHash =
                        kisak::database::SemanticTraceHash(
                            impl_->result.semanticTrace);
                    impl_->result.semanticTraceContractHash =
                        kisak::database::SemanticTraceContractHash(
                            impl_->result.semanticTrace);
                    progress_ = RetailCensusProgress::Succeeded;
                    resultAvailable_ = true;
                }
            }
            else if (parseBlocked) fail(RetailCensusError::RecordTruncated);
            break;
        }
    }
    report.progress = progress_;
    report.stage = stage_;
    report.error = failure_;
    return report;
}

RetailCensusProgress RetailFastfileCensusJob::Progress() const noexcept { return progress_; }
RetailCensusStage RetailFastfileCensusJob::Stage() const noexcept { return stage_; }
RetailCensusError RetailFastfileCensusJob::Failure() const noexcept { return failure_; }
std::uint32_t RetailFastfileCensusJob::CurrentAssetIndex() const noexcept
{
    return impl_ ? impl_->result.nextBodyIndex : UINT32_MAX;
}
std::uint32_t RetailFastfileCensusJob::CurrentAssetType() const noexcept
{
    return impl_ ? impl_->result.nextBodyType : UINT32_MAX;
}
bool RetailFastfileCensusJob::NeedsSource() const noexcept
{
    return impl_ && progress_ == RetailCensusProgress::Running && impl_->source.NeedsSource();
}
std::uint64_t RetailFastfileCensusJob::SourceBytesReceived() const noexcept
{
    return impl_ ? impl_->source.TotalBytesReceived() : 0u;
}
bool RetailFastfileCensusJob::TakeResult(RetailFastfileCensus &destination) noexcept
{
    if (!impl_ || !resultAvailable_ || progress_ != RetailCensusProgress::Succeeded)
        return false;
    destination = impl_->result;
    resultAvailable_ = false;
    return true;
}
void RetailFastfileCensusJob::Reset() noexcept
{
    impl_.reset();
    progress_ = RetailCensusProgress::NotStarted;
    stage_ = RetailCensusStage::NotStarted;
    failure_ = RetailCensusError::None;
    resultAvailable_ = false;
}

} // namespace kisak::fastfile
