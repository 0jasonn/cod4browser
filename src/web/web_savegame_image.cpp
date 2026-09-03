#include <universal/q_shared.h>
#include <client/client.h>
#include <gfx_d3d/r_savegame_image.h>
#include <gfx_d3d/r_material.h>
#if KISAK_WEB_DIAGNOSTICS
#include <gfx_d3d/r_rendercmds.h>
#include <gfx_d3d/r_cmds.h>
#endif
#include <universal/com_files.h>
#include <ui/ui.h>
#include <web/web_renderer.h>
#include <emscripten.h>
#include <array>
#include <memory>
#include <vector>

namespace
{
constexpr std::size_t PIXEL_BYTES = SAVEGAME_IMAGE_SIZE * SAVEGAME_IMAGE_SIZE * 4u;
Material rawMaterial{};
GfxImage rawImage{};
MaterialTextureDef rawTexture{};
char rawName[64]{};
unsigned rawGeneration = 0;
bool rawReady = false;

// The codec owns temporary byte copies only. All path checks, save identity,
// filesystem writes and material publication stay on the C++ side.
EM_JS(int, RunImageCodec, (int encode, const uint8_t *data, unsigned size,
    unsigned context, unsigned complete), {
    const tasks = Module["webImageTasks"] ||= new Set();
    if (Module["webImageClosing"] || tasks.size >= 4) return 0;
    const bytes = HEAPU8.slice(data, data + size);
    const work = (async () => {
        let result = null;
        try {
            const canvas = new OffscreenCanvas(512, 512);
            const ctx = canvas.getContext("2d");
            if (encode) {
                ctx.putImageData(new ImageData(new Uint8ClampedArray(bytes.buffer), 512, 512), 0, 0);
                const blob = await canvas.convertToBlob({type: "image/jpeg", quality: 0.9});
                if (blob.type !== "image/jpeg" || blob.size > 2097152) throw new Error("JPEG encoding failed");
                result = new Uint8Array(await blob.arrayBuffer());
            } else {
                const bitmap = await createImageBitmap(new Blob([bytes], {type: "image/jpeg"}),
                    {colorSpaceConversion: "none", imageOrientation: "none", premultiplyAlpha: "none"});
                try {
                    if (bitmap.width !== 512 || bitmap.height !== 512) throw new Error("Invalid save image dimensions");
                    ctx.drawImage(bitmap, 0, 0);
                    result = ctx.getImageData(0, 0, 512, 512).data;
                } finally { bitmap.close(); }
            }
        } catch (error) { err("Save image codec: " + error); }
        let pointer = 0;
        try {
            if (result) {
                pointer = _malloc(result.length);
                if (pointer) HEAPU8.set(result, pointer);
            }
            getWasmTableEntry(complete)(context, pointer, pointer ? result.length : 0);
        } finally { if (pointer) _free(pointer); }
    })();
    tasks.add(work);
    work.finally(() => tasks.delete(work)).catch(error => err("Save image completion: " + error));
    return 1;
});

struct SaveImageJob { std::array<uint8_t, sizeof(SaveHeader)> header; };
// Only capture requests wait here; save/game state remains in SaveMemory.
// A map unload cancels them before another world can produce their pixels.
std::array<std::unique_ptr<SaveImageJob>, 4> pendingCaptures;

void CompleteSave(unsigned context, const uint8_t *bytes, unsigned size)
{
    std::unique_ptr<SaveImageJob> job(reinterpret_cast<SaveImageJob *>(context));
    SaveHeader saved{};
    memcpy(&saved, job->header.data(), sizeof(saved));
    if (!bytes || !R_IsSaveGameJpeg({bytes, size}))
    {
        Com_PrintWarning(8, "Could not encode save image for '%s'\n", saved.filename);
        return;
    }
    // Encoding is asynchronous. Never resurrect a deleted save or attach an
    // older frame to a replacement save with the same filename.
    int file = 0;
    FS_FOpenFileRead(saved.filename, &file);
    if (!file)
    {
#if KISAK_WEB_DIAGNOSTICS
        Com_Printf(8, "Save image discarded: save no longer exists '%s'\n", saved.filename);
#endif
        return;
    }
    std::array<uint8_t, sizeof(SaveHeader)> current{};
    const unsigned read = FS_Read(current.data(), current.size(), file);
    FS_FCloseFile(file);
    if (read != current.size() || current != job->header)
    {
#if KISAK_WEB_DIAGNOSTICS
        unsigned offset = 0;
        while (offset < current.size() && current[offset] == job->header[offset]) ++offset;
        Com_Printf(8, "Save image discarded: changed header '%s' (read=%u firstDifference=%u)\n",
            saved.filename, read, offset);
#endif
        return;
    }
    char path[sizeof(saved.filename)];
    I_strncpyz(path, saved.filename, sizeof(path));
    memcpy(path + strlen(path) - 3, "jpg", 3);
    if (!FS_WriteFileToDir(path, "players", reinterpret_cast<char *>(const_cast<uint8_t *>(bytes)), size))
        Com_PrintWarning(8, "Could not write save image '%s'\n", path);
    else UI_SaveGameShotUpdated(path);
}

void CompleteRawImage(unsigned generation, const uint8_t *rgba, unsigned size)
{
    if (generation != rawGeneration || !rgba || size != PIXEL_BYTES) return;
    rawImage.mapType = MAPTYPE_2D;
    rawImage.width = rawImage.height = SAVEGAME_IMAGE_SIZE;
    rawImage.depth = 1;
    rawImage.noPicmip = true;
    rawImage.name = rawName;
    rawReady = WebRenderer_SetRawUiImage(&rawImage, rgba, size);
}
} // namespace

