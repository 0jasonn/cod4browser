#include <gfx_d3d/r_cinematic_api.h>
#include <client/client.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_rendercmds.h>
#include <sound/snd_public.h>
#include <universal/com_files.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <web/web_cinematic_decoder.h>
#include <web/web_openal_proxy.h>
#include <web/web_renderer.h>
#include <emscripten.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
EM_JS(void, PublishMovie, (const char *state, const char *name, const char *reason), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:cinematic", {
        detail: { state: UTF8ToString(state), name: UTF8ToString(name), reason: UTF8ToString(reason) }
    }));
});
WebCinematicDecoder decoder;
int movieFile = 0;
int movieSize = 0;
char currentName[256]{};
char nextName[256]{};
unsigned nextFlags = 0, playbackFlags = 0;
bool started = false, finished = true, paused = false, inputEnded = false;
double startedAt = 0, pausedAt = 0, decodedTime = -1;
float playbackVolume = 1;
GfxImage movieImage{};
Material movieMaterial{};
MaterialTextureDef movieTexture{};
std::vector<std::uint8_t> pixels;
std::vector<WebCinematicAudio> audioBlocks;
std::array<ALuint, 1> audioSources{};

int ReadMovie(void *, std::uint8_t *bytes, int count)
{
    return static_cast<int>(FS_Read(bytes, count, movieFile));
}
std::int64_t SeekMovie(void *, std::int64_t offset, int whence)
{
    if (whence == SEEK_CUR) offset += FS_FTell(movieFile);
    else if (whence == SEEK_END) offset += movieSize;
    else if (whence != SEEK_SET) return -1;
    if (offset < 0 || offset > movieSize || FS_Seek(movieFile, static_cast<int>(offset), SEEK_SET))
        return -1;
    return FS_FTell(movieFile);
}
double Position()
{
    return std::max(0.0, ((paused ? pausedAt : emscripten_get_now()) - startedAt) / 1000.0);
}
void ReleaseAudio()
{
    for (auto &source : audioSources)
    {
        if (!source) continue;
        alSourceStop(source);
        ALint queued = 0;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0)
        {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(source, 1, &buffer);
            alDeleteBuffers(1, &buffer);
        }
        alDeleteSources(1, &source);
        source = 0;
    }
}
void FailPlayback(const char *reason)
{
    finished = true;
    ReleaseAudio();
    PublishMovie("failed", currentName, reason);
}
bool QueueAudio()
{
    for (const auto &block : audioBlocks)
    {
        auto &source = audioSources[block.track];
        if (!source)
        {
            alGenSources(1, &source);
            if (!source) return false;
            WebOpenAL_SetSourceAlias(source, "$cinematic");
            alSourcef(source, AL_GAIN, playbackVolume);
        }
        ALint processed = 0, queued = 0;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0)
        {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(source, 1, &buffer);
            alDeleteBuffers(1, &buffer);
        }
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
        if (queued >= 128) return false;
        std::vector<ALshort> pcm(block.samples.size());
        for (std::size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = static_cast<ALshort>(std::clamp(block.samples[i] * 32768.0f, -32768.0f, 32767.0f));
        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (!buffer) return false;
        alBufferData(buffer, block.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16,
            pcm.data(), static_cast<ALsizei>(pcm.size() * sizeof(ALshort)), block.sampleRate);
        alSourceQueueBuffers(source, 1, &buffer);
    }
    for (const auto source : audioSources)
    {
        if (!source || paused) continue;
        ALint state = 0;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) alSourcePlay(source);
    }
    return true;
}
bool UploadVideo(const WebCinematicVideo &video)
{
    pixels.resize(static_cast<std::size_t>(video.width) * video.height * 4);
    // ponytail: CPU conversion uses the existing retained UI image boundary;
    // move planar conversion to the GPU if measured frame cost requires it.
    for (int y = 0; y < video.height; ++y)
        for (int x = 0; x < video.width; ++x)
        {
            const float luma = video.planes[0][y * video.strides[0] + x];
            const float cb = video.planes[1][(y / 2) * video.strides[1] + x / 2];
            const float cr = video.planes[2][(y / 2) * video.strides[2] + x / 2];
            float red, green, blue;
            if (video.fullRange)
            {
                red = luma + 1.402f * (cr - 128);
                green = luma - .344136f * (cb - 128) - .714136f * (cr - 128);
                blue = luma + 1.772f * (cb - 128);
            }
            else
            {
                // Same limited-range transform as the native Bink texture shader.
                const float base = luma * 1.164123535f;
                red = base + 1.595794678f * cr - .87065506f * 255;
                green = base - .813476563f * cr - .391448975f * cb + .529705048f * 255;
                blue = base + 2.017822266f * cb - 1.081668854f * 255;
            }
            auto *pixel = &pixels[(static_cast<std::size_t>(y) * video.width + x) * 4];
            pixel[0] = static_cast<std::uint8_t>(std::clamp(red, 0.0f, 255.0f));
            pixel[1] = static_cast<std::uint8_t>(std::clamp(green, 0.0f, 255.0f));
            pixel[2] = static_cast<std::uint8_t>(std::clamp(blue, 0.0f, 255.0f));
            pixel[3] = video.alpha ? video.planes[3][y * video.strides[3] + x] : 255;
        }
    movieImage.name = "$cinematic";
    movieImage.mapType = MAPTYPE_2D;
    movieImage.width = video.width;
    movieImage.height = video.height;
    movieImage.depth = 1;
    movieImage.noPicmip = true;
    return WebRenderer_UpdateUiImage(&movieImage, pixels.data(), pixels.size());
}
void DecodeFrame()
{
    WebCinematicVideo video;
    const int result = decoder.ReadFrame(video, audioBlocks);
    if (result < 0) { FailPlayback(decoder.Error()); return; }
    if (!result) { inputEnded = true; return; }
    decodedTime = video.seconds;
    if (!UploadVideo(video)) { FailPlayback("cinematic-image-upload-failed"); return; }
    if (!QueueAudio()) FailPlayback("cinematic-audio-queue-failed");
}
}

