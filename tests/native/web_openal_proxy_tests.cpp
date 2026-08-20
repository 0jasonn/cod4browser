#include "web/web_openal_proxy.h"

#include <cassert>
#include <cstdint>

int main()
{
    assert(WebOpenAL_RebaseStarted(10.0, 2.0f, 2.0f) == 9.0);
    assert(WebOpenAL_RebaseStarted(10.0, 3.0f, 1.5f) == 8.0);

    ALuint source = 0;
    alGenSources(1, &source);
    assert(source == 1);

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

    // Invalid IDs fail closed and do not poison a valid later query.
    alSourcePlay(999);
    assert(alGetError() != AL_NONE);
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    assert(state == AL_PLAYING || state == AL_STOPPED);

    alSourceStop(source);
    alDeleteBuffers(1, &buffer);
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
