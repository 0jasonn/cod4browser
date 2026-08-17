#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_sound_platform.h>
#include <database/db_runtime_prefix.h>
#include <sound/snd_alias_types.h>

#include <cstdint>

LoadedSound *varLoadedSound = nullptr;
LoadedSound **varLoadedSoundPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical LoadedSound loader requires the IW3 32-bit ABI");
static_assert(sizeof(LoadedSound) == 44u);

void Load_MssSound(MssSoundCOD4 *sound, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(sound),
        sizeof(MssSoundCOD4));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(0);
    if (sound->data)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            sound->data);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            sound->data = DB_AllocStreamPos(0);
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            if (!DB_RuntimeStreamCanRead(sound->info.data_len))
                DB_RuntimeGeneratedFailure("LoadedSound/data array");
            else
                Load_Stream(true, sound->data, sound->info.data_len);
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_PlatformSetLoadedSoundData(sound, sound->data);
                if (inserted) *inserted = sound->data;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                &sound->data));
        }
    }
    DB_PopStreamPos();
}

void Load_LoadedSound(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varLoadedSound), sizeof(LoadedSound));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(4);
    varXString = &varLoadedSound->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed())
        Load_MssSound(&varLoadedSound->sound, false);
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_LoadedSoundPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varLoadedSoundPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(0);
    if (*varLoadedSoundPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varLoadedSoundPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varLoadedSoundPtr = reinterpret_cast<LoadedSound *>(
                AllocLoad_FxElemVisStateSample());
            varLoadedSound = *varLoadedSoundPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_LoadedSound(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_LoadedSoundAsset(reinterpret_cast<XAssetHeader *>(
                    varLoadedSoundPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varLoadedSoundPtr)->name);
                if (inserted) *inserted = *varLoadedSoundPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varLoadedSoundPtr));
        }
    }
    DB_PopStreamPos();
}
