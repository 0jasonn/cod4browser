#include "web_openal_proxy.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace
{
constexpr std::size_t MAX_SOURCES = 53;
constexpr std::size_t MAX_BUFFERS = 512;
constexpr std::size_t MAX_PCM_BYTES = 16u * 1024u * 1024u;

struct BufferState
{
    bool live = false;
    ALenum format = AL_NONE;
    ALsizei bytes = 0;
    ALsizei rate = 0;
};

struct SourceState
{
    bool live = false;
    ALuint buffer = 0;
    ALenum state = AL_STOPPED;
    ALfloat gain = 1.0f;
    ALfloat pitch = 1.0f;
    ALfloat offset = 0.0f;
    ALfloat position[3] = {};
    bool spatialized = false;
    bool looping = false;
    double started = 0.0;
    double lastRefresh = 0.0;
    std::uint32_t generation = 0;
    std::deque<ALuint> queue;
    std::size_t processed = 0;
    double queueOffset = 0.0;
    std::string diagnosticAlias;
};

std::array<BufferState, MAX_BUFFERS + 1> g_buffers;
std::array<SourceState, MAX_SOURCES + 1> g_sources;
ALuint g_next_buffer = 1;
ALenum g_error = AL_NONE;
bool g_context_current = false;

double now_seconds()
{
    using clock = std::chrono::steady_clock;
    static const auto epoch = clock::now();
    return std::chrono::duration<double>(clock::now() - epoch).count();
}

bool source_valid(ALuint id)
{
    return id > 0 && id <= MAX_SOURCES && g_sources[id].live;
}

bool buffer_valid(ALuint id)
{
    return id > 0 && id <= MAX_BUFFERS && g_buffers[id].live;
}

void fail(ALenum error = 0xA003 /* AL_INVALID_VALUE */)
{
    if (g_error == AL_NONE)
        g_error = error;
}

#if defined(__EMSCRIPTEN__)
void emit_source(ALuint id, const char *op)
{
    const SourceState &source = g_sources[id];
    EM_ASM({
        if (typeof self !== "undefined" && typeof self.postMessage === "function") {
            self.postMessage({type: "audio-command", version: 1, op: UTF8ToString($1),
                sourceId: $0, generation: $2, bufferId: $3, gain: $4, pitch: $5,
                looping: !!$6, offset: $7, x: $8, y: $9, z: $10,
                aliasName: UTF8ToString($11), spatialized: !!$12,
                queueProcessed: $13});
        }
    }, id, op, source.generation, source.buffer, source.gain, source.pitch,
       source.looping ? 1 : 0, source.offset, source.position[0], source.position[1],
       source.position[2], source.diagnosticAlias.c_str(),
       source.spatialized ? 1 : 0,
       static_cast<ALint>(source.processed));
}

void emit_simple(const char *op, ALuint id)
{
    EM_ASM({
        if (typeof self !== "undefined" && typeof self.postMessage === "function")
            self.postMessage({type: "audio-command", version: 1, op: UTF8ToString($0), id: $1});
    }, op, id);
}

void emit_buffer_list(ALuint id, const char *op, const ALuint *buffers,
    ALsizei count)
{
    const SourceState &source = g_sources[id];
    EM_ASM({
        if (typeof self !== "undefined" && typeof self.postMessage === "function") {
            const begin = $3 >>> 2;
            self.postMessage({type: "audio-command", version: 1,
                op: UTF8ToString($1), sourceId: $0, generation: $2,
                bufferIds: Array.from(HEAPU32.subarray(begin, begin + $4))});
        }
    }, id, op, source.generation, buffers, count);
}

void emit_reset()
{
    EM_ASM({
        if (typeof self !== "undefined" && typeof self.postMessage === "function")
            self.postMessage({type: "audio-command", version: 1, op: "device-reset"});
    });
}
#else
void emit_source(ALuint, const char *) {}
void emit_simple(const char *, ALuint) {}
void emit_buffer_list(ALuint, const char *, const ALuint *, ALsizei) {}
void emit_reset() {}
#endif

double buffer_duration(ALuint id)
{
    if (!buffer_valid(id)) return 0.0;
    const BufferState &buffer = g_buffers[id];
    const int channels = buffer.format == AL_FORMAT_STEREO16 ? 2 : 1;
    const double frames = channels > 0
        ? buffer.bytes / (2.0 * channels) : 0.0;
    return buffer.rate > 0 ? frames / buffer.rate : 0.0;
}

void refresh_state_at(SourceState &source, double current)
{
    if (source.state != AL_PLAYING)
        return;
    if (!source.queue.empty())
    {
        const double elapsed = std::max(0.0, current - source.lastRefresh) *
            std::max(0.001f, source.pitch);
        source.lastRefresh = current;
        source.queueOffset += elapsed;
        while (source.processed < source.queue.size())
        {
            const double duration = buffer_duration(
                source.queue[source.processed]);
            if (duration > 0.0 && source.queueOffset < duration)
                break;
            source.queueOffset = duration > 0.0
                ? std::max(0.0, source.queueOffset - duration) : 0.0;
            ++source.processed;
        }
        source.offset = static_cast<ALfloat>(source.queueOffset);
        if (source.processed >= source.queue.size())
        {
            source.offset = 0.0f;
            source.queueOffset = 0.0;
            source.state = AL_STOPPED;
        }
        return;
    }
    if (source.looping || !buffer_valid(source.buffer))
        return;
    const BufferState &buffer = g_buffers[source.buffer];
    const int channels = buffer.format == AL_FORMAT_STEREO16 ? 2 : 1;
    const double frames = channels > 0 ? (buffer.bytes / (2.0 * channels)) : 0.0;
    const double duration = buffer.rate > 0 ? frames / buffer.rate : 0.0;
    const double elapsed = (current - source.started) * std::max(0.001f, source.pitch);
    source.offset = static_cast<ALfloat>(std::max(0.0, elapsed));
    if (duration > 0.0 && elapsed >= duration)
    {
        source.offset = static_cast<ALfloat>(duration);
        source.state = AL_STOPPED;
    }
}

void refresh_state(SourceState &source)
{
    refresh_state_at(source, now_seconds());
}

void source_property(ALuint id)
{
    emit_source(id, "source-property");
}
}

