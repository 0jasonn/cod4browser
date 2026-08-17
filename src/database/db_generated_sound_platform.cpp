#include <database/db_generated_sound_platform.h>
#include <sound/snd_alias_types.h>
#include <universal/q_shared.h>

void DB_PlatformSetLoadedSoundData(MssSoundCOD4 *sound, void *sourceData)
{
    iassert(sound && sourceData);
    // The native Miles/OpenAL boundary may copy or resample here. Until the
    // browser audio backend owns loaded payloads, retain the zone-owned bytes
    // while preserving the canonical MssSound pointer contract.
    sound->data = static_cast<std::uint8_t *>(sourceData);
    sound->info.data_ptr = sourceData;
    sound->info.initial_ptr = sourceData;
}
