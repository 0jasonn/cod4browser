#pragma once

#include <cstdint>

// Portable serialized sound metadata.  Keep these definitions independent of
// the Miles headers so database and platform code can own/inspect the native
// objects without pulling an audio runtime into the build.
struct _AILSOUNDINFO_COD4
{
    int format;
    const void *data_ptr;
    std::uint32_t data_len;
    std::uint32_t rate;
    int bits;
    int channels;
    std::uint32_t samples;
    std::uint32_t block_size;
    const void *initial_ptr;
};

struct MssSoundCOD4
{
    _AILSOUNDINFO_COD4 info;
    std::uint8_t *data;
};

struct LoadedSound
{
    const char *name;
    MssSoundCOD4 sound;
};

struct StreamFileNameRaw
{
    const char *dir;
    const char *name;
};

union StreamFileInfo
{
    StreamFileNameRaw raw;
};

struct StreamFileName
{
    StreamFileInfo info;
};

struct StreamedSound
{
    StreamFileName filename;
};

union SoundFileRef
{
    LoadedSound *loadSnd;
    StreamedSound streamSnd;
};

struct SoundFile
{
    std::uint8_t type;
    std::uint8_t exists;
    std::uint8_t padding[2];
    SoundFileRef u;
};

struct SndCurve
{
    const char *filename;
    int knotCount;
    float knots[8][2];
};

struct MSSSpeakerLevels
{
    int speaker;
    int numLevels;
    float levels[2];
};

struct MSSChannelMap
{
    int speakerCount;
    MSSSpeakerLevels speakers[6];
};

struct SpeakerMap
{
    bool isDefault;
    std::uint8_t padding[3];
    const char *name;
    MSSChannelMap channelMaps[2][2];
};

struct snd_alias_t
{
    const char *aliasName;
    const char *subtitle;
    const char *secondaryAliasName;
    const char *chainAliasName;
    SoundFile *soundFile;
    int sequence;
    float volMin;
    float volMax;
    float pitchMin;
    float pitchMax;
    float distMin;
    float distMax;
    int flags;
    float slavePercentage;
    float probability;
    float lfePercentage;
    float centerPercentage;
    int startDelay;
    SndCurve *volumeFalloffCurve;
    float envelopMin;
    float envelopMax;
    float envelopPercentage;
    SpeakerMap *speakerMap;
};

struct snd_alias_list_t
{
    const char *aliasName;
    snd_alias_t *head;
    int count;
};

static_assert(sizeof(SoundFile) == 12);
static_assert(sizeof(_AILSOUNDINFO_COD4) == 36);
static_assert(sizeof(MssSoundCOD4) == 40);
static_assert(sizeof(LoadedSound) == 44);
static_assert(sizeof(SndCurve) == 72);
static_assert(sizeof(SpeakerMap) == 408);
static_assert(sizeof(snd_alias_t) == 92);
static_assert(sizeof(snd_alias_list_t) == 12);