double WebOpenAL_RebaseStarted(double nowSeconds, float offsetSeconds, float pitch)
{
    return nowSeconds - static_cast<double>(offsetSeconds) /
        std::max(0.001f, pitch);
}

void WebOpenAL_SetSourceAlias(ALuint source, const char *aliasName)
{
    if (!source_valid(source))
        return;
    const char *value = aliasName ? aliasName : "";
    g_sources[source].diagnosticAlias.assign(
        value, std::min(std::strlen(value), std::size_t(128u)));
}

ALenum alGetError()
{
    const ALenum result = g_error;
    g_error = AL_NONE;
    return result;
}

void alDistanceModel(ALenum) {}
void alListener3f(ALenum, ALfloat, ALfloat, ALfloat) {}
void alListenerfv(ALenum, const ALfloat *) {}

void alGenSources(ALsizei count, ALuint *ids)
{
    if (count < 0 || (count > 0 && !ids) || static_cast<std::size_t>(count) > MAX_SOURCES)
    {
        fail();
        return;
    }
    for (ALsizei i = 0; i < count; ++i)
    {
        const ALuint id = static_cast<ALuint>(i + 1);
        g_sources[id] = SourceState{};
        g_sources[id].live = true;
        ids[i] = id;
        emit_simple("source-create", id);
    }
}

void alDeleteSources(ALsizei count, const ALuint *ids)
{
    if (count < 0 || (count > 0 && !ids))
    {
        fail();
        return;
    }
    for (ALsizei i = 0; i < count; ++i)
    {
        if (!source_valid(ids[i]))
        {
            fail();
            continue;
        }
        ++g_sources[ids[i]].generation;
        emit_source(ids[i], "source-delete");
        g_sources[ids[i]] = SourceState{};
    }
}

void alGenBuffers(ALsizei count, ALuint *ids)
{
    if (count < 0 || (count > 0 && !ids))
    {
        fail();
        return;
    }
    for (ALsizei i = 0; i < count; ++i)
    {
        ALuint id = 0;
        for (std::size_t tries = 0; tries < MAX_BUFFERS; ++tries)
        {
            const ALuint candidate = g_next_buffer++;
            if (g_next_buffer > MAX_BUFFERS)
                g_next_buffer = 1;
            if (!g_buffers[candidate].live)
            {
                id = candidate;
                break;
            }
        }
        if (!id)
        {
            fail(0xA001 /* AL_INVALID_NAME */);
            ids[i] = 0;
            continue;
        }
        g_buffers[id] = BufferState{};
        g_buffers[id].live = true;
        ids[i] = id;
    }
}

void alDeleteBuffers(ALsizei count, const ALuint *ids)
{
    if (count < 0 || (count > 0 && !ids))
    {
        fail();
        return;
    }
    for (ALsizei i = 0; i < count; ++i)
    {
        if (!buffer_valid(ids[i]))
        {
            fail();
            continue;
        }
        emit_simple("buffer-delete", ids[i]);
        g_buffers[ids[i]] = BufferState{};
    }
}

