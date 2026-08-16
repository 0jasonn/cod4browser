#pragma once

#include <bgame/weapon_types.h>
#include <web/web_retail_load_context.h>

#include <array>
#include <cstdint>

namespace kisak::fastfile::weapon_loader
{

enum class OperationKind : std::uint8_t
{
    String = 0,
    XModel,
    Fx,
    Material,
    Sound,
    BounceSound,
    AccuracyKnots,
};

struct Operation
{
    OperationKind kind;
    std::uint16_t index;
};

const std::array<std::uint32_t, 48> &StringFieldOffsets() noexcept;
const std::array<std::uint32_t, 38> &XModelFieldOffsets() noexcept;
const std::array<std::uint32_t, 10> &FxFieldOffsets() noexcept;
const std::array<std::uint32_t, 8> &MaterialFieldOffsets() noexcept;
const std::array<std::uint32_t, 48> &SoundFieldOffsets() noexcept;
const std::array<std::uint32_t, 4> &AccuracyKnotFieldOffsets() noexcept;
const std::array<Operation, 157> &Operations() noexcept;

RetailCensusError ResolveCanonicalDependency(
    RetailLoadContext &context,
    std::uint32_t token,
    std::uint32_t assetType,
    void *&asset) noexcept;

void AssignWeaponString(WeaponDef &weapon, std::uint32_t index,
    const char *value) noexcept;
void AssignWeaponAccuracyKnots(WeaponDef &weapon, std::uint32_t index,
    float (*value)[WEAP_ACCURACY_COUNT]) noexcept;
void AssignWeaponXModel(WeaponDef &weapon, std::uint32_t index,
    XModel *value) noexcept;
void AssignWeaponFx(WeaponDef &weapon, std::uint32_t index,
    const FxEffectDef *value) noexcept;
void AssignWeaponMaterial(WeaponDef &weapon, std::uint32_t index,
    Material *value) noexcept;
void AssignWeaponSound(WeaponDef &weapon, std::uint32_t index,
    snd_alias_list_t *value) noexcept;

} // namespace kisak::fastfile::weapon_loader
