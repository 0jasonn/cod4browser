#include <gfx_d3d/r_cinematic_api.h>
#include <client/client.h>
#include <database/database.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_rendercmds.h>
#include <sound/snd_public.h>
#include <universal/com_files.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <web/web_cinematic_decoder.h>
#include <web/web_cinematic.h>
#include <web/web_openal_proxy.h>
#include <web/web_renderer.h>
#include <emscripten.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

GfxCmdBufInput gfxCmdBufInput{};

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
bool clockStarted = false;
double startedAt = 0, pausedAt = 0, decodedTime = -1;
WebCinematicVideo pendingVideo;
bool hasPendingVideo = false;
double audioDuration = 0;
float playbackVolume = 1;
std::array<GfxImage, 4> movieImages{};
bool fullRange = false, videoPublished = false;
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
    if (!clockStarted) return 0;
    double seconds = 0;
    if (audioSources[0] && WebOpenAL_SourcePlaybackSeconds(audioSources[0], seconds))
        return seconds;
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
    WebCinematic_ReleaseImages();
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
        audioDuration += static_cast<double>(block.samples.size()) / block.channels / block.sampleRate;
    }
    for (const auto source : audioSources)
    {
        if (!source || paused) continue;
        ALint state = 0;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        ALint queued = 0, processed = 0;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
        if (state != AL_PLAYING && queued > processed) alSourcePlay(source);
    }
    return true;
}
bool UploadVideo(const WebCinematicVideo *video)
{
    if (video)
    {
        if (video->width < 1 || video->width > 1920 || video->height < 1 || video->height > 1080)
            return false;
        for (unsigned i = 0; i < (video->alpha ? 4u : 3u); ++i)
        {
            const int width = i == 1 || i == 2 ? (video->width + 1) / 2 : video->width;
            if (!video->planes[i] || video->strides[i] < width) return false;
        }
    }
    constexpr const char *names[]{"$cinematicY", "$cinematicCr", "$cinematicCb", "$cinematicA"};
    constexpr unsigned decoderPlanes[]{0, 2, 1, 3};
    constexpr std::uint8_t blank[]{0, 128, 128, 0};
    for (unsigned i = 0; i < movieImages.size(); ++i)
    {
        const bool constant = !video || (i == 3 && !video->alpha);
        const bool chroma = i == 1 || i == 2;
        const int width = constant ? 1 : chroma ? (video->width + 1) / 2 : video->width;
        const int height = constant ? 1 : chroma ? (video->height + 1) / 2 : video->height;
        pixels.resize(static_cast<std::size_t>(width) * height);
        if (constant) pixels[0] = video ? 255 : blank[i];
        else for (int y = 0; y < height; ++y)
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width,
                video->planes[decoderPlanes[i]] + y * video->strides[decoderPlanes[i]], width);
        auto &image = movieImages[i];
        image.name = names[i];
        image.mapType = MAPTYPE_2D;
        image.width = width;
        image.height = height;
        image.depth = 1;
        image.noPicmip = true;
        if (!WebRenderer_UpdateUiImage(&image, pixels.data(), pixels.size(), 1)) return false;
    }
    for (unsigned i = 0; i < movieImages.size(); ++i)
    {
        gfxCmdBufInput.codeImages[TEXTURE_SRC_CODE_CINEMATIC_Y + i] = &movieImages[i];
        gfxCmdBufInput.codeImageSamplerStates[TEXTURE_SRC_CODE_CINEMATIC_Y + i] = 0x62;
    }
    fullRange = video && video->fullRange;
    videoPublished = video != nullptr;
    return true;
}
void DecodeFrame()
{
    const int result = decoder.ReadFrame(pendingVideo, audioBlocks);
    if (result < 0) { FailPlayback(decoder.Error()); return; }
    if (!result) { inputEnded = true; return; }
    hasPendingVideo = true;
    if (!QueueAudio()) FailPlayback("cinematic-audio-queue-failed");
}
}

