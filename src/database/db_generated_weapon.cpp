#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <EffectsCore/fx_types.h>
#include <gfx_d3d/material_types.h>
#include <sound/snd_alias_types.h>

#include <array>
#include <cstdint>
#include <limits>

WeaponDef *varWeaponDef = nullptr;
WeaponDef **varWeaponDefPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical WeaponDef loader requires the IW3 32-bit ABI");
static_assert(sizeof(WeaponDef) == 2168u);

template <typename T>
T *Alloc(int alignment)
{
    return reinterpret_cast<T *>(DB_AllocStreamPos(alignment));
}

bool CheckedArray(std::int64_t count, std::size_t stride, const char *stage,
    std::size_t &bytes)
{
    if (count < 0 || count > (std::numeric_limits<std::int32_t>::max)() ||
        static_cast<std::uint64_t>(count) * stride >
            (std::numeric_limits<std::uint32_t>::max)())
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

void LoadXStringField(const char **field)
{
    varXString = field;
    Load_XString(false);
}

void LoadXStringFields(const char **fields, std::int32_t count)
{
    for (std::int32_t index = 0; index < count; ++index)
    {
        LoadXStringField(&fields[index]);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadSoundName(snd_alias_list_t **field, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(field), 4);
    if (DB_RuntimeGeneratedLoadFailed() || !*field) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(*field);
    const char **nameCell = nullptr;
    if (token == UINT32_MAX)
    {
        nameCell = Alloc<const char *>(3);
        *field = reinterpret_cast<snd_alias_list_t *>(nameCell);
        varXString = nameCell;
        Load_XString(true);
    }
    else
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(field));
        nameCell = reinterpret_cast<const char **>(*field);
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (!nameCell || !*nameCell)
    {
        DB_RuntimeGeneratedFailure("Weapon/sound name missing");
        return;
    }
    *field = DB_FindXAssetHeader(ASSET_TYPE_SOUND, *nameCell).sound;
}

void LoadSoundFields(const std::array<snd_alias_list_t **, 48> &fields,
    std::size_t begin, std::size_t end)
{
    for (std::size_t index = begin; index < end; ++index)
    {
        LoadSoundName(fields[index], false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadAccuracyKnots(float (*&knots)[WEAP_ACCURACY_COUNT],
    std::int32_t count, const char *stage)
{
    if (!knots) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(knots);
    if (token == UINT32_MAX)
    {
        std::size_t bytes = 0;
        if (!CheckedArray(count, sizeof(*knots), stage, bytes)) return;
        knots = Alloc<float[WEAP_ACCURACY_COUNT]>(3);
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(knots),
            static_cast<std::int32_t>(bytes));
    }
    else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(&knots));
}

void LoadWeaponDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varWeaponDef),
        sizeof(WeaponDef));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);

    LoadXStringField(&varWeaponDef->szInternalName);
    LoadXStringField(&varWeaponDef->szDisplayName);
    LoadXStringField(&varWeaponDef->szOverlayName);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXModelPtr = varWeaponDef->gunXModel;
        Load_XModelPtrArray(false, 16);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXModelPtr = &varWeaponDef->handXModel;
        Load_XModelPtr(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
        LoadXStringFields(varWeaponDef->szXAnims, 33);
    if (!DB_RuntimeGeneratedLoadFailed())
        LoadXStringField(&varWeaponDef->szModeName);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varScriptString = varWeaponDef->hideTags;
        Load_ScriptStringArray(false, 8);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varScriptString = varWeaponDef->notetrackSoundMapKeys;
        Load_ScriptStringArray(false, 16);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varScriptString = varWeaponDef->notetrackSoundMapValues;
        Load_ScriptStringArray(false, 16);
    }

    const std::array<const FxEffectDef **, 10> fxFields{{
        &varWeaponDef->viewFlashEffect, &varWeaponDef->worldFlashEffect,
        &varWeaponDef->viewShellEjectEffect, &varWeaponDef->worldShellEjectEffect,
        &varWeaponDef->viewLastShotEjectEffect,
        &varWeaponDef->worldLastShotEjectEffect,
        &varWeaponDef->projExplosionEffect, &varWeaponDef->projDudEffect,
        &varWeaponDef->projTrailEffect, &varWeaponDef->projIgnitionEffect}};
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varFxEffectDefHandle = fxFields[0]; Load_FxEffectDefHandle(false);
        if (!DB_RuntimeGeneratedLoadFailed())
        { varFxEffectDefHandle = fxFields[1]; Load_FxEffectDefHandle(false); }
    }

    const std::array<snd_alias_list_t **, 48> soundFields{{
        &varWeaponDef->pickupSound, &varWeaponDef->pickupSoundPlayer,
        &varWeaponDef->ammoPickupSound, &varWeaponDef->ammoPickupSoundPlayer,
        &varWeaponDef->projectileSound, &varWeaponDef->pullbackSound,
        &varWeaponDef->pullbackSoundPlayer, &varWeaponDef->fireSound,
        &varWeaponDef->fireSoundPlayer, &varWeaponDef->fireLoopSound,
        &varWeaponDef->fireLoopSoundPlayer, &varWeaponDef->fireStopSound,
        &varWeaponDef->fireStopSoundPlayer, &varWeaponDef->fireLastSound,
        &varWeaponDef->fireLastSoundPlayer, &varWeaponDef->emptyFireSound,
        &varWeaponDef->emptyFireSoundPlayer, &varWeaponDef->meleeSwipeSound,
        &varWeaponDef->meleeSwipeSoundPlayer, &varWeaponDef->meleeHitSound,
        &varWeaponDef->meleeMissSound, &varWeaponDef->rechamberSound,
        &varWeaponDef->rechamberSoundPlayer, &varWeaponDef->reloadSound,
        &varWeaponDef->reloadSoundPlayer, &varWeaponDef->reloadEmptySound,
        &varWeaponDef->reloadEmptySoundPlayer, &varWeaponDef->reloadStartSound,
        &varWeaponDef->reloadStartSoundPlayer, &varWeaponDef->reloadEndSound,
        &varWeaponDef->reloadEndSoundPlayer, &varWeaponDef->detonateSound,
        &varWeaponDef->detonateSoundPlayer, &varWeaponDef->nightVisionWearSound,
        &varWeaponDef->nightVisionWearSoundPlayer,
        &varWeaponDef->nightVisionRemoveSound,
        &varWeaponDef->nightVisionRemoveSoundPlayer,
        &varWeaponDef->altSwitchSound, &varWeaponDef->altSwitchSoundPlayer,
        &varWeaponDef->raiseSound, &varWeaponDef->raiseSoundPlayer,
        &varWeaponDef->firstRaiseSound, &varWeaponDef->firstRaiseSoundPlayer,
        &varWeaponDef->putawaySound, &varWeaponDef->putawaySoundPlayer,
        &varWeaponDef->projExplosionSound, &varWeaponDef->projDudSound,
        &varWeaponDef->projIgnitionSound}};
    if (!DB_RuntimeGeneratedLoadFailed()) LoadSoundFields(soundFields, 0, 45);

    if (!DB_RuntimeGeneratedLoadFailed() && varWeaponDef->bounceSound)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            varWeaponDef->bounceSound);
        if (token == UINT32_MAX)
        {
            std::size_t bytes = 0;
            if (CheckedArray(29, sizeof(snd_alias_list_t *),
                "Weapon/bounce sounds", bytes))
            {
                varWeaponDef->bounceSound = Alloc<snd_alias_list_t *>(3);
                Load_Stream(true, reinterpret_cast<std::uint8_t *>(
                    varWeaponDef->bounceSound), static_cast<std::int32_t>(bytes));
                for (std::int32_t index = 0;
                    index < 29 && !DB_RuntimeGeneratedLoadFailed(); ++index)
                    LoadSoundName(&varWeaponDef->bounceSound[index], false);
            }
        }
        else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
            &varWeaponDef->bounceSound));
    }

    for (std::size_t index = 2; index < 6 &&
        !DB_RuntimeGeneratedLoadFailed(); ++index)
    {
        varFxEffectDefHandle = fxFields[index];
        Load_FxEffectDefHandle(false);
    }
    const std::array<Material **, 8> materialFields{{
        &varWeaponDef->reticleCenter, &varWeaponDef->reticleSide,
        &varWeaponDef->hudIcon, &varWeaponDef->ammoCounterIcon,
        &varWeaponDef->overlayMaterial, &varWeaponDef->overlayMaterialLowRes,
        &varWeaponDef->killIcon, &varWeaponDef->dpadIcon}};
    for (std::size_t index = 0; index < 2 &&
        !DB_RuntimeGeneratedLoadFailed(); ++index)
    { varMaterialHandle = materialFields[index]; Load_MaterialHandle(false); }

    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXModelPtr = varWeaponDef->worldModel;
        Load_XModelPtrArray(false, 16);
    }
    const std::array<XModel **, 4> laterModels{{
        &varWeaponDef->worldClipModel, &varWeaponDef->rocketModel,
        &varWeaponDef->knifeModel, &varWeaponDef->worldKnifeModel}};
    for (XModel **field : laterModels)
    {
        if (DB_RuntimeGeneratedLoadFailed()) break;
        varXModelPtr = field; Load_XModelPtr(false);
    }
    for (std::size_t index = 2; index < 4 &&
        !DB_RuntimeGeneratedLoadFailed(); ++index)
    { varMaterialHandle = materialFields[index]; Load_MaterialHandle(false); }
    if (!DB_RuntimeGeneratedLoadFailed()) LoadXStringField(&varWeaponDef->szAmmoName);
    if (!DB_RuntimeGeneratedLoadFailed()) LoadXStringField(&varWeaponDef->szClipName);
    if (!DB_RuntimeGeneratedLoadFailed())
        LoadXStringField(&varWeaponDef->szSharedAmmoCapName);
    for (std::size_t index = 4; index < 8 &&
        !DB_RuntimeGeneratedLoadFailed(); ++index)
    { varMaterialHandle = materialFields[index]; Load_MaterialHandle(false); }
    if (!DB_RuntimeGeneratedLoadFailed())
        LoadXStringField(&varWeaponDef->szAltWeaponName);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXModelPtr = &varWeaponDef->projectileModel;
        Load_XModelPtr(false);
    }
    for (std::size_t index = 6; index < 8 &&
        !DB_RuntimeGeneratedLoadFailed(); ++index)
    { varFxEffectDefHandle = fxFields[index]; Load_FxEffectDefHandle(false); }
    if (!DB_RuntimeGeneratedLoadFailed()) LoadSoundFields(soundFields, 45, 47);
    for (std::size_t index = 8; index < 10 &&
        !DB_RuntimeGeneratedLoadFailed(); ++index)
    { varFxEffectDefHandle = fxFields[index]; Load_FxEffectDefHandle(false); }
    if (!DB_RuntimeGeneratedLoadFailed()) LoadSoundFields(soundFields, 47, 48);

    if (!DB_RuntimeGeneratedLoadFailed())
        LoadXStringField(&varWeaponDef->accuracyGraphName[0]);
    if (!DB_RuntimeGeneratedLoadFailed()) LoadAccuracyKnots(
        varWeaponDef->accuracyGraphKnots[0],
        varWeaponDef->accuracyGraphKnotCount[0], "Weapon/accuracy graph 0");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadAccuracyKnots(
        varWeaponDef->originalAccuracyGraphKnots[0],
        varWeaponDef->accuracyGraphKnotCount[0],
        "Weapon/original accuracy graph 0");
    if (!DB_RuntimeGeneratedLoadFailed())
        LoadXStringField(&varWeaponDef->accuracyGraphName[1]);
    if (!DB_RuntimeGeneratedLoadFailed()) LoadAccuracyKnots(
        varWeaponDef->accuracyGraphKnots[1],
        varWeaponDef->accuracyGraphKnotCount[1], "Weapon/accuracy graph 1");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadAccuracyKnots(
        varWeaponDef->originalAccuracyGraphKnots[1],
        varWeaponDef->accuracyGraphKnotCount[1],
        "Weapon/original accuracy graph 1");
    if (!DB_RuntimeGeneratedLoadFailed()) LoadXStringField(&varWeaponDef->szUseHintString);
    if (!DB_RuntimeGeneratedLoadFailed()) LoadXStringField(&varWeaponDef->dropHintString);
    if (!DB_RuntimeGeneratedLoadFailed()) LoadXStringField(&varWeaponDef->szScript);
    if (!DB_RuntimeGeneratedLoadFailed()) LoadXStringField(&varWeaponDef->fireRumble);
    if (!DB_RuntimeGeneratedLoadFailed()) LoadXStringField(&varWeaponDef->meleeImpactRumble);
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_WeaponDefPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varWeaponDefPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varWeaponDefPtr)
    {
        const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
            *varWeaponDefPtr);
        if (token == UINT32_MAX || token == UINT32_MAX - 1u)
        {
            *varWeaponDefPtr = Alloc<WeaponDef>(3);
            varWeaponDef = *varWeaponDefPtr;
            const void **inserted = token == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            LoadWeaponDef(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_WeaponDefAsset(reinterpret_cast<XAssetHeader *>(
                    varWeaponDefPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varWeaponDefPtr)->szInternalName);
                if (inserted) *inserted = *varWeaponDefPtr;
            }
        }
        else DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
            varWeaponDefPtr));
    }
    DB_PopStreamPos();
}