void WebCinematic_Update()
{
    if (!started || finished || paused) return;
    const double position = Position();
    // Bound per-pump work so background throttling cannot block the host loop.
    for (int count = 0; count < 4 && !inputEnded && !finished &&
        decodedTime + decoder.FrameSeconds() <= position; ++count) DecodeFrame();
    if (inputEnded && position >= decoder.Duration())
    {
        if (playbackFlags & 2) R_Cinematic_StartPlayback(currentName, playbackFlags, playbackVolume);
        else { finished = true; PublishMovie("ended", currentName, ""); }
    }
}
void __cdecl R_Cinematic_SyncNow() { WebCinematic_Update(); }
bool R_Cinematic_GetPlaybackInfo(char *name, std::size_t nameSize, std::uint32_t *timeInMsec)
{
    if (!name || !nameSize || !timeInMsec || !started) return false;
    I_strncpyz(name, currentName, static_cast<int>(nameSize));
    *timeInMsec = static_cast<std::uint32_t>(std::max(0.0, decodedTime) * 1000);
    return true;
}
bool R_Cinematic_IsNextReady() { return nextName[0] != 0; }
bool R_Cinematic_IsPending() { return currentName[0] != 0; }
bool R_Cinematic_IsStarted() { return started && !finished; }
bool R_Cinematic_IsFinished() { return finished; }
void R_Cinematic_StopPlayback()
{
    ReleaseAudio();
    decoder.Close();
    if (movieFile) FS_FCloseFile(movieFile);
    movieFile = movieSize = 0;
    WebRenderer_ReleaseUiImage(&movieImage);
    std::vector<std::uint8_t>().swap(pixels);
    audioBlocks.clear();
    started = false;
    finished = true;
    currentName[0] = 0;
}
void __cdecl R_Cinematic_StartPlayback(char *name, unsigned int flags, float volume)
{
    char requested[sizeof(currentName)]{};
    const bool validLength = name && std::strlen(name) < sizeof(requested);
    I_strncpyz(requested, name ? name : "", sizeof(requested));
    R_Cinematic_StopPlayback();
    I_strncpyz(currentName, requested, sizeof(currentName));
    bool valid = validLength && requested[0];
    for (const char *p = requested; *p; ++p)
        valid &= (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_';
    if (!valid) { PublishMovie("failed", currentName, "invalid-cinematic-name"); return; }
    char path[sizeof(currentName) + 16];
    std::snprintf(path, sizeof(path), "video/%s.bik", currentName);
    if (FS_Initialized()) movieSize = static_cast<int>(FS_FOpenFileRead(path, &movieFile));
    if (!movieFile)
    {
        PublishMovie("skipped", currentName, "movie-not-imported");
        return;
    }
    if (!decoder.Open({nullptr, movieSize, ReadMovie, SeekMovie}))
    {
        FailPlayback(decoder.Error());
        decoder.Close();
        FS_FCloseFile(movieFile);
        movieFile = 0;
        return;
    }
    playbackFlags = flags;
    playbackVolume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0;
    paused = inputEnded = false;
    started = true;
    finished = false;
    decodedTime = -1;
    DecodeFrame();
    startedAt = emscripten_get_now();
    if (!finished) PublishMovie("started", currentName, "");
}
void R_Cinematic_SetNextPlayback(const char *name, unsigned int flags)
{
    I_strncpyz(nextName, name ? name : "", sizeof(nextName));
    nextFlags = flags;
}
void __cdecl R_Cinematic_StartNextPlayback()
{
    const float scale = snd_cinematicVolumeScale ? snd_cinematicVolumeScale->current.value : 1;
    R_Cinematic_StartPlayback(nextName, nextFlags, static_cast<float>(SND_GetVolumeNormalized()) * scale);
    nextName[0] = 0;
}
void R_Cinematic_UnsetNextPlayback() { nextName[0] = 0; }
void __cdecl R_Cinematic_SetPaused(CinematicEnum value)
{
    const bool requested = static_cast<int>(value) != 0;
    if (!started || finished || paused == requested) return;
    const double now = emscripten_get_now();
    if (requested) pausedAt = now;
    else startedAt += now - pausedAt;
    paused = requested;
    for (const auto source : audioSources)
        if (source) { if (paused) alSourcePause(source); else alSourcePlay(source); }
}
void __cdecl R_Cinematic_DrawStretchPic_Letterboxed()
{
    if (!started || finished) return;
    Material *white = Material_RegisterHandle("white", 3);
    if (!white) return;
    movieMaterial = *white;
    movieMaterial.info.name = "$cinematic";
    movieMaterial.textureCount = 1;
    movieMaterial.textureTable = &movieTexture;
    movieTexture.semantic = 0;
    movieTexture.samplerState = 0x62;
    movieTexture.u.image = &movieImage;
    const float width = cls.vidConfig.displayWidth, height = cls.vidConfig.displayHeight;
    const float movieHeight = std::min(height, width * cls.vidConfig.aspectRatioDisplayPixel / 1.7777778f);
    const float black[4]{0, 0, 0, 1};
    R_AddCmdDrawStretchPic(0, 0, width, height, 0, 0, 1, 1, black, white);
    R_AddCmdDrawStretchPic(0, (height - movieHeight) * .5f, width, movieHeight,
        0, 0, 1, 1, colorWhite, &movieMaterial);
}
#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_DiagnosticCinematicOmission()
{
    char name[] = "diagnostic_intro";
    R_Cinematic_StartPlayback(name, 0, 1);
    return R_Cinematic_IsFinished() ? 1 : 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestCinematicState(int operation)
{
    if (operation == 1 || operation == 2)
        R_Cinematic_SetPaused(static_cast<CinematicEnum>(operation == 1 ? 1 : 0));
    if (operation == 3) return (R_Cinematic_IsStarted() ? 1 : 0) | (R_Cinematic_IsFinished() ? 2 : 0);
    std::uint32_t milliseconds = 0;
    char name[256];
    R_Cinematic_GetPlaybackInfo(name, sizeof(name), &milliseconds);
    return milliseconds;
}
#endif