void alBufferData(ALuint id, ALenum format, const ALvoid *data, ALsizei bytes, ALsizei rate)
{
    const bool validFormat = format == AL_FORMAT_MONO16 || format == AL_FORMAT_STEREO16;
    if (!buffer_valid(id) || !validFormat || !data || bytes <= 0 ||
        static_cast<std::size_t>(bytes) > MAX_PCM_BYTES || rate <= 0)
    {
        fail();
        return;
    }
    g_buffers[id].format = format;
    g_buffers[id].bytes = bytes;
    g_buffers[id].rate = rate;
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        if (typeof self !== "undefined" && typeof self.postMessage === "function") {
            const pcm = HEAPU8.slice($1, $1 + $2).buffer;
            self.postMessage({type: "audio-command", version: 1, op: "buffer-upload",
                bufferId: $0, format: $3, bytes: $2, rate: $4, pcm}, [pcm]);
        }
    }, id, data, bytes, format, rate);
#endif
}

void alSource3f(ALuint id, ALenum parameter, ALfloat x, ALfloat y, ALfloat z)
{
    if (!source_valid(id) || parameter != AL_POSITION)
    {
        fail();
        return;
    }
    auto &source = g_sources[id];
    source.position[0] = x;
    source.position[1] = y;
    source.position[2] = z;
    source.spatialized = true;
    source_property(id);
}

void alSource3i(ALuint id, ALenum, ALint, ALint, ALint)
{
    if (!source_valid(id)) fail();
}

void alSourcef(ALuint id, ALenum parameter, ALfloat value)
{
    if (!source_valid(id))
    {
        fail();
        return;
    }
    auto &source = g_sources[id];
    const double mutationTime = now_seconds();
    refresh_state_at(source, mutationTime);
    switch (parameter)
    {
    case AL_GAIN: source.gain = std::max(0.0f, value); break;
    case AL_PITCH:
        source.pitch = std::max(0.001f, value);
        if (source.state == AL_PLAYING && source.queue.empty())
            source.started = WebOpenAL_RebaseStarted(mutationTime, source.offset, source.pitch);
        break;
    case AL_SEC_OFFSET:
        source.offset = std::max(0.0f, value);
        source.queueOffset = source.offset;
        if (source.state == AL_PLAYING && source.queue.empty())
            source.started = WebOpenAL_RebaseStarted(mutationTime, source.offset, source.pitch);
        break;
    default: fail(); return;
    }
    source_property(id);
}

void alSourcei(ALuint id, ALenum parameter, ALint value)
{
    if (!source_valid(id))
    {
        fail();
        return;
    }
    auto &source = g_sources[id];
    const double mutationTime = now_seconds();
    refresh_state_at(source, mutationTime);
    switch (parameter)
    {
    case AL_BUFFER:
        if (value && !buffer_valid(static_cast<ALuint>(value))) { fail(); return; }
        source.buffer = static_cast<ALuint>(value);
        source.offset = 0.0f;
        source.queueOffset = 0.0;
        break;
    case AL_LOOPING: source.looping = value != AL_FALSE; break;
    case AL_SEC_OFFSET:
        source.offset = std::max(0, value);
        source.queueOffset = source.offset;
        if (source.state == AL_PLAYING && source.queue.empty())
            source.started = WebOpenAL_RebaseStarted(mutationTime, source.offset, source.pitch);
        break;
    default: fail(); return;
    }
    source_property(id);
}

void alGetSourcef(ALuint id, ALenum parameter, ALfloat *value)
{
    if (!source_valid(id) || !value)
    {
        fail();
        return;
    }
    auto &source = g_sources[id];
    refresh_state(source);
    switch (parameter)
    {
    case AL_GAIN: *value = source.gain; break;
    case AL_PITCH: *value = source.pitch; break;
    case AL_SEC_OFFSET: *value = source.offset; break;
    default: fail(); break;
    }
}

void alGetSource3f(ALuint id, ALenum parameter, ALfloat *x, ALfloat *y, ALfloat *z)
{
    if (!source_valid(id) || parameter != AL_POSITION || !x || !y || !z)
    {
        fail();
        return;
    }
    *x = g_sources[id].position[0];
    *y = g_sources[id].position[1];
    *z = g_sources[id].position[2];
}

void alGetSourcei(ALuint id, ALenum parameter, ALint *value)
{
    if (!source_valid(id) || !value)
    {
        fail();
        return;
    }
    auto &source = g_sources[id];
    refresh_state(source);
    switch (parameter)
    {
    case AL_SOURCE_STATE: *value = source.state; break;
    case AL_BUFFERS_QUEUED:
        *value = static_cast<ALint>(source.queue.size());
        break;
    case AL_BUFFERS_PROCESSED:
        *value = static_cast<ALint>(source.processed);
        break;
    default: fail(); break;
    }
}

