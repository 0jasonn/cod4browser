#pragma once

// A deliberately small OpenAL ABI used by the canonical sound driver on web.
// The implementation mirrors driver-visible state in the Worker and forwards
// loaded PCM commands to the main-thread Web Audio owner.  It is not a second
// sound mixer and is never used by native OpenAL builds.

#include <cstddef>
#include <cstdint>

using ALboolean = std::int8_t;
using ALchar = char;
using ALbyte = std::int8_t;
using ALubyte = std::uint8_t;
using ALshort = std::int16_t;
using ALushort = std::uint16_t;
using ALint = std::int32_t;
using ALuint = std::uint32_t;
using ALsizei = std::int32_t;
using ALenum = std::int32_t;
using ALfloat = float;
using ALdouble = double;
using ALvoid = void;

struct ALCdevice;
struct ALCcontext;
using ALCboolean = std::int8_t;
using ALCchar = char;
using ALCbyte = std::int8_t;
using ALCubyte = std::uint8_t;
using ALCshort = std::int16_t;
using ALCushort = std::uint16_t;
using ALCint = std::int32_t;
using ALCuint = std::uint32_t;
using ALCsizei = std::int32_t;
using ALCenum = std::int32_t;
using ALCfloat = float;
using ALCdouble = double;
using ALCvoid = void;

constexpr ALboolean AL_FALSE = 0;
constexpr ALboolean AL_TRUE = 1;
constexpr ALenum AL_NONE = 0;
constexpr ALenum AL_STOPPED = 0x1014;
constexpr ALenum AL_PLAYING = 0x1012;
constexpr ALenum AL_PAUSED = 0x1013;
constexpr ALenum AL_FORMAT_MONO16 = 0x1101;
constexpr ALenum AL_FORMAT_STEREO16 = 0x1103;
constexpr ALenum AL_POSITION = 0x1004;
constexpr ALenum AL_VELOCITY = 0x1006;
constexpr ALenum AL_ORIENTATION = 0x100F;
constexpr ALenum AL_GAIN = 0x100A;
constexpr ALenum AL_PITCH = 0x1003;
constexpr ALenum AL_LOOPING = 0x1007;
constexpr ALenum AL_BUFFER = 0x1009;
constexpr ALenum AL_SOURCE_STATE = 0x1010;
constexpr ALenum AL_BUFFERS_QUEUED = 0x1015;
constexpr ALenum AL_BUFFERS_PROCESSED = 0x1016;
constexpr ALenum AL_SEC_OFFSET = 0x1024;
constexpr ALenum AL_AUXILIARY_SEND_FILTER = 0x20006;
constexpr ALenum AL_DIRECT_FILTER = 0x20005;
constexpr ALenum AL_LOWPASS_GAIN = 0x0001;
constexpr ALenum AL_LOWPASS_GAINHF = 0x0002;
constexpr ALenum AL_FILTER_TYPE = 0x8001;
constexpr ALenum AL_FILTER_LOWPASS = 0x0001;
constexpr ALenum AL_FILTER_HIGHPASS = 0x0002;
constexpr ALenum AL_FILTER_NULL = 0;
constexpr ALenum AL_EFFECT_TYPE = 0x8001;
constexpr ALenum AL_EFFECT_EAXREVERB = 0x8002;
constexpr ALenum AL_EFFECTSLOT_EFFECT = 0x0001;
constexpr ALenum AL_HIGHPASS_GAIN = 0x0001;
constexpr ALenum AL_HIGHPASS_GAINLF = 0x0002;
constexpr ALenum AL_DISTANCE_MODEL = 0xD000;

// EFX values are retained as opaque numeric properties.  Web Audio does not
// claim EFX/reverb parity; the canonical calls remain safe no-ops.
constexpr ALenum AL_EAXREVERB_DENSITY = 0x0001;
constexpr ALenum AL_EAXREVERB_DIFFUSION = 0x0002;
constexpr ALenum AL_EAXREVERB_GAIN = 0x0003;
constexpr ALenum AL_EAXREVERB_GAINHF = 0x0004;
constexpr ALenum AL_EAXREVERB_GAINLF = 0x0005;
constexpr ALenum AL_EAXREVERB_DECAY_TIME = 0x0006;
constexpr ALenum AL_EAXREVERB_DECAY_HFRATIO = 0x0007;
constexpr ALenum AL_EAXREVERB_DECAY_LFRATIO = 0x0008;
constexpr ALenum AL_EAXREVERB_REFLECTIONS_GAIN = 0x0009;
constexpr ALenum AL_EAXREVERB_REFLECTIONS_DELAY = 0x000A;
constexpr ALenum AL_EAXREVERB_REFLECTIONS_PAN = 0x000B;
constexpr ALenum AL_EAXREVERB_LATE_REVERB_GAIN = 0x000C;
constexpr ALenum AL_EAXREVERB_LATE_REVERB_DELAY = 0x000D;
constexpr ALenum AL_EAXREVERB_LATE_REVERB_PAN = 0x000E;
constexpr ALenum AL_EAXREVERB_ECHO_TIME = 0x000F;
constexpr ALenum AL_EAXREVERB_ECHO_DEPTH = 0x0010;
constexpr ALenum AL_EAXREVERB_MODULATION_TIME = 0x0011;
constexpr ALenum AL_EAXREVERB_MODULATION_DEPTH = 0x0012;
constexpr ALenum AL_EAXREVERB_AIR_ABSORPTION_GAINHF = 0x0013;
constexpr ALenum AL_EAXREVERB_HFREFERENCE = 0x0014;
constexpr ALenum AL_EAXREVERB_LFREFERENCE = 0x0015;
constexpr ALenum AL_EAXREVERB_ROOM_ROLLOFF_FACTOR = 0x0016;
constexpr ALenum AL_EAXREVERB_DECAY_HFLIMIT = 0x0017;
constexpr ALCenum ALC_FREQUENCY = 0x1007;

