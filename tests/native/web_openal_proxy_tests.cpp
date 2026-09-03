#include "web/web_openal_proxy.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    assert(WebOpenAL_RebaseStarted(10.0, 2.0f, 2.0f) == 9.0);
    assert(WebOpenAL_RebaseStarted(10.0, 3.0f, 1.5f) == 8.0);

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
    return 0;
}