void alSourcePlay(ALuint id)
{
    if (!source_valid(id)) { fail(); return; }
    auto &source = g_sources[id];
    source.state = AL_PLAYING;
    const double current = now_seconds();
    source.started = current - source.offset / std::max(0.001f, source.pitch);
    source.lastRefresh = current;
    ++source.generation;
    emit_source(id, "source-play");
}

void alSourcePause(ALuint id)
{
    if (!source_valid(id)) { fail(); return; }
    auto &source = g_sources[id];
    refresh_state(source);
    source.state = AL_PAUSED;
    emit_source(id, "source-pause");
}

void alSourceStop(ALuint id)
{
    if (!source_valid(id)) { fail(); return; }
    auto &source = g_sources[id];
    source.state = AL_STOPPED;
    source.offset = 0.0f;
    source.queueOffset = 0.0;
    if (!source.queue.empty())
        source.processed = source.queue.size();
    ++source.generation;
    emit_source(id, "source-stop");
}

void alSourceQueueBuffers(ALuint id, ALsizei count, const ALuint *buffers)
{
    if (!source_valid(id) || count < 0 || (count > 0 && !buffers)) { fail(); return; }
    for (ALsizei i = 0; i < count; ++i)
        if (!buffer_valid(buffers[i])) { fail(); return; }
    auto &source = g_sources[id];
    for (ALsizei i = 0; i < count; ++i)
        source.queue.push_back(buffers[i]);
    emit_buffer_list(id, "source-queue", buffers, count);
}

void alSourceUnqueueBuffers(ALuint id, ALsizei count, ALuint *buffers)
{
    if (!source_valid(id) || count < 0 || (count > 0 && !buffers)) { fail(); return; }
    auto &source = g_sources[id];
    const ALsizei removed = std::min<ALsizei>(count,
        static_cast<ALsizei>(source.processed));
    if (removed != count) { fail(); return; }
    for (ALsizei i = 0; i < removed; ++i)
    {
        buffers[i] = source.queue.front();
        source.queue.pop_front();
    }
    source.processed -= removed;
    emit_buffer_list(id, "source-unqueue", buffers, removed);
}

void alEffectf(ALuint, ALenum, ALfloat) {}
void alEffectfv(ALuint, ALenum, const ALfloat *) {}
void alEffecti(ALuint, ALenum, ALint) {}
void alFilterf(ALuint, ALenum, ALfloat) {}
void alFilteri(ALuint, ALenum, ALint) {}
void alAuxiliaryEffectSloti(ALuint, ALenum, ALint) {}
void alGenAuxiliaryEffectSlots(ALsizei count, ALuint *ids) { alGenBuffers(count, ids); }
void alDeleteAuxiliaryEffectSlots(ALsizei count, const ALuint *ids) { alDeleteBuffers(count, ids); }
void alGenEffects(ALsizei count, ALuint *ids) { alGenBuffers(count, ids); }
void alDeleteEffects(ALsizei count, const ALuint *ids) { alDeleteBuffers(count, ids); }
void alGenFilters(ALsizei count, ALuint *ids) { alGenBuffers(count, ids); }
void alDeleteFilters(ALsizei count, const ALuint *ids) { alDeleteBuffers(count, ids); }

struct ALCdevice {};
struct ALCcontext {};
static ALCdevice g_device;
static ALCcontext g_context;

void reset_proxy_state()
{
    g_sources.fill(SourceState{});
    g_buffers.fill(BufferState{});
    g_next_buffer = 1;
    g_error = AL_NONE;
    g_context_current = false;
    emit_reset();
}

ALCdevice *alcOpenDevice(const ALCchar *) { return &g_device; }
ALCboolean alcCloseDevice(ALCdevice *device) { return device == &g_device; }
ALCcontext *alcCreateContext(ALCdevice *device, const ALCint *)
{
    if (device != &g_device)
        return nullptr;
    reset_proxy_state();
    return &g_context;
}
void alcDestroyContext(ALCcontext *context)
{
    if (context == &g_context)
        reset_proxy_state();
}
ALCboolean alcMakeContextCurrent(ALCcontext *context)
{
    if (context != nullptr && context != &g_context)
        return AL_FALSE;
    g_context_current = context == &g_context;
    return AL_TRUE;
}

// Platform-only probe used by the served Worker smoke. It exercises the same
// proxy upload path as LoadedSound without creating an alias, channel, or
// gameplay event.
extern "C" int KisakWeb_TestAudioProxyPcm()
{
    ALuint buffer = 0;
    const ALshort pcm[2] = { 0, 8192 };
    alGenBuffers(1, &buffer);
    if (!buffer)
        return 0;
    alBufferData(buffer, AL_FORMAT_MONO16, pcm, sizeof(pcm), 44100);
    const bool accepted = alGetError() == AL_NONE;
    alDeleteBuffers(1, &buffer);
    return accepted ? 1 : 0;
}