// Pure timing helper shared by the proxy and deterministic native tests.
// Device-only playback observations; canonical SND remains the channel owner.
std::uint32_t WebOpenAL_SourceGeneration(ALuint source);
bool WebOpenAL_ApplyPlayback(ALuint source, std::uint32_t generation,
    std::uint32_t processed, double offset, ALenum state);

// Retains only a bounded diagnostic identity for the next canonical source
// commands. This never participates in OpenAL state or playback selection.
void WebOpenAL_SetSourceAlias(ALuint source, const char *aliasName);

// Cumulative device playback for a stream, including buffers already unqueued.
// False reports a failed device; no wall-time estimate is substituted here.
bool WebOpenAL_SourcePlaybackSeconds(ALuint source, double &seconds);

// Device-only snapshot in native stage/band order: enabled, type, gain (dB),
// frequency (Hz), Q. Canonical SndEqParams and entchannel ownership stay in SND.
void WebOpenAL_SetSourceEq(ALuint source, const float (&bands)[6][5]);

// Narrow device operations; canonical room/alias/wet-fade state stays in SND.
void WebOpenAL_SetRoomType(int room);
void WebOpenAL_SetReverbSend(ALuint source, float wet);

ALenum alGetError();
void alDistanceModel(ALenum);
void alListener3f(ALenum, ALfloat, ALfloat, ALfloat);
void alListenerfv(ALenum, const ALfloat *);
void alGenSources(ALsizei, ALuint *);
void alDeleteSources(ALsizei, const ALuint *);
void alGenBuffers(ALsizei, ALuint *);
void alDeleteBuffers(ALsizei, const ALuint *);
void alBufferData(ALuint, ALenum, const ALvoid *, ALsizei, ALsizei);
void alSource3f(ALuint, ALenum, ALfloat, ALfloat, ALfloat);
void alSource3i(ALuint, ALenum, ALint, ALint, ALint);
void alSourcef(ALuint, ALenum, ALfloat);
void alSourcei(ALuint, ALenum, ALint);
void alGetSourcef(ALuint, ALenum, ALfloat *);
void alGetSource3f(ALuint, ALenum, ALfloat *, ALfloat *, ALfloat *);
void alGetSourcei(ALuint, ALenum, ALint *);
void alSourcePlay(ALuint);
void alSourcePause(ALuint);
void alSourceStop(ALuint);
void alSourceQueueBuffers(ALuint, ALsizei, const ALuint *);
void alSourceUnqueueBuffers(ALuint, ALsizei, ALuint *);
void alEffectf(ALuint, ALenum, ALfloat);
void alEffectfv(ALuint, ALenum, const ALfloat *);
void alEffecti(ALuint, ALenum, ALint);
void alFilterf(ALuint, ALenum, ALfloat);
void alFilteri(ALuint, ALenum, ALint);
void alAuxiliaryEffectSloti(ALuint, ALenum, ALint);
void alGenAuxiliaryEffectSlots(ALsizei, ALuint *);
void alDeleteAuxiliaryEffectSlots(ALsizei, const ALuint *);
void alGenEffects(ALsizei, ALuint *);
void alDeleteEffects(ALsizei, const ALuint *);
void alGenFilters(ALsizei, ALuint *);
void alDeleteFilters(ALsizei, const ALuint *);

ALCdevice *alcOpenDevice(const ALCchar *);
ALCboolean alcCloseDevice(ALCdevice *);
ALCcontext *alcCreateContext(ALCdevice *, const ALCint *);
void alcDestroyContext(ALCcontext *);
ALCboolean alcMakeContextCurrent(ALCcontext *);