void R_SaveGameThumbnail(const SaveHeader &header)
{
    const size_t length = strnlen(header.filename, sizeof(header.filename));
    if (length < 4 || length == sizeof(header.filename) ||
        I_stricmp(header.filename + length - 4, ".svg")) return;
    char path[sizeof(header.filename)];
    I_strncpyz(path, header.filename, sizeof(path));
    memcpy(path + length - 3, "jpg", 3);
    FS_DeleteInDir(path, (char *)"players");
    UI_SaveGameShotUpdated(path);
    auto job = std::make_unique<SaveImageJob>();
    memcpy(job->header.data(), &header, sizeof(header));
    // Coalesce replacement saves while waiting for the renderer/codec.
    auto *slot = &pendingCaptures[0];
    for (auto &pending : pendingCaptures)
    {
        if (!pending) slot = &pending;
        else
        {
            SaveHeader previous{};
            memcpy(&previous, pending->header.data(), sizeof(previous));
            if (!I_stricmp(previous.filename, header.filename)) { slot = &pending; break; }
        }
    }
    *slot = std::move(job);
    WebSaveImage_CapturePending();
}

void WebSaveImage_CapturePending()
{
    for (auto &job : pendingCaptures)
    {
        if (!job) continue;
        SaveHeader header{};
        memcpy(&header, job->header.data(), sizeof(header));
        std::vector<uint8_t> rgba;
        if (!WebRenderer_ReadSaveGameShot(rgba, header.mapName)) continue;
        if (RunImageCodec(1, rgba.data(), rgba.size(), reinterpret_cast<unsigned>(job.get()),
            reinterpret_cast<unsigned>(CompleteSave))) job.release();
    }
}

void WebSaveImage_CancelPending()
{
    for (auto &job : pendingCaptures) job.reset();
}

void Material_InvalidateRawImage()
{
    ++rawGeneration;
    rawReady = false;
    rawName[0] = 0;
}

Material *Material_RegisterRawImage(const char *name, int imageTrack)
{
    if (!name || !*name || strlen(name) >= sizeof(rawName)) return nullptr;
    if (I_stricmp(name, rawName))
    {
        Material_InvalidateRawImage();
        I_strncpyz(rawName, name, sizeof(rawName));
        int file = 0;
        const unsigned size = FS_FOpenFileRead(name, &file);
        if (!file) return nullptr;
        std::vector<uint8_t> jpeg(size <= SAVEGAME_JPEG_MAX_BYTES ? size : 0);
        const unsigned read = jpeg.empty() ? 0 : FS_Read(jpeg.data(), size, file);
        FS_FCloseFile(file);
        if (read != size || !R_IsSaveGameJpeg(jpeg)) return nullptr;
        if (!RunImageCodec(0, jpeg.data(), jpeg.size(), rawGeneration, reinterpret_cast<unsigned>(CompleteRawImage)))
            rawName[0] = 0; // A full codec queue is temporary; retry next UI frame.
    }
    if (!rawReady) return nullptr;
    // This API is used only by the single selected save preview. Reuse one
    // canonical material/image and one backend texture across selections.
    Material *base = Material_RegisterHandle("white", imageTrack);
    if (!base) return nullptr;
    rawMaterial = *base;
    rawMaterial.info.name = rawName;
    rawMaterial.textureCount = 1;
    rawMaterial.textureTable = &rawTexture;
    rawTexture.semantic = 0;
    rawTexture.samplerState = 0x62;
    rawTexture.u.image = &rawImage;
    return &rawMaterial;
}