void WebCinematic_Update()
{
    if (!gfxCmdBufInput.codeImages[TEXTURE_SRC_CODE_CINEMATIC_Y] && !UploadVideo(nullptr)) return;
    if (!started || finished || paused) return;
    if (!clockStarted)
    {
        // Begin the clock at the first presented frame, including a loading
        // keepalive while the canonical map command is suspended.
        clockStarted = true;
        startedAt = emscripten_get_now();
        PublishMovie("started", currentName, "");
    }
    // Keep one decoded frame pending: its audio must be queued before display
    // time. The decoder owns its planes until the next ReadFrame, so this adds
    // no copied frame queue. Bound catch-up work after a Worker stall.
    for (int count = 0; count < 4 && !finished; ++count)
    {
        if (!hasPendingVideo && !inputEnded) DecodeFrame();
        if (finished || !hasPendingVideo || pendingVideo.seconds > Position()) break;
        if (!UploadVideo(&pendingVideo)) { FailPlayback("cinematic-image-upload-failed"); return; }
        decodedTime = pendingVideo.seconds;
        hasPendingVideo = false;
    }
    if (!hasPendingVideo && !inputEnded && !finished) DecodeFrame();
    if (inputEnded && Position() >= std::max(decoder.Duration(), audioDuration))
    {
        if (playbackFlags & 2) R_Cinematic_StartPlayback(currentName, playbackFlags, playbackVolume);
        else { finished = true; PublishMovie("ended", currentName, ""); }
    }
}
const GfxImage *WebCinematic_PlaneImage(unsigned plane)
{
    return plane < 4 ? gfxCmdBufInput.codeImages[TEXTURE_SRC_CODE_CINEMATIC_Y + plane] : nullptr;
}
bool WebCinematic_FullRange() { return fullRange; }
void WebCinematic_ReleaseImages()
{
    for (const auto &image : movieImages) WebRenderer_ReleaseUiImage(&image);
    std::fill_n(gfxCmdBufInput.codeImages + TEXTURE_SRC_CODE_CINEMATIC_Y, 4, nullptr);
    fullRange = false;
    videoPublished = false;
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
    // CG_DrawFadeInCinematic calls stop every frame while idle. Keep the
    // inactive planes instead of recreating four GL textures on every call.
    if (!started && !movieFile && !currentName[0] && !videoPublished) return;
    ReleaseAudio();
    decoder.Close();
    if (movieFile) FS_FCloseFile(movieFile);
    movieFile = movieSize = 0;
    WebCinematic_ReleaseImages();
    UploadVideo(nullptr);
    std::vector<std::uint8_t>().swap(pixels);
    audioBlocks.clear();
    hasPendingVideo = false;
    pendingVideo = {};
    audioDuration = 0;
    clockStarted = false;
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
    clockStarted = false;
    started = true;
    finished = false;
    decodedTime = -1;
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
    if (clockStarted)
    {
        if (requested) pausedAt = now;
        else startedAt += now - pausedAt;
    }
    paused = requested;
    for (const auto source : audioSources)
        if (source) { if (paused) alSourcePause(source); else alSourcePlay(source); }
}
void __cdecl R_Cinematic_DrawStretchPic_Letterboxed()
{
    if (!started || finished) return;
    Material *white = Material_RegisterHandle("white", 3);
    if (!white) return;
    Material *cinematic = Material_RegisterHandle("cinematic", 3);
    if (!cinematic) return;
    const float width = cls.vidConfig.displayWidth, height = cls.vidConfig.displayHeight;
    const float movieHeight = std::min(height, width * cls.vidConfig.aspectRatioDisplayPixel / 1.7777778f);
    const float black[4]{0, 0, 0, 1};
    R_AddCmdDrawStretchPic(0, 0, width, height, 0, 0, 1, 1, black, white);
    R_AddCmdDrawStretchPic(0, (height - movieHeight) * .5f, width, movieHeight,
        0, 0, 1, 1, colorWhite, cinematic);
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
    if ((operation >= 10 && operation <= 13) || operation == 15 || operation == 16)
    {
        // Synthetic decoder planes: odd dimensions and padded row strides.
        // No movie fixture, gameplay state, or extra exported diagnostic API.
        std::uint8_t y[12], cb[6], cr[6], alpha[12];
        std::fill_n(y, 12, 0);
        std::fill_n(cb, 6, 0);
        std::fill_n(cr, 6, 0);
        std::fill_n(alpha, 12, 0);
        for (int row = 0; row < 3; ++row)
        {
            std::fill_n(y + row * 4, 3, operation == 10 ? 81 : operation == 11 ? 145 : 128);
            std::fill_n(alpha + row * 4, 3, operation == 11 ? 128 : 64);
        }
        for (int row = 0; row < 2; ++row)
        {
            std::fill_n(cb + row * 3, 2, operation == 10 ? 90 : operation == 11 ? 54 : 128);
            std::fill_n(cr + row * 3, 2, operation == 10 ? 240 : operation == 11 ? 34 : 128);
        }
        if (operation == 12)
        {
            // The centre sample must interpolate all four chroma texels.
            cb[0] = cr[0] = cb[4] = cr[4] = 0;
            cb[1] = cr[1] = cb[3] = cr[3] = 255;
        }
        WebCinematicVideo video;
        video.planes = {y, cb, cr, alpha};
        video.strides = {4, 3, 3, 4};
        video.width = video.height = 3;
        video.alpha = operation != 10;
        video.fullRange = operation >= 12;
        if (operation == 15) video.strides[3] = 0;
        if (operation == 16) video.width = 1921;
        return UploadVideo(&video);
    }
    if (operation == 14) { R_Cinematic_StopPlayback(); return 1; }
    if (operation >= 20 && operation <= 23)
        return WebRenderer_TestCinematicPixel(operation >= 22, operation % 2 != 0);
    if (operation == 24) return WebRenderer_TestCinematicPixel(false, 2);
    if (operation == 4)
    {
        int count = 0;
        DB_EnumXAssets(ASSET_TYPE_MATERIAL, [](XAssetHeader header, void *context) {
            const Material *material = header.material;
            const auto *set = material ? material->techniqueSet : nullptr;
            if (set && set->remappedTechniqueSet) set = set->remappedTechniqueSet;
            if (!set) return;
            for (int type = 0; type < 34; ++type)
            {
                const auto *technique = set->techniques[type];
                if (!technique) continue;
                for (unsigned p = 0; p < technique->passCount; ++p)
                {
                    const auto &pass = technique->passArray[p];
                    unsigned mask = 0;
                    const unsigned argCount = pass.perPrimArgCount + pass.perObjArgCount + pass.stableArgCount;
                    for (unsigned a = 0; pass.args && a < argCount; ++a)
                        if (pass.args[a].type == MTL_ARG_CODE_PIXEL_SAMPLER &&
                            pass.args[a].u.codeSampler >= TEXTURE_SRC_CODE_CINEMATIC_Y &&
                            pass.args[a].u.codeSampler <= TEXTURE_SRC_CODE_CINEMATIC_A)
                            mask |= 1u << (pass.args[a].u.codeSampler - TEXTURE_SRC_CODE_CINEMATIC_Y);
                    if (!mask) continue;
                    ++*static_cast<int *>(context);
                    Com_Printf(0, "CINEMATIC_MATERIAL name=%s type=%d pass=%u mask=%u tech=%s ps=%s vs=%s\n",
                        material->info.name, type, p, mask, technique->name,
                        pass.pixelShader ? pass.pixelShader->name : "<none>",
                        pass.vertexShader ? pass.vertexShader->name : "<none>");

                }
            }
        }, &count, false);
        return count;
    }
    if (operation == 1 || operation == 2)
        R_Cinematic_SetPaused(static_cast<CinematicEnum>(operation == 1 ? 1 : 0));
    if (operation == 3) return (R_Cinematic_IsStarted() ? 1 : 0) | (R_Cinematic_IsFinished() ? 2 : 0);
    std::uint32_t milliseconds = 0;
    char name[256];
    R_Cinematic_GetPlaybackInfo(name, sizeof(name), &milliseconds);
    return milliseconds;
}
#endif
