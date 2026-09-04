#include "web/web_openal_proxy.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    ALuint source = 0;
    alGenSources(1, &source);
    assert(source == 1);

    WebOpenAL_SetRoomType(25);
    WebOpenAL_SetReverbSend(source, 0.5f);
    assert(alGetError() == AL_NONE);
    WebOpenAL_SetRoomType(-1);
    assert(alGetError() != AL_NONE);
    WebOpenAL_SetRoomType(26);
    assert(alGetError() != AL_NONE);
    WebOpenAL_SetReverbSend(999, 0.5f);
    assert(alGetError() != AL_NONE);
    WebOpenAL_SetReverbSend(source, std::numeric_limits<float>::quiet_NaN());
    assert(alGetError() != AL_NONE);
    WebOpenAL_SetReverbSend(source, -0.1f);
    assert(alGetError() != AL_NONE);
    WebOpenAL_SetReverbSend(source, 1.1f);
    assert(alGetError() != AL_NONE);
    WebOpenAL_SetReverbSend(source, 0);
    assert(alGetError() == AL_NONE);

    float bands[6][5] = {};
    bands[5][0] = 1; bands[5][1] = 4;
    bands[5][2] = -6; bands[5][3] = 1000; bands[5][4] = 0.75f;
    WebOpenAL_SetSourceEq(source, bands);
    assert(alGetError() == AL_NONE);
    bands[5][4] = 0;
    WebOpenAL_SetSourceEq(source, bands);
    assert(alGetError() != AL_NONE);
    bands[5][4] = std::numeric_limits<float>::infinity();
    WebOpenAL_SetSourceEq(source, bands);
    assert(alGetError() != AL_NONE);
    bands[5][4] = 1;
    bands[5][1] = 1.5f;
    WebOpenAL_SetSourceEq(source, bands);
    assert(alGetError() != AL_NONE);
    bands[5][0] = 0;
    WebOpenAL_SetSourceEq(source, bands);
    assert(alGetError() == AL_NONE);

    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    assert(buffer == 1);
    const std::int16_t pcm[2] = { 0, 16384 };
    alBufferData(buffer, AL_FORMAT_MONO16, pcm, sizeof(pcm), 44100);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, 0.25f);
    alSourcef(source, AL_PITCH, 1.5f);
    alSourcePlay(source);

    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    assert(state == AL_PLAYING);
    ALfloat gain = 0.0f;
    alGetSourcef(source, AL_GAIN, &gain);
    assert(gain == 0.25f);

    // Additional allocations must preserve live game sources. The final slot
    // belongs to the movie device, outside the 53 canonical SND channels.
    ALuint remainingSources[53]{};
    alGenSources(53, remainingSources);
    assert(alGetError() == AL_NONE);
    assert(remainingSources[0] == 2 && remainingSources[52] == 54);
    alGetSourcef(source, AL_GAIN, &gain);
    assert(gain == 0.25f);
    ALuint exhausted = 0;
    alGenSources(1, &exhausted);
    assert(alGetError() != AL_NONE && exhausted == 0);
    alDeleteSources(53, remainingSources);
    ALuint reused = 0;
    alGenSources(1, &reused);
    assert(reused == 2);
    alDeleteSources(1, &reused);

    // Invalid IDs fail closed and do not poison a valid later query.
    alSourcePlay(999);
    assert(alGetError() != AL_NONE);
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    assert(state == AL_PLAYING || state == AL_STOPPED);

    alSourceStop(source);
    alSourcei(source, AL_BUFFER, 0);
    alDeleteBuffers(1, &buffer);

    // Queued streaming buffers retain order and become unqueueable once a
    // stopped source reports them as processed, matching the refill contract
    // used by SND_FillStreamBuffers.
    ALuint streamBuffers[2] = {};
    alGenBuffers(2, streamBuffers);
    assert(streamBuffers[0] != 0 && streamBuffers[1] != 0);
    alBufferData(streamBuffers[0], AL_FORMAT_MONO16, pcm, sizeof(pcm), 44100);
    alBufferData(streamBuffers[1], AL_FORMAT_MONO16, pcm, sizeof(pcm), 44100);
    alSourceQueueBuffers(source, 2, streamBuffers);
    ALint queued = 0;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
    assert(queued == 2);
    alSourcePlay(source);
    alSourceStop(source);
    ALint processed = 0;
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
    assert(processed == 2);
    ALuint unqueued[2] = {};
    alSourceUnqueueBuffers(source, 2, unqueued);
    assert(unqueued[0] == streamBuffers[0]);
    assert(unqueued[1] == streamBuffers[1]);
    alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
    assert(queued == 0);
    alDeleteBuffers(2, streamBuffers);
    alDeleteSources(1, &source);

    // Context teardown is a bounded reset, so a fresh init deterministically
    // reuses IDs and has no stale error/state.
    ALCdevice *device = alcOpenDevice(nullptr);
    ALCcontext *context = alcCreateContext(device, nullptr);
    assert(alcMakeContextCurrent(context));
    ALuint resetBuffer = 0;
    alGenBuffers(1, &resetBuffer);
    assert(resetBuffer == 1);
    assert(alcMakeContextCurrent(nullptr));
    alcDestroyContext(context);
    assert(alGetError() == AL_NONE);
    ALuint reusedBuffer = 0;
    alGenBuffers(1, &reusedBuffer);
    assert(reusedBuffer == 1);
    alDeleteBuffers(1, &reusedBuffer);
    alcCloseDevice(device);
    // The proxy must wait for the device rather than infer completion from
    // Worker elapsed time. Feedback is generation- and queue-prefix-safe.
    ALuint clockSource = 0, clockBuffers[3]{};
    alGenSources(1, &clockSource);
    alGenBuffers(3, clockBuffers);
    const std::int16_t clockPcm[8]{};
    for (ALuint id : clockBuffers)
        alBufferData(id, AL_FORMAT_MONO16, clockPcm, sizeof(clockPcm), 8);
    alSourcei(clockSource, AL_BUFFER, clockBuffers[0]);
    alSourcePlay(clockSource);
    auto generation = WebOpenAL_SourceGeneration(clockSource);
    ALfloat offset = -1;
    alGetSourcef(clockSource, AL_SEC_OFFSET, &offset);
    assert(offset == 0);
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 0, 0.75, AL_PLAYING));
    alSourcef(clockSource, AL_PITCH, 2);
    alGetSourcef(clockSource, AL_SEC_OFFSET, &offset);
    assert(offset == 0.75f);
    alSourcePause(clockSource);
    assert(!WebOpenAL_ApplyPlayback(clockSource, generation, 0, 1, AL_STOPPED));
    generation = WebOpenAL_SourceGeneration(clockSource);
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 0, 0.8, AL_PAUSED));
    alGetSourcef(clockSource, AL_SEC_OFFSET, &offset);
    assert(offset == 0.8f);
    alSourcePlay(clockSource);
    assert(!WebOpenAL_ApplyPlayback(clockSource, generation, 0, 1, AL_STOPPED));
    generation = WebOpenAL_SourceGeneration(clockSource);
    assert(!WebOpenAL_ApplyPlayback(clockSource, generation, 0,
        std::numeric_limits<double>::quiet_NaN(), AL_PLAYING));
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 0, 1, AL_STOPPED));
    alGetSourcei(clockSource, AL_SOURCE_STATE, &state);
    assert(state == AL_STOPPED);

    alSourcei(clockSource, AL_BUFFER, 0);
    alSourceQueueBuffers(clockSource, 2, clockBuffers);
    alSourcePlay(clockSource);
    generation = WebOpenAL_SourceGeneration(clockSource);
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 1, 0.25, AL_PLAYING));
    double movieSeconds = -1;
    assert(WebOpenAL_SourcePlaybackSeconds(clockSource, movieSeconds));
    assert(movieSeconds == 1.25);
    ALuint retired = 0;
    alSourceUnqueueBuffers(clockSource, 1, &retired);
    assert(retired == clockBuffers[0]);
    assert(WebOpenAL_SourcePlaybackSeconds(clockSource, movieSeconds));
    assert(movieSeconds == 1.25);
    assert(!WebOpenAL_ApplyPlayback(clockSource, generation, 0, 0, AL_PLAYING));
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 1, 0.5, AL_PLAYING));
    assert(WebOpenAL_SourcePlaybackSeconds(clockSource, movieSeconds));
    assert(movieSeconds == 1.5);
    alGetSourcei(clockSource, AL_BUFFERS_PROCESSED, &processed);
    assert(processed == 0);
    alSourceQueueBuffers(clockSource, 1, &clockBuffers[2]);
    // The device exhausted its older tail while the new queue command was in flight.
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 2, 0, AL_STOPPED));
    alGetSourcei(clockSource, AL_SOURCE_STATE, &state);
    assert(state == AL_PLAYING);
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 3, 0, AL_STOPPED));
    assert(WebOpenAL_SourcePlaybackSeconds(clockSource, movieSeconds));
    assert(movieSeconds == 3);
    alGetSourcei(clockSource, AL_BUFFERS_PROCESSED, &processed);
    assert(processed == 2);
    alDeleteSources(1, &clockSource);
    alGenSources(1, &clockSource);
    alSourcei(clockSource, AL_BUFFER, clockBuffers[0]);
    alSourcePlay(clockSource);
    assert(!WebOpenAL_ApplyPlayback(clockSource, generation, 0, 1, AL_STOPPED));
    generation = WebOpenAL_SourceGeneration(clockSource);
    assert(WebOpenAL_ApplyPlayback(clockSource, generation, 0, 0, AL_NONE));
    alGetSourcei(clockSource, AL_SOURCE_STATE, &state);
    assert(state == AL_STOPPED); // explicit device failure retires a muted channel
    assert(!WebOpenAL_SourcePlaybackSeconds(clockSource, movieSeconds));
    alDeleteSources(1, &clockSource);
    alDeleteBuffers(3, clockBuffers);
    return 0;
}