#if KISAK_WEB_DIAGNOSTICS
namespace
{
int testCodecResult = 0;
GfxImage testImage{};
Material testMaterial{};
MaterialTextureDef testTexture{};
void TestDecoded(unsigned, const uint8_t *rgba, unsigned size)
{
    if (!rgba || size != PIXEL_BYTES) { testCodecResult = -2; return; }
    testImage.mapType = MAPTYPE_2D;
    testImage.name = "synthetic-save-jpeg";
    testImage.width = testImage.height = SAVEGAME_IMAGE_SIZE;
    testImage.depth = 1;
    const auto top = (128 * 512 + 128) * 4;
    const auto bottom = (384 * 512 + 128) * 4;
    testCodecResult = 1 | (rgba[top] > 250 && rgba[top + 1] < 3 ? 2 : 0) |
        (rgba[bottom] < 3 && rgba[bottom + 1] > 250 ? 4 : 0) |
        (WebRenderer_SetRawUiImage(&testImage, rgba, size) ? 8 : 0);
    testMaterial.info.name = testImage.name;
    testMaterial.textureCount = 1;
    testMaterial.textureTable = &testTexture;
    testTexture.u.image = &testImage;
    testTexture.samplerState = 0x62;
}
void TestEncoded(unsigned, const uint8_t *jpeg, unsigned size)
{
    if (!jpeg || !R_IsSaveGameJpeg({jpeg, size}) ||
        !RunImageCodec(0, jpeg, size, 0, reinterpret_cast<unsigned>(TestDecoded))) testCodecResult = -3;
}
}
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestSaveImage(int operation)
{
    if (operation == 0)
    {
        std::vector<uint8_t> rgba(PIXEL_BYTES, 0);
        for (unsigned at = 0; at < rgba.size(); at += 4)
        {
            rgba[at + (at < PIXEL_BYTES / 2 ? 0 : 1)] = 255;
            rgba[at + 3] = 255;
        }
        testCodecResult = -1;
        if (!RunImageCodec(1, rgba.data(), rgba.size(), 0, reinterpret_cast<unsigned>(TestEncoded))) return 0;
        return 1;
    }
    if (operation == 1) return testCodecResult;
    if (operation == 4)
    {
        // A capture waiting for a world which has not rendered yet.
        SaveHeader header{};
        strcpy(header.filename, "synthetic.svg");
        strcpy(header.mapName, "synthetic");
        pendingCaptures[0] = std::make_unique<SaveImageJob>();
        memcpy(pendingCaptures[0]->header.data(), &header, sizeof(header));
        WebSaveImage_CapturePending();
    }
    if (operation == 4 || operation == 5)
    {
        int count = 0;
        for (const auto &job : pendingCaptures) count += job != nullptr;
        return count;
    }
    if (operation == 2 || operation == 3)
    {
        cls.vidConfig.displayWidth = EM_ASM_INT({ return Module.canvas.width; });
        cls.vidConfig.displayHeight = EM_ASM_INT({ return Module.canvas.height; });
        const float white[4]{1, 1, 1, 1};
        R_BeginFrame();
        R_AddCmdDrawStretchPic(0, 0, 512, 512, 0, 0, 1, 1, white, &testMaterial);
        R_EndFrame();
        const int height = EM_ASM_INT({ return Module.canvas.height; });
        const auto pixel = WebRenderer_TestDrawPixel(128, height - (operation == 2 ? 128 : 384));
        WebRenderer_SetUiScene({});
        return pixel;
    }
    return -1;
}
#endif
