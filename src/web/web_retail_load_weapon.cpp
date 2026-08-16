#include <web/web_retail_load_weapon.h>

#include <iterator>

namespace kisak::fastfile::weapon_loader
{
namespace
{
constexpr std::array<std::uint32_t, 48> STRING_OFFSETS = {{
    0u, 4u, 8u,
    80u, 84u, 88u, 92u, 96u, 100u, 104u, 108u, 112u, 116u,
    120u, 124u, 128u, 132u, 136u, 140u, 144u, 148u, 152u, 156u,
    160u, 164u, 168u, 172u, 176u, 180u, 184u, 188u, 192u, 196u,
    200u, 204u, 208u, 212u,
    804u, 812u, 832u, 1340u, 1900u, 1904u, 2012u, 2016u, 2036u,
    2152u, 2156u,
}};

constexpr std::array<std::uint32_t, 38> XMODEL_OFFSETS = [] {
    std::array<std::uint32_t, 38> offsets{};
    std::size_t output = 0u;
    for (std::uint32_t offset = 12u; offset <= 72u; offset += 4u)
        offsets[output++] = offset;
    offsets[output++] = 76u;
    for (std::uint32_t offset = 700u; offset <= 760u; offset += 4u)
        offsets[output++] = offset;
    for (const std::uint32_t offset : {764u, 768u, 772u, 776u, 1412u})
        offsets[output++] = offset;
    return offsets;
}();

constexpr std::array<std::uint32_t, 10> FX_OFFSETS = {{
    332u, 336u, 524u, 528u, 532u, 536u,
    1420u, 1428u, 1704u, 1732u,
}};

constexpr std::array<std::uint32_t, 8> MATERIAL_OFFSETS = {{
    540u, 544u, 780u, 788u, 1072u, 1076u, 1304u, 1316u,
}};

constexpr std::array<std::uint32_t, 48> SOUND_OFFSETS = [] {
    std::array<std::uint32_t, 48> offsets{};
    std::size_t output = 0u;
    for (std::uint32_t offset = 340u; offset <= 516u; offset += 4u)
        offsets[output++] = offset;
    offsets[output++] = 1432u;
    offsets[output++] = 1436u;
    offsets[output++] = 1736u;
    return offsets;
}();

// Exact generated Load_WeaponDef child order. Alias-only canonical handles
// consume no stream bytes but still resolve at their native position.
constexpr std::array<Operation, 157> OPERATIONS = [] {
    std::array<Operation, 157> operations{};
    std::size_t output = 0u;
    for (std::uint16_t index = 0u; index < 3u; ++index)
        operations[output++] = {OperationKind::String, index};
    for (std::uint16_t index = 0u; index < 17u; ++index)
        operations[output++] = {OperationKind::XModel, index};
    for (std::uint16_t index = 3u; index <= 36u; ++index)
        operations[output++] = {OperationKind::String, index};
    for (std::uint16_t index = 0u; index < 2u; ++index)
        operations[output++] = {OperationKind::Fx, index};
    for (std::uint16_t index = 0u; index < 45u; ++index)
        operations[output++] = {OperationKind::Sound, index};
    operations[output++] = {OperationKind::BounceSound, 0u};
    for (std::uint16_t index = 2u; index < 6u; ++index)
        operations[output++] = {OperationKind::Fx, index};
    for (std::uint16_t index = 0u; index < 2u; ++index)
        operations[output++] = {OperationKind::Material, index};
    for (std::uint16_t index = 17u; index <= 36u; ++index)
        operations[output++] = {OperationKind::XModel, index};
    operations[output++] = {OperationKind::Material, 2u};
    operations[output++] = {OperationKind::Material, 3u};
    operations[output++] = {OperationKind::String, 37u};
    operations[output++] = {OperationKind::String, 38u};
    operations[output++] = {OperationKind::String, 39u};
    for (std::uint16_t index = 4u; index < 8u; ++index)
        operations[output++] = {OperationKind::Material, index};
    operations[output++] = {OperationKind::String, 40u};
    operations[output++] = {OperationKind::XModel, 37u};
    operations[output++] = {OperationKind::Fx, 6u};
    operations[output++] = {OperationKind::Fx, 7u};
    operations[output++] = {OperationKind::Sound, 45u};
    operations[output++] = {OperationKind::Sound, 46u};
    operations[output++] = {OperationKind::Fx, 8u};
    operations[output++] = {OperationKind::Fx, 9u};
    operations[output++] = {OperationKind::Sound, 47u};
    operations[output++] = {OperationKind::String, 41u};
    operations[output++] = {OperationKind::AccuracyKnots, 0u};
    operations[output++] = {OperationKind::AccuracyKnots, 1u};
    operations[output++] = {OperationKind::String, 42u};
    operations[output++] = {OperationKind::AccuracyKnots, 2u};
    operations[output++] = {OperationKind::AccuracyKnots, 3u};
    for (std::uint16_t index = 43u; index < 48u; ++index)
        operations[output++] = {OperationKind::String, index};
    return operations;
}();

constexpr std::array<std::uint32_t, 4> ACCURACY_KNOT_OFFSETS = {{
    1908u, 1916u, 1912u, 1920u,
}};
} // namespace

const std::array<std::uint32_t, 48> &StringFieldOffsets() noexcept { return STRING_OFFSETS; }
const std::array<std::uint32_t, 38> &XModelFieldOffsets() noexcept { return XMODEL_OFFSETS; }
const std::array<std::uint32_t, 10> &FxFieldOffsets() noexcept { return FX_OFFSETS; }
const std::array<std::uint32_t, 8> &MaterialFieldOffsets() noexcept { return MATERIAL_OFFSETS; }
const std::array<std::uint32_t, 48> &SoundFieldOffsets() noexcept { return SOUND_OFFSETS; }
const std::array<std::uint32_t, 4> &AccuracyKnotFieldOffsets() noexcept { return ACCURACY_KNOT_OFFSETS; }
const std::array<Operation, 157> &Operations() noexcept { return OPERATIONS; }

RetailCensusError ResolveCanonicalDependency(
    RetailLoadContext &context,
    std::uint32_t token,
    std::uint32_t assetType,
    void *&asset) noexcept
{
    asset = nullptr;
    if (token == 0u) return RetailCensusError::None;
    if (token == UINT32_MAX || token == UINT32_MAX - 1u)
        return RetailCensusError::WeaponDependencyUnsupported;
    std::uint32_t identity = 0u;
    if (context.ResolveAssetAlias(token, assetType, identity) !=
        ZoneRegistryError::None)
    {
        return RetailCensusError::WeaponDependencyUnsupported;
    }
    asset = context.FindCanonicalAsset(assetType, identity);
    return asset ? RetailCensusError::None
                 : RetailCensusError::WeaponDependencyUnsupported;
}

void AssignWeaponString(WeaponDef &weapon, std::uint32_t index,
    const char *value) noexcept
{
    if (index < 3u)
    {
        const char **targets[] = {
            &weapon.szInternalName, &weapon.szDisplayName, &weapon.szOverlayName};
        *targets[index] = value;
        return;
    }
    if (index < 36u)
    {
        weapon.szXAnims[index - 3u] = value;
        return;
    }
    switch (index)
    {
    case 36u: weapon.szModeName = value; break;
    case 37u: weapon.szAmmoName = value; break;
    case 38u: weapon.szClipName = value; break;
    case 39u: weapon.szSharedAmmoCapName = value; break;
    case 40u: weapon.szAltWeaponName = value; break;
    case 41u: weapon.accuracyGraphName[0u] = value; break;
    case 42u: weapon.accuracyGraphName[1u] = value; break;
    case 43u: weapon.szUseHintString = value; break;
    case 44u: weapon.dropHintString = value; break;
    case 45u: weapon.szScript = value; break;
    case 46u: weapon.fireRumble = value; break;
    case 47u: weapon.meleeImpactRumble = value; break;
    default: break;
    }
}

void AssignWeaponAccuracyKnots(WeaponDef &weapon, std::uint32_t index,
    float (*value)[WEAP_ACCURACY_COUNT]) noexcept
{
    switch (index)
    {
    case 0u: weapon.accuracyGraphKnots[0u] = value; break;
    case 1u: weapon.originalAccuracyGraphKnots[0u] = value; break;
    case 2u: weapon.accuracyGraphKnots[1u] = value; break;
    case 3u: weapon.originalAccuracyGraphKnots[1u] = value; break;
    default: break;
    }
}

void AssignWeaponXModel(WeaponDef &weapon, std::uint32_t index, XModel *value) noexcept
{
    if (index < 16u) weapon.gunXModel[index] = value;
    else if (index == 16u) weapon.handXModel = value;
    else if (index < 33u) weapon.worldModel[index - 17u] = value;
    else if (index == 33u) weapon.worldClipModel = value;
    else if (index == 34u) weapon.rocketModel = value;
    else if (index == 35u) weapon.knifeModel = value;
    else if (index == 36u) weapon.worldKnifeModel = value;
    else if (index == 37u) weapon.projectileModel = value;
}

void AssignWeaponFx(WeaponDef &weapon, std::uint32_t index,
    const FxEffectDef *value) noexcept
{
    const FxEffectDef **targets[] = {
        &weapon.viewFlashEffect, &weapon.worldFlashEffect,
        &weapon.viewShellEjectEffect, &weapon.worldShellEjectEffect,
        &weapon.viewLastShotEjectEffect, &weapon.worldLastShotEjectEffect,
        &weapon.projExplosionEffect, &weapon.projDudEffect,
        &weapon.projTrailEffect, &weapon.projIgnitionEffect};
    if (index < std::size(targets)) *targets[index] = value;
}

void AssignWeaponMaterial(WeaponDef &weapon, std::uint32_t index,
    Material *value) noexcept
{
    Material **targets[] = {
        &weapon.reticleCenter, &weapon.reticleSide, &weapon.hudIcon,
        &weapon.ammoCounterIcon, &weapon.overlayMaterial,
        &weapon.overlayMaterialLowRes, &weapon.killIcon, &weapon.dpadIcon};
    if (index < std::size(targets)) *targets[index] = value;
}

void AssignWeaponSound(WeaponDef &weapon, std::uint32_t index,
    snd_alias_list_t *value) noexcept
{
    snd_alias_list_t **targets[] = {
        &weapon.pickupSound, &weapon.pickupSoundPlayer,
        &weapon.ammoPickupSound, &weapon.ammoPickupSoundPlayer,
        &weapon.projectileSound, &weapon.pullbackSound,
        &weapon.pullbackSoundPlayer, &weapon.fireSound,
        &weapon.fireSoundPlayer, &weapon.fireLoopSound,
        &weapon.fireLoopSoundPlayer, &weapon.fireStopSound,
        &weapon.fireStopSoundPlayer, &weapon.fireLastSound,
        &weapon.fireLastSoundPlayer, &weapon.emptyFireSound,
        &weapon.emptyFireSoundPlayer, &weapon.meleeSwipeSound,
        &weapon.meleeSwipeSoundPlayer, &weapon.meleeHitSound,
        &weapon.meleeMissSound, &weapon.rechamberSound,
        &weapon.rechamberSoundPlayer, &weapon.reloadSound,
        &weapon.reloadSoundPlayer, &weapon.reloadEmptySound,
        &weapon.reloadEmptySoundPlayer, &weapon.reloadStartSound,
        &weapon.reloadStartSoundPlayer, &weapon.reloadEndSound,
        &weapon.reloadEndSoundPlayer, &weapon.detonateSound,
        &weapon.detonateSoundPlayer, &weapon.nightVisionWearSound,
        &weapon.nightVisionWearSoundPlayer, &weapon.nightVisionRemoveSound,
        &weapon.nightVisionRemoveSoundPlayer, &weapon.altSwitchSound,
        &weapon.altSwitchSoundPlayer, &weapon.raiseSound,
        &weapon.raiseSoundPlayer, &weapon.firstRaiseSound,
        &weapon.firstRaiseSoundPlayer, &weapon.putawaySound,
        &weapon.putawaySoundPlayer, &weapon.projExplosionSound,
        &weapon.projDudSound, &weapon.projIgnitionSound};
    if (index < std::size(targets)) *targets[index] = value;
}

} // namespace kisak::fastfile::weapon_loader
