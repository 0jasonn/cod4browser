#include <database/db_generated_sound_platform.h>
#include <sound/snd_alias_types.h>
#include <universal/q_shared.h>

#if defined(KISAK_WEB) && defined(KISAK_OPENAL)
void __cdecl SND_SetData(MssSoundCOD4 *sound, void *sourceData);
#endif

void DB_PlatformSetLoadedSoundData(MssSoundCOD4 *sound, void *sourceData)
{
    iassert(sound && sourceData);
#if defined(KISAK_WEB) && defined(KISAK_OPENAL)
    // Match the native generated loader: LoadedSound payloads must be copied
    // into sound-owned storage before the fastfile stream advances. Keeping a
    // pointer into the stream block makes later SFX read recycled zone bytes.
    SND_SetData(sound, sourceData);
#else
    sound->data = static_cast<std::uint8_t *>(sourceData);
    sound->info.data_ptr = sourceData;
    sound->info.initial_ptr = sourceData;
#endif
}
