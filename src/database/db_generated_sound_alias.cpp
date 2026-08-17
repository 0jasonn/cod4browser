#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <sound/snd_alias_types.h>

#include <cstdint>
#include <limits>

snd_alias_list_t *varsnd_alias_list_t = nullptr;
snd_alias_list_t **varsnd_alias_list_ptr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical sound-alias loader requires the IW3 32-bit ABI");
static_assert(sizeof(snd_alias_list_t) == 12u);
static_assert(sizeof(snd_alias_t) == 92u);
static_assert(sizeof(SoundFile) == 12u);
static_assert(sizeof(SpeakerMap) == 408u);

bool CheckedArray(std::int32_t count, std::size_t stride,
    const char *stage, std::size_t &bytes)
{
    if (count < 0 || static_cast<std::uint64_t>(count) * stride >
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

void Load_StreamedSound(StreamedSound *sound)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(sound),
        sizeof(StreamedSound));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &sound->filename.info.raw.dir;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXString = &sound->filename.info.raw.name;
        Load_XString(false);
    }
}

void Load_SoundFile(SoundFile *soundFile, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(soundFile),
        sizeof(SoundFile));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (soundFile->type == 1)
    {
        varLoadedSoundPtr = &soundFile->u.loadSnd;
        Load_LoadedSoundPtr(false);
    }
    else
    {
        Load_StreamedSound(&soundFile->u.streamSnd);
    }
}

void Load_SpeakerMap(SpeakerMap *speakerMap, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(speakerMap),
        sizeof(SpeakerMap));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &speakerMap->name;
    Load_XString(false);
}

void Load_snd_alias_t(snd_alias_t *alias)
{
    Load_Stream(false, reinterpret_cast<std::uint8_t *>(alias),
        sizeof(snd_alias_t));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    const char **strings[] = {
        &alias->aliasName,
        &alias->subtitle,
        &alias->secondaryAliasName,
        &alias->chainAliasName,
    };
    for (const char **string : strings)
    {
        varXString = string;
        Load_XString(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }

    if (alias->soundFile)
    {
        if (reinterpret_cast<std::uintptr_t>(alias->soundFile) == UINT32_MAX)
        {
            alias->soundFile = reinterpret_cast<SoundFile *>(
                AllocLoad_FxElemVisStateSample());
            Load_SoundFile(alias->soundFile, true);
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &alias->soundFile));
        }
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;

    varSndCurvePtr = &alias->volumeFalloffCurve;
    Load_SndCurvePtr(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;

    if (alias->speakerMap)
    {
        if (reinterpret_cast<std::uintptr_t>(alias->speakerMap) == UINT32_MAX)
        {
            alias->speakerMap = reinterpret_cast<SpeakerMap *>(
                AllocLoad_FxElemVisStateSample());
            Load_SpeakerMap(alias->speakerMap, true);
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &alias->speakerMap));
        }
    }
}

void Load_snd_alias_tArray(snd_alias_t *aliases, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedArray(count, sizeof(snd_alias_t),
        "SoundAlias/alias array", bytes)) return;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(aliases),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::int32_t index = 0; index < count; ++index)
    {
        Load_snd_alias_t(&aliases[index]);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_snd_alias_list_t(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varsnd_alias_list_t),
        sizeof(snd_alias_list_t));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(4);
    varXString = &varsnd_alias_list_t->aliasName;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed() && varsnd_alias_list_t->head)
    {
        if (reinterpret_cast<std::uintptr_t>(varsnd_alias_list_t->head) ==
            UINT32_MAX)
        {
            varsnd_alias_list_t->head = reinterpret_cast<snd_alias_t *>(
                AllocLoad_FxElemVisStateSample());
            Load_snd_alias_tArray(varsnd_alias_list_t->head,
                varsnd_alias_list_t->count);
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &varsnd_alias_list_t->head));
        }
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_snd_alias_list_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varsnd_alias_list_ptr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(0);
    if (*varsnd_alias_list_ptr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varsnd_alias_list_ptr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varsnd_alias_list_ptr = reinterpret_cast<snd_alias_list_t *>(
                AllocLoad_FxElemVisStateSample());
            varsnd_alias_list_t = *varsnd_alias_list_ptr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_snd_alias_list_t(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_snd_alias_list_Asset(reinterpret_cast<XAssetHeader *>(
                    varsnd_alias_list_ptr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varsnd_alias_list_ptr)->aliasName);
                if (inserted) *inserted = *varsnd_alias_list_ptr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varsnd_alias_list_ptr));
        }
    }
    DB_PopStreamPos();
}
